# STM32F103 智慧农业终端

基于 **STM32F103CBT6、STM32CubeMX、HAL 和 CMake** 开发的智慧农业终端固件。项目当前已完成基础采集与调试框架，支持 ADC+DMA 获取光照和土壤传感器数据、OLED 初始化、双串口中断接收以及非阻塞式周期调度，并预留 DHT22、CO₂、执行器控制和 LoRa 通信的扩展接口。

> 当前仓库处于持续开发阶段。本文将“已实现并通过编译验证”的功能与“规划中”的功能分开说明，不代表所有传感器和执行器均已完成实物联调。

## 项目特点

- STM32F103CBT6，HSE 8 MHz 经 PLL 倍频至 72 MHz
- STM32CubeMX 负责时钟、GPIO、ADC、DMA、I2C、USART 和 TIM 初始化
- CMake + Ninja + GNU Arm Embedded Toolchain 构建
- ADC1 双通道扫描，DMA 循环搬运光照和土壤原始数据
- 基于 `HAL_GetTick()` 的非阻塞式裸机任务调度
- USART1 调试输出与变长中断接收
- USART2 CO₂ 模块接收框架
- DHT22 单总线驱动和 DWT 微秒延时
- SSD1306 OLED 驱动、字体和位图显示支持
- 用户业务代码与 CubeMX 生成代码分层管理

## 当前实现状态

### 已实现并通过构建验证

- [x] 72 MHz 系统时钟配置
- [x] SWD 调试接口保留
- [x] ADC1 双通道扫描和 DMA 循环采样
- [x] ADC 启动前校准及错误检查
- [x] 光照值查表换算
- [x] 土壤湿度等级换算
- [x] I2C1 OLED 初始化和固定图标显示
- [x] USART1 `printf` 重定向和变长中断接收
- [x] USART2 CO₂ 接收和协议解析代码
- [x] DHT22 驱动代码
- [x] PB12/PB13 外部中断入口
- [x] PC13 心跳灯非阻塞闪烁
- [x] Debug 配置下生成 ELF 和 MAP 文件

### 已有代码但尚未接入当前主循环

- [ ] 周期读取并显示 CO₂ 浓度
- [ ] 周期读取并显示 DHT22 温湿度
- [ ] OLED 实时显示全部传感器数据
- [ ] 按键页面切换和模式控制
- [ ] 蜂鸣器报警策略
- [ ] LED PWM 效果

### 后续规划

- [ ] ADC 数据滤波和统一传感器数据模型
- [ ] 修复 UART 接收缓冲区并发覆盖风险
- [ ] 自动/手动控制模式
- [ ] PC14 继电器控制
- [ ] PB8 电机 PWM 调速
- [ ] 控制阈值滞回和故障安全策略
- [ ] 串口 CLI 参数配置
- [ ] 参数写入内部 Flash
- [ ] LLCC68 LoRa 通信、CRC、ACK 和超时重发

## 当前运行逻辑

```text
上电复位
  │
  ├─ HAL、72 MHz 时钟和 CubeMX 外设初始化
  ├─ OLED 初始化并显示固定图标
  ├─ 启动 USART1 调试接收
  ├─ 启动 USART2 CO₂ 接收
  ├─ 初始化 DHT22 接口
  ├─ ADC1 校准
  └─ 启动 ADC1 + DMA 双通道循环采样
       │
       └─ 主循环
           ├─ 每 500 ms 翻转 PC13 心跳灯
           └─ 每 1000 ms 计算并打印光照、土壤数据
```

串口启动输出：

```text
System ready
```

周期输出示例：

```text
soil voltage: 1650 mV
soil resistance: 10000 ohm
Lux: 68, Soil: 4
```

## 硬件与引脚

### 当前 CubeMX 已配置引脚

| 功能 | MCU 引脚 | 外设/模式 | 说明 |
|---|---|---|---|
| 光照传感器 | PA0 | ADC1_IN0 | DMA 缓冲区 `adc_values[0]` |
| 土壤传感器 | PA1 | ADC1_IN1 | DMA 缓冲区 `adc_values[1]` |
| CO₂ TX | PA2 | USART2_TX | USART2，9600 8N1 |
| CO₂ RX | PA3 | USART2_RX | USART2，9600 8N1 |
| LED PWM | PA8 | TIM1_CH1 | 当前尚未在主循环启动 |
| 调试串口 TX | PA9 | USART1_TX | 115200 8N1，连接板载 CH340 |
| 调试串口 RX | PA10 | USART1_RX | 支持 Receive-to-Idle 中断接收 |
| SW1 | PB12 | EXTI12 | 下降沿触发、内部上拉 |
| SW2 | PB13 | EXTI13 | 下降沿触发、内部上拉 |
| LED3 | PB15 | GPIO Output | 低电平点亮 |
| OLED SCL | PB6 | I2C1_SCL | 当前配置 100 kHz |
| OLED SDA | PB7 | I2C1_SDA | SSD1306 驱动 |
| 蜂鸣器 PWM | PB9 | TIM4_CH4 | 需根据有源/无源蜂鸣器确认最终方案 |
| 板载 LED | PC13 | GPIO Output | 低电平点亮，当前用作心跳灯 |
| DHT22 | PC15 | GPIO | 单总线，运行时动态切换输入/输出 |
| SWDIO | PA13 | SWD | 调试接口，禁止占用 |
| SWCLK | PA14 | SWD | 调试接口，禁止占用 |

### 硬件注意事项

- **LSE 必须保持关闭**：PC14/PC15 计划用于继电器和 DHT22，启用 LSE 会占用这两个引脚。
- LED3 和 PC13 板载 LED 均为低电平点亮。
- CO₂ 模块接口供电和逻辑电平需要根据具体模块手册确认。
- DHT22 数据线需要可靠上拉；如果模块本身没有上拉电阻，应外接约 4.7 kΩ～10 kΩ 上拉。
- PA13/PA14 保留给 SWD，避免失去下载和调试接口。

## 软件结构

```text
Cube_MX_Code/
├── Core/
│   ├── Inc/                    # CubeMX 生成头文件
│   └── Src/                    # CubeMX 生成初始化和中断代码
├── Drivers/
│   ├── CMSIS/
│   └── STM32F1xx_HAL_Driver/
├── User/
│   ├── app/
│   │   ├── app_main.c          # 当前应用入口和非阻塞主循环
│   │   └── PWM.c               # PWM 示例接口
│   ├── bsp/
│   │   ├── CO2.c               # CO₂ 串口协议
│   │   ├── dht22.c             # DHT22 单总线驱动
│   │   ├── debug.c             # USART1 调试接收
│   │   ├── ISR_callback.c      # HAL 回调统一分发
│   │   ├── light.c             # 光照换算
│   │   └── soil.c              # 土壤湿度换算
│   └── oled/
│       ├── oled.c               # OLED 驱动
│       ├── oled_demo.c          # OLED 演示入口
│       ├── oledfont.h           # 字库
│       └── bmp.h                # 位图资源
├── cmake/
│   ├── gcc-arm-none-eabi.cmake # GNU Arm 工具链配置
│   └── stm32cubemx/            # CubeMX 生成的 CMake 配置
├── Cube_MX_Code.ioc            # CubeMX 工程配置
├── CMakeLists.txt              # 顶层构建配置和 User 层源文件
├── CMakePresets.json
└── STM32F103XX_FLASH.ld        # 链接脚本
```

## 构建环境

建议安装：

- STM32CubeMX
- STM32CubeCLT 或 GNU Arm Embedded Toolchain
- CMake
- Ninja
- VS Code
- STM32Cube VS Code Extension
- ST-Link 驱动和 GDB Server

确认以下命令可用：

```powershell
cmake --version
ninja --version
arm-none-eabi-gcc --version
arm-none-eabi-size --version
```

## 编译方法

在项目根目录执行：

```powershell
cmake --fresh --preset Debug
cmake --build --preset Debug
```

清理后完整重建：

```powershell
cmake --build --preset Debug --clean-first
```

构建成功后生成：

```text
build/Debug/Cube_MX_Code.elf
build/Debug/Cube_MX_Code.map
```

查看程序体积：

```powershell
arm-none-eabi-size build/Debug/Cube_MX_Code.elf
```

当前 Debug 构建结果：

```text
text     data    bss     total
30972    104     2536    33612 bytes
```

链接器统计：

```text
RAM:    2640 B / 20 KB    12.89%
FLASH: 31080 B / 128 KB   23.71%
```

## 下载与调试

1. 使用 ST-Link 连接 SWDIO、SWCLK、GND 和目标板参考电压。
2. 在 VS Code 中选择 `STM32Cube: Launch ST-Link GDB Server`。
3. 按 `F5` 构建、下载并进入调试。
4. 确认调试器实际加载：

```text
build/Debug/Cube_MX_Code.elf
```

5. 检查下载和校验日志，不要仅以“构建成功”判断固件已写入开发板。

可在 Live Watch 中观察：

```c
adc_values[0]
adc_values[1]
debug_rx_len
co2_rx_len
```

## CubeMX 开发约定

- 新增外设或修改引脚时，优先在 CubeMX 中修改 `.ioc` 并重新生成代码。
- 用户业务代码应放在 `User/` 或 CubeMX 的 `USER CODE BEGIN/END` 区域。
- 不直接修改 CubeMX 生成区中的初始化代码，以免下次生成时丢失。
- 重新生成后应重新检查时钟、初始电平、中断、引脚标签和 CMake 源文件列表。

## 开发路线

建议后续按以下顺序推进：

1. 建立统一的 `SensorData_t` 数据模型和传感器有效状态。
2. 增加 ADC 滤波，稳定光照和土壤显示。
3. 修复 USART1/USART2 接收缓冲区覆盖问题。
4. 将 CO₂ 和 DHT22 周期采集接入非阻塞主循环。
5. 将实时数据、故障状态和模式显示到 OLED。
6. 在 CubeMX 中配置 PC14 继电器、PB8 电机及最终蜂鸣器模式。
7. 实现带滞回的自动灌溉、通风和报警策略。
8. 加入串口 CLI、参数持久化和 LoRa 通信。
9. 完成长时间运行、传感器断线、错误帧和执行器安全状态测试。

## 当前验证范围

截至 **2026-09-17**：

- Debug 固件已通过 GNU Arm GCC + CMake/Ninja 构建；
- 构建结果为 0 Error、0 Warning；
- ELF 和 MAP 文件已生成并可被 STM32Cube Build Analyzer 解析；
- 代码具备 ADC、OLED、UART、DHT22 等模块基础；
- 完整传感器与执行器实物联调仍需继续验证。
