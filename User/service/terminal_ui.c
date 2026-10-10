#include "terminal_ui.h"
#include "terminal_view.h"
#include "button_service.h"
#include "oled.h"
#include <string.h>

static uint8_t page, force_refresh;
static uint32_t last_refresh;
static TerminalRows_t previous;

void Terminal_UIInit(uint32_t now)
{
    page = 0U;
    force_refresh = 1U;
    last_refresh = now - 200U;
    memset(&previous, 0, sizeof(previous));
    Button_ServiceInit(now);
}

uint8_t Terminal_UIGetPage(void) { return page; }

void Terminal_UIRefresh(uint32_t now)
{
    if (!force_refresh && (now - last_refresh) < 200U) return;
    last_refresh = now;
    SensorData_t sensor;
    ControlSnapshot_t control;
    LoraStatus_t link;
    TerminalRows_t rows;
    Sensor_GetSnapshot(&sensor);
    Control_GetSnapshot(&control);
    Lora_GetStatus(&link);
    TerminalView_Format(page, &sensor, &control, &link, now, &rows);
    for (uint8_t row = 0U; row < 4U; ++row)
    {
        if (force_refresh || strcmp(rows.row[row], previous.row[row]) != 0)
        {
            const uint8_t y = (uint8_t)(row * 16U);
            OLED_Fill_Area(0U, y, 127U, (uint8_t)(y + 15U), 0U);
            OLED_ShowString(0U, y, rows.row[row], 16U);
            OLED_RefreshPages((uint8_t)(row * 2U), (uint8_t)(row * 2U + 1U));
        }
    }
    previous = rows;
    force_refresh = 0U;
}

void Terminal_UITask(uint32_t now)
{
    Button_ServiceTask(now);
    const uint8_t events = Button_TakeEvents();
    ControlSnapshot_t control;
    Control_GetSnapshot(&control);
    /* 同时出现多个事件时，紧急停止最高优先，不能又被模式切换解锁。 */
    if (events & KEY_SW2_LONG)
    {
        Control_EmergencyStop(now);
        page = TERMINAL_PAGE_FAULT;
    }
    else if (events & KEY_SW1_LONG)
    {
        (void)Control_SetMode(control.policy.mode == CONTROL_MODE_MANUAL ? CONTROL_MODE_AUTO : CONTROL_MODE_MANUAL, now);
    }
    else
    {
        if (events & KEY_SW1_SHORT) page = (uint8_t)((page + 1U) % TERMINAL_PAGE_COUNT);
        if (events & KEY_SW2_SHORT)
        {
            if (page == TERMINAL_PAGE_FAN)
                (void)Control_SetFan((uint8_t)((control.actuator.fan_level + 1U) % 5U), now);
            else if (page == TERMINAL_PAGE_PUMP)
                (void)Control_SetPump((uint8_t)(control.actuator.relay_on == 0U), now);
            else Control_ToggleMute(now);
        }
    }
    if (events != 0U) force_refresh = 1U;
    Terminal_UIRefresh(now);
}
