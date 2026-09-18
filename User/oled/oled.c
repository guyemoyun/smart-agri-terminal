/**
  ******************************************************************************
  * @file    oled.c
  * @brief   SSD1306 OLED 驱动实现（帧缓冲渲染 + I2C 水平寻址整帧刷新）
  * @note    数据流：绘图 API → s_gram[8][128] 本地帧缓冲 → OLED_Refresh()
  *          → SSD1306 水平寻址模式一次写满 1024 字节 GDDRAM。
  ******************************************************************************
  */
#include "oled.h"
#include "oledfont.h"
#include "main.h"
#include "i2c.h"
#include <string.h>

/* SSD1306 I2C 从机写地址：7位地址 0x3C 左移1位 = 0x78 */
#define OLED_I2C_ADDR    0x78
/* I2C 传输超时（ms）：100kHz 下整帧约92ms，给足裕量；400kHz 下约23ms */
#define OLED_I2C_TIMEOUT 200

/* 本地帧缓冲：[页 0~7][列 0~127]，每页 8 行，bit0=页内最上方像素，bit7=最下方 */
static uint8_t s_gram[OLED_HEIGHT / 8][OLED_WIDTH];

/* ============================ 底层 I2C ============================ */
/**
  * @brief  向 SSD1306 写一条命令（控制字节 0x00 = 后续为命令）
  * @param  cmd 命令字节
  */
static void OLED_WR_Cmd(uint8_t cmd)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_I2C_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT,
                      &cmd, 1, OLED_I2C_TIMEOUT);
}

/**
  * @brief  整帧刷新：把本地帧缓冲一次性写入 GDDRAM
  * @note   水平寻址模式（SSD1306 命令 20h A1A0=00b）：
  *           21h 设列范围 0~127，22h 设页范围 0~7，之后控制字节 0x40，
  *           连续写 1024 字节，硬件到达页边界自动换页。
  *         耗时：400kHz ≈23ms / 100kHz ≈92ms（一次事务，无逐字节寻址开销）。
  */
void OLED_Refresh(void)
{
    OLED_WR_Cmd(0x20); OLED_WR_Cmd(0x00);      // 内存地址模式 = 水平寻址
    OLED_WR_Cmd(0x21); OLED_WR_Cmd(0x00); OLED_WR_Cmd(0x7F); // 列起始0 / 结束127
    OLED_WR_Cmd(0x22); OLED_WR_Cmd(0x00); OLED_WR_Cmd(0x07); // 页起始0 / 结束7

    HAL_I2C_Mem_Write(&hi2c1, OLED_I2C_ADDR, 0x40, I2C_MEMADD_SIZE_8BIT,
                      &s_gram[0][0], OLED_WIDTH * (OLED_HEIGHT / 8),
                      OLED_I2C_TIMEOUT);        // 0x40 = 后续为显存数据
}

/**
  * @brief  刷新指定页范围，每页对应 8 个垂直像素
  * @param  start_page 起始页，范围 0~7
  * @param  end_page   结束页，范围 0~7
  */
void OLED_RefreshPages(uint8_t start_page, uint8_t end_page)
{
    uint16_t count;

    if (start_page > end_page)
    {
        uint8_t temp = start_page;
        start_page = end_page;
        end_page = temp;
    }
    if (start_page > 7U) start_page = 7U;
    if (end_page > 7U) end_page = 7U;

    OLED_WR_Cmd(0x20); OLED_WR_Cmd(0x00);
    OLED_WR_Cmd(0x21); OLED_WR_Cmd(0x00); OLED_WR_Cmd(0x7F);
    OLED_WR_Cmd(0x22); OLED_WR_Cmd(start_page); OLED_WR_Cmd(end_page);

    count = (uint16_t)(end_page - start_page + 1U) * OLED_WIDTH;
    HAL_I2C_Mem_Write(&hi2c1, OLED_I2C_ADDR, 0x40, I2C_MEMADD_SIZE_8BIT,
                      &s_gram[start_page][0], count, OLED_I2C_TIMEOUT);
}
/* ============================ 像素/位图渲染 ============================ */
/**
  * @brief  设置帧缓冲中单个像素（坐标越界自动忽略）
  * @param  x  列 0~127
  * @param  y  行 0~63
  * @param  on 1=点亮 0=熄灭
  */
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    if (on)
        s_gram[y >> 3][x] |=  (uint8_t)(1U << (y & 7));
    else
        s_gram[y >> 3][x] &= (uint8_t)~(1U << (y & 7));
}

/**
  * @brief  把"按列扫描"格式的位图块叠加进帧缓冲（内部通用渲染函数）
  * @note   数据排列：每 8 行一个水平条带(strip)，条带内每列 1 字节，
  *         bit0=条带最上行。ASCII 字库/中文点阵/BMP 全部是该格式，
  *         因此一个函数即可渲染所有图形。逐像素写入 + SetPixel 自动裁剪，
  *         图形贴边、超出屏幕都安全。
  * @param  x0 位图左上角 X（像素，任意值，不要求 8 对齐）
  * @param  y0 位图左上角 Y（像素，任意值）
  * @param  data 位图数据
  * @param  w 位图宽（列）
  * @param  h 位图高（行，必须是 8 的整数倍）
  */
static void OLED_Blit(int x0, int y0, const uint8_t *data, uint16_t w, uint16_t h)
{
    uint16_t strip, col, bit;
    for (strip = 0; strip < h / 8; strip++)
    {
        for (col = 0; col < w; col++)
        {
            uint8_t byte = data[strip * w + col];
            for (bit = 0; bit < 8; bit++)
            {
                OLED_SetPixel((uint8_t)(x0 + col), (uint8_t)(y0 + strip * 8 + bit),
                              (byte >> bit) & 1);
            }
        }
    }
}

/**
  * @brief  矩形区域填充
  * @param  x0,y0 左上角 / x1,y1 右下角（自动交换顺序、自动限幅）
  * @param  fill_data 非0=区域点亮，0=区域熄灭
  */
void OLED_Fill_Area(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t fill_data)
{
    uint8_t x, y;
    if (x0 > x1) { uint8_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { uint8_t t = y0; y0 = y1; y1 = t; }
    if (x1 >= OLED_WIDTH)  x1 = OLED_WIDTH - 1;
    if (y1 >= OLED_HEIGHT) y1 = OLED_HEIGHT - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            OLED_SetPixel(x, y, fill_data ? 1 : 0);
}

/* ============================ 字符/字符串 ============================ */
/**
  * @brief  显示单个 ASCII 字符（像素级任意定位）
  * @param  x,y 字符左上角像素坐标
  * @param  chr 字符 ASCII 值
  * @param  font_size 字号：FONT_SIZE_16 / FONT_SIZE_24
  * @retval 字符宽度（像素）；完全超出右边界返回 0
  */
uint8_t OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t font_size)
{
    if (x >= OLED_WIDTH) return 0;

    switch (font_size)
    {
#if FONT_SIZE_8
    case FONT_SIZE_8:
        if (chr >= ' ' && chr <= '~')
            OLED_Blit(x, y, F6x8[chr - ' '], 6, 8);
        return 6;
#endif
#if FONT_SIZE_16
    case FONT_SIZE_16:
        if (chr >= ' ' && chr <= '~')
            OLED_Blit(x, y, F8X16[chr - ' '], 8, 16);
        return 8;
#endif
#if FONT_SIZE_24
    case FONT_SIZE_24:
        if (chr >= ' ' && chr <= '~')
            OLED_Blit(x, y, F12X24[chr - ' '], 12, 24);
        return 12;
#endif
    default:
        return 0;
    }
}

/**
  * @brief  显示无符号整数（前导 0 以空格代替，与常见 OLED 例程行为一致）
  * @param  x,y 左上角像素坐标
  * @param  num 数值
  * @param  len 数字位数
  * @param  font_size 字号
  */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t font_size)
{
    uint8_t char_width = (font_size == FONT_SIZE_16) ? 8 : 12;
    uint8_t t, started = 0;
    uint32_t power = 1;

    for (t = 1; t < len; t++) power *= 10;       // 最高位权值

    for (t = 0; t < len; t++)
    {
        uint8_t digit = (uint8_t)((num / power) % 10);
        power /= 10;
        if (digit != 0 || t == len - 1) started = 1; // 最高有效位之后不再显示空格
        if (started)
            OLED_ShowChar((uint8_t)(x + char_width * t), y, (uint8_t)('0' + digit), font_size);
        else
            OLED_ShowChar((uint8_t)(x + char_width * t), y, ' ', font_size);
    }
}

/* ---------- UTF-8 解析（中文显示用） ---------- */
/**
  * @brief  获取一个 UTF-8 字符的字节长度（1~4），非法序列按 1 字节处理
  */
static int GetUtf8CharLength(const unsigned char *target)
{
    if (target[0] <= 0x7F) return 1;
    if ((target[0] >= 0xC2 && target[0] <= 0xDF) && (target[1] & 0xC0) == 0x80) return 2;
    if ((target[0] >= 0xE0 && target[0] <= 0xEF) && (target[1] & 0xC0) == 0x80
        && (target[2] & 0xC0) == 0x80) return 3;
    if ((target[0] >= 0xF0 && target[0] <= 0xF7) && (target[1] & 0xC0) == 0x80
        && (target[2] & 0xC0) == 0x80 && (target[3] & 0xC0) == 0x80) return 4;
    return 1;
}

/**
  * @brief  按 UTF-8 字节内容在中文索引表中查找点阵序号
  * @param  target 字符串当前位置
  * @param  font_size 字号 16/24
  * @retval 点阵序号；未找到返回 -1
  */
static int FindFontIndex(const char *target, uint8_t font_size)
{
    int i, charLen = GetUtf8CharLength((const unsigned char *)target);
#if FONT_SIZE_16
    if (font_size == FONT_SIZE_16)
    {
        for (i = 0; i < (int)(sizeof(g_font_dot_matrix_16_index) / sizeof(g_font_dot_matrix_16_index[0])); i++)
            if (strncmp(g_font_dot_matrix_16_index[i], target, (size_t)charLen) == 0)
                return i;
    }
#endif
#if FONT_SIZE_24
    if (font_size == FONT_SIZE_24)
    {
        for (i = 0; i < (int)(sizeof(g_font_dot_matrix_24_index) / sizeof(g_font_dot_matrix_24_index[0])); i++)
            if (strncmp(g_font_dot_matrix_24_index[i], target, (size_t)charLen) == 0)
                return i;
    }
#endif
    return -1;
}

/**
  * @brief  显示字符串（ASCII 与 UTF-8 中文混合，自动换行前不处理，超出宽度截断）
  * @note   ASCII：按字库宽度推进；中文：查点阵表后整字渲染，字宽=字号；
  *         字库中不存在的字符：显示 16x16 错误方框图标 Ferror。
  * @param  x,y 字符串左上角像素坐标
  * @param  chr 以 '\0' 结尾的字符串
  * @param  font_size 字号（中文仅支持 16/24）
  */
void OLED_ShowString(uint8_t x, uint8_t y, const char *chr, uint8_t font_size)
{
    uint8_t cur_x = x;
    uint8_t ascii_width = (font_size == FONT_SIZE_16) ? 8 : 12;
    unsigned char *p = (unsigned char *)chr;

    if (chr == NULL || font_size == 0) return;

    while (*p != '\0')
    {
        int utf8_len = GetUtf8CharLength(p);

        if (utf8_len == 1)                          // ASCII 字符
        {
            OLED_ShowChar(cur_x, y, *p, font_size);
            cur_x = (uint8_t)(cur_x + ascii_width);
            p += 1;
        }
        else                                       // 多字节（中文）
        {
            int idx = FindFontIndex((const char *)p, font_size);
            if (idx >= 0)
            {
#if FONT_SIZE_16
                if (font_size == FONT_SIZE_16)
                    OLED_Blit(cur_x, y, (const uint8_t *)g_font_dot_matrix_16[idx], 16, 16);
#endif
#if FONT_SIZE_24
                if (font_size == FONT_SIZE_24)
                    OLED_Blit(cur_x, y, (const uint8_t *)g_font_dot_matrix_24[idx], 24, 24);
#endif
                cur_x = (uint8_t)(cur_x + font_size);
            }
            else
            {
                OLED_Blit(cur_x, y, Ferror, 16, 16);   // 字库缺失：错误方框
                cur_x = (uint8_t)(cur_x + 16);
            }
            p += utf8_len;
        }

        if (cur_x >= OLED_WIDTH) break;               // 到右边界停止
    }
}

/* ============================ BMP ============================ */
/**
  * @brief  绘制位图（按列扫描格式，任意像素位置，越界自动裁剪）
  * @param  x0,y0 位图左上角像素坐标（不要求 8 对齐）
  * @param  bmp_width,bmp_height 位图宽高（高应为 8 的整数倍）
  * @param  BMP 位图数据
  */
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint16_t bmp_width, uint16_t bmp_height,
                  const uint8_t BMP[])
{
    OLED_Blit(x0, y0, BMP, bmp_width, bmp_height);
}

/* ============================ 显示开关 ============================ */
/**
  * @brief  开启显示（8D 14h 电荷泵使能 + AFh 显示开）
  */
void OLED_Display_On(void)
{
    OLED_WR_Cmd(0x8D); OLED_WR_Cmd(0x14);
    OLED_WR_Cmd(0xAF);
}

/**
  * @brief  关闭显示并进入休眠（电荷泵关闭，功耗最低）
  */
void OLED_Display_Off(void)
{
    OLED_WR_Cmd(0x8D); OLED_WR_Cmd(0x10);
    OLED_WR_Cmd(0xAE);
}

/**
  * @brief  清空帧缓冲
  */
void OLED_Clear(void)
{
    memset(s_gram, 0, sizeof(s_gram));
}

/* ============================ 初始化 ============================ */
/**
  * @brief  SSD1306 初始化（命令序列对应 SSD1306 手册第 10 节典型序列）
  * @note   关键项：复用率 64(A8 3F)、段重映射 A1、COM 扫描反向 C8、
  *         COM 引脚配置 DA 12、电荷泵 8D 14（0.91寸模块必须开启，否则屏幕不亮）。
  *         完成后清帧缓冲并整帧刷新一次，保证上电为黑屏。
  */
void OLED_Init(void)
{
    HAL_Delay(200);                 // 等待上电电源稳定

    OLED_WR_Cmd(0xAE);              // 显示关
    OLED_WR_Cmd(0x00);              // 列地址低4位
    OLED_WR_Cmd(0x10);              // 列地址高4位
    OLED_WR_Cmd(0x40);              // 起始行 0
    OLED_WR_Cmd(0xB0);              // 页地址
    OLED_WR_Cmd(0x81); OLED_WR_Cmd(0xFF); // 对比度 255
    OLED_WR_Cmd(0xA1);              // 段重映射（左右镜像，配合模块安装方向）
    OLED_WR_Cmd(0xA6);              // 正常显示（非反色）
    OLED_WR_Cmd(0xA8); OLED_WR_Cmd(0x3F); // 多路复用 1/64
    OLED_WR_Cmd(0xC8);              // COM 扫描方向：反向
    OLED_WR_Cmd(0xD3); OLED_WR_Cmd(0x00); // 显示偏移 0
    OLED_WR_Cmd(0xD5); OLED_WR_Cmd(0x80); // 时钟分频/振荡 默认
    OLED_WR_Cmd(0xD8); OLED_WR_Cmd(0x05);
    OLED_WR_Cmd(0xD9); OLED_WR_Cmd(0xF1); // 预充电周期
    OLED_WR_Cmd(0xDA); OLED_WR_Cmd(0x12); // COM 引脚配置
    OLED_WR_Cmd(0xDB); OLED_WR_Cmd(0x30); // VCOMH
    OLED_WR_Cmd(0x8D); OLED_WR_Cmd(0x14); // 电荷泵使能
    OLED_WR_Cmd(0xAF);              // 显示开

    OLED_Clear();
    OLED_Refresh();                 // 上电先刷一次黑屏
}
