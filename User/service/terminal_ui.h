#ifndef TERMINAL_UI_H
#define TERMINAL_UI_H
#include <stdint.h>
void Terminal_UIInit(uint32_t now);
void Terminal_UITask(uint32_t now);
void Terminal_UIRefresh(uint32_t now);
uint8_t Terminal_UIGetPage(void);
#endif
