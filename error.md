# 错误记录与解决方案

- 项目：STM32F103 智慧农业终端
- 记录日期：2026-09-18
- 当前工程：`D:\test\STM32_Cmake\Cube_MX_Code`
- 参考基准工程：`D:\test\Cube_MX_Code`（Keil5/MDK-ARM）

## 1. DHT22 无法读取问题

### 现象

CMake/GCC 版本烧录后：

```text
DHT22: ERR(2)
```

OLED 温湿度区域显示无效占位值，其他传感器可以正常显示。

Keil5/MDK-ARM 版本在同一块开发板、同一组传感器上能够正常显示温度和湿度，因此硬件接线、DHT22 传感器和 PC15 基本可判定正常。

### 错误含义

`ERR(2)` 表示 DHT22 已经进入数据接收阶段，但某一位数据没有在超时时间内出现预期的电平变化。

DHT22 每一位的时序只有几十微秒，读取过程容易受以下因素影响：

- SysTick 中断；
- USART1/USART2 中断；
- DMA 中断；
- GCC Debug `-O0` 造成的函数和 GPIO 轮询开销；
- 采样点距离 0/1 脉冲边界过近；
- 使用 `0xFF` 同时表示合法字节和超时错误。

### 对比定位

Keil 成功工程和 CMake 工程最初并不是同一份业务代码：

- Keil 使用已经完成实物联调的 DHT22 驱动；
- CMake 版本曾使用重构中的 DHT22 驱动；
- CMake Debug 默认使用 GCC `-O0`；
- 两个工程的 OLED、I2C 和应用主循环也存在差异。

因此问题最终没有归因于硬件，而是按“Keil 实物基准迁移到 CMake”的路线处理。

## 2. 采取的修复措施

### 2.1 恢复 Keil 已验证驱动

将 Keil 工程中的以下文件迁移到 CMake 工程：

```text
User/bsp/dht22.c
User/bsp/dht22.h
User/bsp/CO2.c
User/bsp/CO2.h
User/oled/oled.c
User/oled/oled.h
User/oled/oledfont.h
```

其中 DHT22 和 CO2 文件通过 SHA256 对比确认与 Keil 成功版本一致。

### 2.2 优化 DHT22 微秒时序

在 CMake 中对 DHT22 源文件单独启用 `-O2`：

```cmake
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/User/bsp/dht22.c
    PROPERTIES COMPILE_OPTIONS "-O2"
)
```

同时，`delay_us()` 使用 GCC 函数级优化属性：

```c
#if defined(__GNUC__)
__attribute__((optimize("O2")))
#endif
void delay_us(uint32_t us)
```

### 2.3 调整首次读取时间

DHT22 上电后不立即读取，改为等待约 2 秒后再进行第一次读取，满足传感器稳定和读取间隔要求。

### 2.4 关键采样区间临时关闭中断

DHT22 启动低电平 1ms 期间保持中断开启，以保证 `HAL_Delay()` 正常工作；释放数据线后：

```c
保存 PRIMASK
关闭中断
读取传感器应答和40位数据
恢复原中断状态
```

这样可以避免 SysTick、USART 和 DMA 中断打断约 4ms 的微秒级采样窗口。

### 2.5 增大时序裕量

- 响应和数据位等待超时计数增加；
- 数据位采样点从约 30us 调整到约 40us；
- 0-bit 的高电平约 26~28us，1-bit 的高电平约 70us，40us 采样点具有更大区分裕量。

### 2.6 消除 `0xFF` 歧义

读取字节改为通过输出参数返回数据，通过函数返回值表示超时状态：

```c
static uint8_t DHT22_ReadByte(uint8_t *value);
```

不再把 `0xFF` 同时作为合法数据和错误码。

## 3. 验证结果

### 构建验证

最新 CMake Debug 构建成功：

```text
[45/45] Linking C executable Cube_MX_Code.elf
0 Error
0 Warning
```

最新资源占用约为：

```text
RAM:    3816 B / 20 KB    18.63%
FLASH: 37964 B / 128 KB   28.96%
```

### 实物验证

用户已确认 DHT22 成功获取温湿度数据。此前 Keil5/MDK-ARM 版本也已经在同一块板上成功显示：

- 温度；
- 湿度；
- CO2；
- 光照；
- 土壤湿度。

由此确认传感器硬件链路可用，CMake 版本的主要问题属于驱动时序、编译优化和工程版本差异。

## 4. 经验结论

1. 嵌入式工程不能只比较源代码目录名称，必须比较实际参与构建的文件、编译参数和最终 ELF。
2. Keil 成功不代表 CMake 已经使用同一份代码；本项目两个工程最初存在多个不同版本的 `app_main.c`、OLED、DHT22、CO2 和 `.ioc`。
3. DHT22 这类微秒级协议必须同时关注：主频、优化等级、中断干扰、采样点和 GPIO 上拉。
4. 调试阶段应优先保留已验证的 Keil 硬件基准，再逐步迁移到 CMake 的服务层架构。
5. CMake Debug 构建中的 `-O0` 不适合直接验证所有微秒级时序驱动；应对时序源文件做局部优化，或提供专用硬件验证配置。

## 5. 当前仍需关注的问题

- CMake 当前 I2C1 仍可能是 100kHz，Keil 成功工程使用 400kHz；需要在 CubeMX 中手动确认 Fast Mode 配置并重新生成；
- `HAL_GPIO_EXTI_Callback()` 中按键消抖仍在中断上下文执行，后续应改为中断置标志、主循环消抖；
- CO2 当前先恢复了 Keil 的阻塞式读取，后续再做非阻塞双缓冲重构；
- DHT22 临界区临时关闭中断属于裸机时序折中，后续可考虑使用定时器输入捕获或更专用的时序采样方案；
- 最终提交前需要确认当前 CMake ELF 已由 ST-Link 实际烧录并验证，而不是只确认构建成功。
