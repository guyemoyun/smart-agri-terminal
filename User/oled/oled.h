#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define OLED_CMD 0	// 写命令
#define OLED_DATA 1 // 写数据

// OLED控制用函数
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Clear(void);
void OLED_Fill_Area(uint8_t x0, uint8_t y0, uint8_t x1,
					uint8_t y1, uint8_t fill_data);
uint8_t OLED_Show_Font_error(uint8_t x, uint8_t y);
uint8_t OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t font_size);
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t font_size);
void OLED_ShowCHinese(uint8_t x, uint8_t y, uint8_t no, uint8_t font_size);
void OLED_ShowString(uint8_t x, uint8_t y, const char *chr, uint8_t font_size);
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint16_t bmp_width, uint16_t bmp_height,
				  const uint8_t BMP[]);
void OLED_Init(void);
#endif
