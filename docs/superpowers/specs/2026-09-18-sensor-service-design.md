# 传感器采集闭环设计

- 日期：2026-09-18
- 项目：STM32F103 智慧农业终端
- 状态：待用户审阅
- 范围：ADC 光照/土壤、DHT22、CO2、OLED/USART1 数据输出

## 1. 目标

在不修改当前 CubeMX 引脚和外设配置的前提下，建立独立的传感器服务层，将底层驱动数据统一整理为一份可供 OLED、调试串口和后续自动控制使用的状态快照。

本阶段完成：

1. ADC DMA 原始数据的周期读取和滤波；
2. 光照 Lux 和土壤湿度等级的统一更新；
3. DHT22 每 2 秒一次的周期读取；
4. CO2 接收帧的非阻塞处理、校验和解析；
5. 每个传感器的有效状态、错误码和更新时间；
6. OLED 与 USART1 输出统一传感器状态；
7. 构建、串口、OLED 和 Live Watch 验证。

本阶段不包含：

- 继电器、电机和蜂鸣器自动控制；
- LoRa/SPI1 配置和通信协议；
- Flash 参数保存；
- RTOS 引入；
- 修改 `.ioc` 或重新规划硬件引脚。

## 2. 当前硬件接口

| 数据 | 引脚/外设 | 当前状态 |
|---|---|---|
| 光照 ADC | PA0 / ADC1_IN0 | ADC1 + DMA 循环采样 |
| 土壤 ADC | PA1 / ADC1_IN1 | ADC1 + DMA 循环采样 |
| CO2 | PA2/PA3 / USART2 | 9600 8N1，Receive-to-Idle 接收框架 |
| DHT22 | PC15 | 单总线驱动，运行时切换输入/输出 |
| OLED | PB6/PB7 / I2C1 | SSD1306 驱动 |
| 调试串口 | PA9/PA10 / USART1 | 115200 8N1 |

LSE 保持关闭，PA13/PA14 保留给 SWD。

## 3. 软件结构

```text
User/bsp/
    light.c/.h       # 光照换算
    soil.c/.h        # 土壤换算
    dht22.c/.h       # DHT22 底层时序
    CO2.c/.h         # CO2 接收和协议底层
    debug.c/.h       # USART1 接收

User/service/
    sensor_service.c/.h  # 统一采集调度、滤波、状态和错误处理

User/app/
    app_main.c       # 初始化和非阻塞任务调度

User/oled/
    oled.c/.h         # OLED 底层显示
    oled_demo.c        # 当前显示入口，逐步改为传感器页面
```

`sensor_service` 对外只暴露统一状态和服务接口。OLED、调试输出和后续控制逻辑不直接依赖 `adc_values[]`、`co2_buffer[]` 或 DHT22 底层缓存。

## 4. 统一数据模型

建议在 `User/service/sensor_service.h` 中定义：

```c
typedef struct
{
    uint16_t light_adc;
    uint16_t soil_adc;
    uint16_t light_lux;
    uint8_t soil_level;

    float temperature;
    float humidity;
    uint16_t co2_ppm;

    uint8_t light_valid;
    uint8_t soil_valid;
    uint8_t dht22_valid;
    uint8_t co2_valid;

    uint8_t dht22_error;
    uint8_t co2_error;

    uint32_t adc_update_tick;
    uint32_t dht22_update_tick;
    uint32_t co2_update_tick;
} SensorData_t;

extern volatile SensorData_t g_sensor_data;
```

说明：

- `light_adc`、`soil_adc` 保存滤波后的 ADC 原始值，而不是单次 DMA 瞬时值；
- `light_lux`、`soil_level` 保存底层换算结果；
- DHT22/CO2 读取失败时保留上一次有效数据，但将对应 `valid` 置 0；
- `*_update_tick` 用于判断数据新鲜度；
- 错误码只描述最近一次失败原因，连续失败计数后续再扩展。

## 5. ADC 滤波设计

当前 `adc_values[0]` 和 `adc_values[1]` 由 DMA 持续更新。服务层每 100 ms 读取一次，并使用 16 次采样平均：

```text
DMA 原始值 → 16 次采样累计 → 平均值 → 光照/土壤换算
```

实现要求：

- `ADC_FILTER_SAMPLES = 16`；
- 光照和土壤分别维护采样累计与计数；
- 平均完成后更新 `g_sensor_data.light_adc` 和 `soil_adc`；
- 再调用现有 `GetLux()` / `GetSoilHumidity()` 或将换算函数改为接收指定 ADC 值；
- 不能在中断回调里执行浮点换算、串口输出或 OLED 刷新。

第一版允许使用简单平均，不引入复杂滤波库。

## 6. DHT22 设计

DHT22 每 2000 ms 调用一次：

```text
Sensor_ServiceTask(now)
    └─ 到达 DHT22 周期
        └─ DHT22_ReadData()
            ├─ 成功：更新温度、湿度、valid=1、更新时间
            └─ 失败：保留旧值、valid=0、记录错误码
```

要求：

- 不连续快速读取；
- 失败不能卡死主循环；
- OLED 和串口必须能区分旧值与当前有效值；
- 继续保持 PC15 的 DHT22 约束，LSE 不得启用。

若当前驱动使用 `0xFF` 同时表示合法字节和超时，需要在本阶段修正为独立状态返回值，避免合法 `0xFF` 数据被误判。

## 7. CO2 设计

当前协议假设为 6 字节帧：

```text
[0] = 0x2C
[1] = 浓度高字节
[2] = 浓度低字节
[3:4] = 保留
[5] = 前5字节累加和低8位
```

服务层不再阻塞等待完整帧。底层接收回调只负责：

1. 记录接收长度；
2. 将完整帧复制到业务缓冲区；
3. 设置 `frame_ready`；
4. 重新开启下一次接收。

主循环中的 CO2 处理负责：

1. 检查 `frame_ready`；
2. 检查帧长、帧头和校验和；
3. 成功后更新 `co2_ppm`、`co2_valid` 和更新时间；
4. 失败时保留旧值、清除本次完成标志并记录错误码。

必须避免中断重新接收时覆盖主循环正在解析的同一缓冲区。第一版使用“接收缓冲区 + 完成帧缓冲区”双缓冲，不引入环形队列。

CO2 长时间没有合法帧时，由新鲜度检查将 `co2_valid` 置 0；初始超时阈值为 3000 ms。

## 8. 主循环任务周期

```text
ADC 派生数据：100 ms
传感器有效性检查：500 ms
DHT22：2000 ms
CO2：收到帧立即处理，离线超时 3000 ms
OLED：500 ms
USART1 状态报告：1000 ms
```

任务必须使用 `HAL_GetTick()` 差值判断，不使用 1 秒级 `HAL_Delay()` 阻塞整个主循环。

## 9. OLED 和 USART1 输出

第一版统一输出以下数据：

```text
T: 25.6 C / H: 63.2 %
CO2: 620 ppm
Light: 68 lux
Soil: 3
Status: DHT=OK CO2=OK ADC=OK
```

传感器无效时使用明确状态：

```text
DHT: ERR(2)
CO2: OFFLINE
```

旧值可以保留在结构体中，但显示层必须根据 `valid` 决定显示数值还是错误状态。

OLED 刷新放在主循环上下文，不在中断中调用 I2C。

## 10. 错误处理

| 场景 | 数据处理 | 状态处理 |
|---|---|---|
| ADC 正常 | 更新滤波值和换算值 | `light_valid/soil_valid=1` |
| ADC 数据超范围 | 保留上次值 | 对应 valid=0 |
| DHT22 成功 | 更新温湿度 | `dht22_valid=1` |
| DHT22 超时/校验错 | 保留旧值 | `dht22_valid=0`，记录错误码 |
| CO2 合法帧 | 更新 ppm | `co2_valid=1` |
| CO2 帧头/校验错 | 保留旧值 | `co2_valid=0`，记录错误码 |
| CO2 超过3秒无帧 | 保留旧值 | `co2_valid=0`，标记离线 |

错误处理不能调用 `Error_Handler()`，除非是 ADC 启动等系统初始化失败；运行期传感器故障只影响对应传感器状态。

## 11. CMake 变更

顶层 `CMakeLists.txt` 增加：

```text
User/service/sensor_service.c
User/service
```

不修改 `cmake/stm32cubemx/CMakeLists.txt`，避免 CubeMX 重新生成时覆盖用户配置。

## 12. 验收标准

### 构建

- CMake Debug 构建成功；
- 0 Error、0 Warning；
- 生成 `build/Debug/Cube_MX_Code.elf` 和 `.map`。

### 串口

- 上电输出 `System ready`；
- 每秒输出统一传感器状态；
- DHT22/CO2 失败显示错误或离线状态；
- 传感器异常不能阻塞主循环。

### 实物

1. 遮挡或照射光敏传感器，滤波后的 ADC 和 Lux 稳定变化；
2. 改变土壤传感器输入，土壤 ADC 和等级变化；
3. DHT22 每约 2 秒更新一次；
4. CO2 模块收到合法帧后更新 ppm；
5. 拔掉 DHT22，心跳灯、ADC 和串口仍继续运行；
6. 拔掉 CO2 模块，系统在约 3 秒后标记离线；
7. OLED 与 USART1 显示的有效状态一致；
8. Live Watch 可观察 `g_sensor_data` 字段。

## 13. 风险和边界

- 当前 CO2 协议格式来自现有驱动假设，实物验证时需用串口原始帧确认；
- DHT22 仍会有毫秒级忙等，第一版接受该开销；
- OLED 的固定地址和 I2C 错误处理不在本阶段重点范围内；
- 本阶段不宣称继电器、电机、蜂鸣器或 LoRa 已完成实物联调。
