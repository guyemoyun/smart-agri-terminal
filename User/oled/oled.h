/**
  ******************************************************************************
  * @file    oled.h
  * @brief   SSD1306 128x64 OLED 驱动对外接口（I2C1：PB6-SCL / PB7-SDA）
  * @note    架构：本地帧缓冲（Framebuffer）+ 水平寻址整帧刷新
  *            ① 所有绘图操作只改 RAM 中的帧缓冲，屏幕不立即变化；
  *            ② 调用 OLED_Refresh() 后整帧经一次 I2C 事务写入 GDDRAM。
  *          优点：任意像素级坐标、无画面跳动闪烁、刷新速度快（400kHz ≈23ms）。
  ******************************************************************************
  */
#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

#define OLED_WIDTH  128
#define OLED_HEIGHT 64

#define OLED_CMD  0   // 写命令
#define OLED_DATA 1   // 写数据

/* ---------- 基础控制 ---------- */
void OLED_Init(void);          // 初始化（含 200ms 上电等待 + 清屏）
void OLED_Clear(void);         // 清空帧缓冲（需再调 Refresh 才会清屏）
void OLED_Refresh(void);       // 整帧刷新：帧缓冲 → I2C → GDDRAM
void OLED_RefreshPages(uint8_t start_page, uint8_t end_page); // 刷新指定页范围
void OLED_Display_On(void);    // 开显示
void OLED_Display_Off(void);   // 关显示（进入休眠）

/* ---------- 绘图（全部像素级坐标，自动裁剪边界） ---------- */
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on);                       // 画/擦一个点 on:1亮 0灭
void OLED_Fill_Area(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                    uint8_t fill_data);                                     // 矩形区域填充（fill_data 非0=点亮）
uint8_t OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t font_size);// 单字符 @return 字符宽度，0=完全超出屏幕
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len,
                  uint8_t font_size);                                      // 数字（前导0显示为空格）
void OLED_ShowString(uint8_t x, uint8_t y, const char *chr,
                     uint8_t font_size);                                   // 字符串（ASCII/UTF-8 中文混合）
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint16_t bmp_width,
                  uint16_t bmp_height, const uint8_t BMP[]);                // 位图（按列扫描格式，支持任意位置）

#endif
