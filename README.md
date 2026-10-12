# STM32F103 智慧农业终端

更新日期：**2026-10-12**。

基于 **STM32F103CBT6、STM32CubeMX、HAL 和 CMake** 开发的智慧农业终端固件。当前终端软件已接通环境采集、数据有效性管理、MANUAL/AUTO/SAFE 控制、执行器输出、OLED/按键交互及 **LoRa v3 双向命令与结果反馈**。

当前开发阶段为：**终端软件与主机模拟测试已完成当前功能阶段，网关板固件和无线实物联调待推进**。仓库中的 `gateway_command.c` 是可复用的纯 C 命令事务核心，不代表网关板、ESP8266 或云端接入已经完成。

## 当前实现状态

| 模块 | 当前进度 | 验证边界 |
| --- | --- | --- |
| 基础外设 | 72 MHz 时钟、SWD、GPIO、ADC/DMA、I2C1、USART1/2、SPI1、TIM1/4 和 EXTI | 已纳入 Debug/Release 固件构建 |
| 环境采集 | ADC 双通道循环采样与 16 次均值滤波，DHT22 周期采集，CO2 非阻塞消费 | 光照、土壤换算与传感器有效位已接入；物理精度仍需校准 |
| 数据模型 | `SensorData_t` 快照、更新时刻、错误码与数据失效判断 | 无效数据不参与对应控制策略 |
| 本地控制 | MANUAL/AUTO/SAFE、通风阈值滞回、手动租约、泵超时/冷却和紧急停止 | 自动灌溉默认禁用，需校准后通过配置接口启用 |
| 执行器 | PC14 继电器、PB8 风扇电机、PB9 蜂鸣器统一管理 | 电机与蜂鸣器共用 TIM4，独立控制通道；输出状态表示驱动指令 |
| OLED 与按键 | 环境、风扇、灌溉、通信、故障五页；消抖、长短按、变化行刷新 | 页面格式与交互有主机模拟覆盖 |
| LoRa 终端 | LLCC68 半双工收发、v3 帧校验、命令执行、结果回传、ACK 有限重试与事务去重 | 软件测试通过，实际射频链路尚未验证 |
| 网关事务核心 | 命令组帧、等待 RESULT、结果确认及事务游标恢复 | 纯 C 模块，尚未集成为完整网关板固件 |
| 联网与持久化 | ESP8266、云端/米家、Flash 配置保存、看门狗 | 尚未实现或验证 |

## 软件结构与运行流程

CubeMX 生成代码负责外设初始化，`User/app/app_main.c` 接管业务主循环。主要数据流为：

```text
ADC / DHT22 / CO2 → SensorData_t 快照 → 控制策略 → 执行器
                                      │             │
                                      └── OLED / LoRa 遥测与结果
按键 / LoRa COMMAND ──────────────────────→ 控制服务
```

当前启动与调度流程：

```text
上电复位
  ├─ HAL、72 MHz 时钟及 CubeMX 外设初始化
  ├─ 传感器、控制、按键/UI 和 LoRa 软件状态初始化
  │    └─ 默认 MANUAL，继电器关闭、风扇 0 档、蜂鸣器关闭
  ├─ OLED、调试串口、CO2 串口与 DHT22 初始化
  ├─ ADC 校准并启动双通道 DMA；失败时关闭执行器并进入错误处理
  └─ 主循环
       ├─ 每 500 ms 翻转 PC13 心跳灯
       ├─ 按键处理、页面操作与 OLED 更新
       ├─ 控制策略、输出更新和超时保护
       ├─ ADC 均值滤波、DHT22 采集与 CO2 接收消费
       ├─ LoRa 初始化重试、接收、发送及 COMMAND/ACK/RESULT 处理
       └─ 采集和射频任务之间再次执行控制安全检查
```

- ADC 每 100 ms 累积一组 DMA 数据，满 16 次后更新均值；DHT22 每 2 秒读取一次。
- CO2 每 20 ms 消费接收数据，使用 64 字节有界接收环和滑动帧校验，支持串口错误/超时恢复。
- 传感器状态约每 500 ms 更新，串口报告每 1 秒输出。
- 新遥测默认间隔 5 秒；ACK 重试与优先结果回传可能延长实际上报间隔。
- 按键和 DIO1 中断只记录事件，业务处理在前台进行。CO2 和 LoRa 不通过长循环等待数据或 TX_DONE，但 DHT22、I2C、SPI/BUSY 仍存在有界同步操作。

当前串口启动信息：

```text
Terminal ready: MANUAL, outputs off, duplex v3
```

周期报告包含 ADC 状态、温湿度或错误码、CO2 浓度或错误码，以及光照估算值和土壤等级。土壤等级不是湿度百分比，光照换算也需要根据实际传感器校准。

## 本地控制与按键

### 默认控制参数

下表为当前代码中的示例配置，尚未针对实际种植环境完成标定。

| 项目 | 默认行为 |
| --- | --- |
| 上电模式 | MANUAL，所有执行器关闭 |
| 温度通风 | 30℃ 请求风扇 2 档，28℃ 恢复关闭，中间区间保持请求 |
| CO2 通风 | 1200 ppm 请求风扇 3 档，1000 ppm 恢复关闭 |
| 多路通风请求 | 取较高档位，失效传感器不参与对应策略 |
| 风扇档位 | 0～4 档对应 0%、30%、50%、80%、100% 占空比，PWM 20 kHz |
| 手动风扇租约 | 60 秒，重复设置相同档位不续期 |
| 自动灌溉 | 默认禁用；示例为土壤干等级 1 开启、湿等级 3 关闭 |
| 泵运行上限 | 单次最大 10 秒，超时停止并锁存 SAFE |
| 泵停机冷却 | 60 秒，手动控制与模式切换也不能绕过 |
| 数据有效期 | ADC 3.2 秒、DHT22 6 秒、CO2 3 秒 |
| 启动宽限 | 8 秒后提示缺失传感器；宽限内也不以无效土壤数据驱动泵 |

紧急停止、泵超时、驱动失败或无效配置会触发 SAFE。无效/过期土壤数据会关闭泵。告警静音只关闭提示声，不清除告警和安全限制。SAFE 解锁仍受执行器就绪与冷却条件约束，远程命令不能解除本地紧急停止或泵超时锁。

### 按键操作

按键每 10 ms 采样，20 ms 消抖，1500 ms 判定长按；长按释放后不再产生短按事件。

| 操作 | 行为 |
| --- | --- |
| SW1 短按 | 环境 → 风扇 → 灌溉 → 通信 → 故障，循环切页 |
| SW1 长按 | MANUAL → AUTO；AUTO/SAFE → MANUAL，解锁需满足就绪检查 |
| SW2 短按：风扇页 | MANUAL 下循环设置 0～4 档 |
| SW2 短按：灌溉页 | MANUAL 下切换泵输出，仍检查土壤有效性与冷却 |
| SW2 短按：其他页 | 切换告警静音 |
| SW2 长按 | 停止泵/风扇，锁存 SAFE 并切到故障页 |

同轮两键长按以 SW2 紧急停止优先。OLED 每行最多 16 个 ASCII 字符，仅刷新变化行。

## LoRa v3 双向通信

当前无线协议为 **v3**，包含 `HELLO`、`TELEMETRY`、`COMMAND`、`RESULT` 和 `ACK` 五类消息。此前的 34 字节遥测 v2 仅保留为 v3 遥测的内层负载，**不兼容旧的裸 v2 无线帧**。

- 20 字节帧头，多字节字段小端，最大帧 96 字节，使用 CRC16/Modbus 检错。
- 当前为固定地址的单终端/单网关：终端地址 1，网关地址 0；不接受广播控制命令。
- 空闲连续接收，发送完成后返回 RX；射频 CRC、帧长度、地址、会话、事务和业务参数校验通过后才进入命令处理。
- 支持 `SET_MODE`、`SET_FAN`、`SET_PUMP`、`SET_MUTE`、`STOP_ALL` 和 `GET_STATUS`。远程操作仍经过本地控制服务和安全限制。
- HELLO/TELEMETRY/RESULT 等待匹配 ACK，单条可靠消息最多发送 **3 次（含首次）**；网关命令以 RESULT 判断业务结果。
- 使用结果缓存、事务高水位和重复结果重发，避免同一会话内重复执行命令；射频重连保留会话和去重状态。
- RESULT 优先于普通遥测；有界结果队列满时拒绝新命令，避免执行后无法保留结果。
- 网关重启后需先观察 HELLO/遥测中的命令事务高水位，再发送新命令；命令有效期按终端毫秒时基估算，最大 30 秒。
- 通信页显示 RX/TX、ACK、重试及命令计数。匹配 ACK 或当前会话的网关命令作为在线依据，15 秒无活动转离线。

射频参数当前配置为 **470.5 MHz、SF9、125 kHz 带宽、4/5 编码率、8 符号前导码、22 dBm 发射功率**，尚未进行实际频率、功率或链路测量。

`TX_DONE` 仅表示芯片完成发送；ACK 表示对应消息被确认；RESULT 的状态码才说明命令是否通过业务校验。执行成功反映的是输出指令已经应用，没有转速、流量或出水反馈。通信页的网关在线也不代表 WiFi 或云端在线。

完整帧字段、状态码、去重和网关事务规则见 [LoRa v3 双向协议说明](docs/lora-duplex-v3.md)。本地控制与遥测内层定义见 [终端本地控制与遥测说明](docs/terminal-control-and-telemetry.md)，其中标明的早期“未实现远程命令”内容是历史状态，当前功能以 v3 说明及源码为准。

## 硬件与引脚

下表根据当前 `.ioc`、`Core` 初始化和 `User` 驱动整理。

| 功能 | MCU 引脚 | 外设/配置 | 当前用途 |
| --- | --- | --- | --- |
| 光照采集 | PA0 | ADC1_IN0 + DMA | `adc_values[0]`，均值滤波与光照估算 |
| 土壤采集 | PA1 | ADC1_IN1 + DMA | `adc_values[1]`，土壤等级与有效性判断 |
| CO2 TX/RX | PA2 / PA3 | USART2，9600 8N1 | CO2 数据接收与解析 |
| LoRa NSS | PA4 | GPIO 输出 | SPI 软件片选 |
| LoRa SCK/MISO/MOSI | PA5 / PA6 / PA7 | SPI1 | LLCC68 通信 |
| LoRa BUSY / RESET | PB0 / PB1 | GPIO 输入 / 输出 | 芯片忙检测与复位 |
| LoRa DIO1 | PB10 | 上升沿 EXTI | 射频事件通知，前台处理 IRQ |
| OLED SCL/SDA | PB6 / PB7 | I2C1，400 kHz | SSD1306 五页显示 |
| 风扇电机 PWM | PB8 | TIM4_CH3，20 kHz | 五档占空比控制 |
| 有源蜂鸣器 | PB9 | TIM4_CH4，共用 20 kHz 时基 | 告警与静音控制 |
| 调试串口 TX/RX | PA9 / PA10 | USART1，115200 8N1 | `printf` 与 Receive-to-Idle 接收框架 |
| SW1 / SW2 | PB12 / PB13 | 下降沿 EXTI、内部上拉 | 前台消抖和长短按处理 |
| 告警 LED | PB15 | GPIO 输出，低电平亮 | 告警位图指示 |
| 心跳 LED | PC13 | GPIO 输出，低电平亮 | 每 500 ms 翻转 |
| 继电器 | PC14 | GPIO 输出 | 泵开关指令 |
| DHT22 | PC15 | GPIO 单总线 | 运行时切换输入/输出，DWT 微秒延时 |
| LED PWM 预留 | PA8 | TIM1_CH1 | 已配置，未接入当前业务主循环 |
| SWD | PA13 / PA14 | SWDIO / SWCLK | ST-Link 下载与调试 |

当前工程及链接脚本按 **128 KB Flash / 20 KB RAM** 配置；烧录前应核对目标板实际芯片型号与容量。DHT22 使用 PC15，修改时钟配置时需检查它与 LSE 引脚用途的关系。

## 目录结构

```text
Core/                        CubeMX 生成的启动、外设初始化和中断入口
Drivers/                     STM32 HAL / CMSIS
User/
  app/                       应用入口及基础按键、LED、PWM 示例
  bsp/                       光照、土壤、DHT22、CO2、调试串口和 HAL 回调
  service/                   传感器、控制策略、执行器、按键和 OLED 页面服务
  LoRa/                      LLCC68 驱动、v3 协议、可靠事务及终端/网关适配核心
  oled/                      SSD1306 驱动、字库与位图资源
docs/                        本地控制、遥测和双向协议说明
tests/
  terminal_control/          策略、执行器、CO2、UI、射频和终端服务模拟
  lora_duplex/               协议编解码与网关/终端双向事务模拟
  motor_control/             电机驱动 HAL 模拟回归测试
cmake/                       工具链与 CubeMX CMake 配置
Cube_MX_Code.ioc              外设与引脚配置
CMakeLists.txt               固件源文件及包含路径
CMakePresets.json            Debug / Release 构建预设
STM32F103XX_FLASH.ld          Flash / RAM 布局
```

## 构建与软件测试

固件构建需要 CMake、Ninja 和 GNU Arm 工具链；外设配置使用 STM32CubeMX。主机测试另外需要 **Python 和主机 GCC**，不能用 `arm-none-eabi-gcc` 代替主机编译器。

在项目根目录执行：

```powershell
# 首次配置或修改工具链后重新配置
cmake --preset Debug
cmake --preset Release

# 增量构建
cmake --build --preset Debug
cmake --build --preset Release

# 需要完整重建时
cmake --build --preset Debug --clean-first
cmake --build --preset Release --clean-first

# 主机软件测试，不操作开发板
python -X utf8 tests/lora_duplex/run_tests.py
python -X utf8 tests/terminal_control/run_tests.py
python -X utf8 tests/motor_control/run_tests.py

# 查看 ELF 体积和检查补丁空白问题
arm-none-eabi-size build/Debug/Cube_MX_Code.elf build/Release/Cube_MX_Code.elf
git diff --check
```

主机 GCC 不在 PATH 时，三个测试脚本均可通过 `--cc <主机GCC路径>` 指定编译器。固件产物位于 `build/Debug/` 和 `build/Release/`，包含 `Cube_MX_Code.elf`、`Cube_MX_Code.map` 与 `compile_commands.json`。

### 当前验证记录（2026-10-12）

- LoRa 双向协议：**8/8 组通过，0 断言失败**，覆盖 CRC/边界、ACK 错配与迟到、三次发送耗尽、tick 回绕、事务去重、丢包/重复帧和网关游标恢复。
- 终端控制：**6 个主机测试程序通过，0 断言失败**，覆盖策略、共享定时器通道、CO2 环形接收、UI/控制集成、射频启动/轮询和完整双向终端服务。
- 电机驱动：**14/14 用例通过，0 断言失败**。
- Debug/Release 增量构建均成功，Ninja 报告 `no work to do`；本次没有重新执行全量构建。2026-10-10 的全量构建验收记录见双向协议文档。

本次读取现有 ELF 的结果如下；Flash 为 `text + data`，静态 RAM 为 `data + bss`，其中链接脚本已预留的堆栈空间计入 `bss`。这些数值不代表运行时栈峰值测量。

| 配置 | text | data | bss | Flash | 静态 RAM |
| --- | ---: | ---: | ---: | ---: | ---: |
| Debug | 63,384 B | 192 B | 5,032 B | 63,576 B / 128 KB | 5,224 B / 20 KB |
| Release | 43,008 B | 192 B | 5,024 B | 43,200 B / 128 KB | 5,216 B / 20 KB |

本次验证未烧录开发板，也未进行传感器/执行器实物联调、两板无线通信、RSSI/丢包率测量或 ESP8266/云端测试。主机模拟通过不等于实际无线链路可靠性已经验证。

## 下载、调试与开发约定

1. 使用 ST-Link 连接 SWDIO、SWCLK、GND 和目标板参考电压。
2. 在 VS Code 中选择 `STM32Cube: Launch ST-Link GDB Server`，核对扩展当前选择的构建配置与 ELF 后启动调试。
3. Debug 对应 `build/Debug/Cube_MX_Code.elf`，Release 对应 `build/Release/Cube_MX_Code.elf`。当前调试配置由 STM32Cube 扩展动态选择产物，应以实际下载日志为准。
4. 检查编程与校验结果，再核对串口启动信息、传感器有效位、控制快照和 LoRa 状态；构建成功不能代替下载及运行验证。

业务代码优先放在 `User/` 或 CubeMX 的 `USER CODE BEGIN/END` 区域。修改外设时先更新 `.ioc` 并重新生成，随后复查初始电平、时钟、引脚标签、中断分发与 CMake 源文件列表。避免手动修改生成区导致下次重新生成丢失。

## 后续开发重点

1. 建立并核对独立网关板工程，集成 LLCC68 收发适配与现有 `gateway_command` 事务核心，完成节点会话、时基和事务游标管理。
2. 根据实际 ESP8266 固件实现串口 AT/联网适配，再接入计划中的云端和米家功能。
3. 补充串口 CLI、参数持久化与看门狗；完善 USART1 接收消费及缓冲区并发管理。
4. 开展传感器校准、按键/OLED 与执行器联调、两板 COMMAND/RESULT/ACK 测试，以及丢包、重启、断线和长时间运行测试。
5. 根据实测结果调整阈值、占空比、超时与射频参数。当前没有流量、液位、转速或电流反馈，不将软件泵超时等同于缺水/堵转检测；协议 CRC 和会话过滤也不提供加密或身份认证。
