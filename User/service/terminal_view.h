#ifndef TERMINAL_VIEW_H
#define TERMINAL_VIEW_H
#include "sensor_service.h"
#include "control_service.h"
#include "lora_service.h"
#define TERMINAL_PAGE_COUNT 5U
#define TERMINAL_PAGE_ENV   0U
#define TERMINAL_PAGE_FAN   1U
#define TERMINAL_PAGE_PUMP  2U
#define TERMINAL_PAGE_LINK  3U
#define TERMINAL_PAGE_FAULT 4U
#define TERMINAL_ROW_WIDTH 16U

typedef struct { char row[4][TERMINAL_ROW_WIDTH + 1U]; } TerminalRows_t;
void TerminalView_Format(uint8_t page, const SensorData_t *sensor,
    const ControlSnapshot_t *control, const LoraStatus_t *link, uint32_t now, TerminalRows_t *rows);
#endif
