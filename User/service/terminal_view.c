#include "terminal_view.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *ModeName(ControlMode_t mode)
{
    switch (mode)
    {
        case CONTROL_MODE_MANUAL: return "MANUAL";
        case CONTROL_MODE_AUTO: return "AUTO";
        case CONTROL_MODE_SAFE: return "SAFE";
        default: return "UNKNOWN";
    }
}

static void Fixed1(float value, char *out, size_t capacity)
{
    if (capacity < 6U) return;
    if (!isfinite(value) || value < -40.0f || value > 100.0f)
    {
        memcpy(out, "--.-", 5U);
        return;
    }
    const int scaled = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    const unsigned int absolute = (unsigned int)(scaled < 0 ? -scaled : scaled);
    const unsigned int whole = absolute / 10U;
    size_t index = 0U;
    /* 最大为正数 100.0 或负数 -40.0，连同结尾符都在 6 字节之内。 */
    if (scaled < 0) out[index++] = '-';
    if (whole >= 100U) out[index++] = '1';
    if (whole >= 10U) out[index++] = (char)('0' + (whole / 10U) % 10U);
    out[index++] = (char)('0' + whole % 10U);
    out[index++] = '.';
    out[index++] = (char)('0' + absolute % 10U);
    out[index] = '\0';
}

void TerminalView_Format(uint8_t page, const SensorData_t *sensor,
    const ControlSnapshot_t *control, const LoraStatus_t *link, uint32_t now, TerminalRows_t *rows)
{
    const size_t width = TERMINAL_ROW_WIDTH + 1U;
    memset(rows, 0, sizeof(*rows));
    if (page == TERMINAL_PAGE_ENV)
    {
        char temperature[6] = "--.-", humidity[6] = "--.-";
        if (sensor->dht22_valid && (now - sensor->dht22_update_tick) <= 6000U &&
            isfinite(sensor->temperature) && isfinite(sensor->humidity) &&
            sensor->temperature >= -40.0f && sensor->temperature <= 85.0f &&
            sensor->humidity >= 0.0f && sensor->humidity <= 100.0f)
        {
            Fixed1(sensor->temperature, temperature, sizeof(temperature));
            Fixed1(sensor->humidity, humidity, sizeof(humidity));
        }
        (void)snprintf(rows->row[0], width, "T:%.5s H:%.5s", temperature, humidity);
        if (sensor->co2_valid && (now - sensor->co2_update_tick) <= 3000U)
            (void)snprintf(rows->row[1], width, "CO2:%5u ppm", (unsigned int)sensor->co2_ppm);
        else (void)snprintf(rows->row[1], width, "CO2: -- ppm");
        char light[6] = "--", soil[6] = "--";
        if (sensor->light_valid && (now - sensor->adc_update_tick) <= 3200U)
            (void)snprintf(light, sizeof(light), "%u", (unsigned int)sensor->light_lux);
        if (sensor->soil_valid && (now - sensor->adc_update_tick) <= 3200U)
            (void)snprintf(soil, sizeof(soil), "%u", (unsigned int)sensor->soil_level);
        (void)snprintf(rows->row[2], width, "L~:%s S:%s", light, soil);
        (void)snprintf(rows->row[3], width, "M:%.6s P:%u F:%c", ModeName(control->policy.mode),
            (unsigned int)(control->actuator.relay_on != 0U),
            (int)('0' + (control->actuator.fan_level <= 4U ? control->actuator.fan_level : 0U)));
    }
    else if (page == TERMINAL_PAGE_FAN)
    {
        (void)snprintf(rows->row[0], width, "FAN PWM 2/5");
        (void)snprintf(rows->row[1], width, "Mode:%.6s", ModeName(control->policy.mode));
        (void)snprintf(rows->row[2], width, "L:%u PWM:%3u%%", (unsigned int)(control->actuator.fan_level <= 4U ? control->actuator.fan_level : 0U),
            (unsigned int)(control->actuator.fan_duty_permille <= 1000U ? control->actuator.fan_duty_permille / 10U : 0U));
        (void)snprintf(rows->row[3], width, "SW2: NEXT LEVEL");
    }
    else if (page == TERMINAL_PAGE_PUMP)
    {
        (void)snprintf(rows->row[0], width, "PUMP 3/5");
        (void)snprintf(rows->row[1], width, "M:%.6s Out:%u", ModeName(control->policy.mode),
            (unsigned int)(control->actuator.relay_on != 0U));
        (void)snprintf(rows->row[2], width, "SW2: TOGGLE PUMP");
        (void)snprintf(rows->row[3], width, "AUTO:%s", control->auto_irrigation_enabled ? "ENABLED" : "DISABLED");
    }
    else if (page == TERMINAL_PAGE_LINK)
    {
        (void)snprintf(rows->row[0], width, "RADIO:%s 4/5", link->ready ? (link->tx_busy ? "TX" : "IDLE") : "WAIT");
        (void)snprintf(rows->row[1], width, "Done:%lu", (unsigned long)link->tx_done_count);
        (void)snprintf(rows->row[2], width, "Err:%lu", (unsigned long)link->tx_error_count);
        (void)snprintf(rows->row[3], width, "Init:%lu", (unsigned long)link->init_error_count);
    }
    else
    {
        (void)snprintf(rows->row[0], width, "FAULTS 5/5");
        (void)snprintf(rows->row[1], width, "Alarm:0x%04X", (unsigned int)control->policy.alarm_mask);
        (void)snprintf(rows->row[2], width, "Mute:%u Ready:%u", (unsigned int)(control->policy.muted != 0U),
            (unsigned int)(control->actuator.ready_mask & ACTUATOR_READY_ALL));
        (void)snprintf(rows->row[3], width, "Res:%u M:%.6s", (unsigned int)control->policy.last_result & 0x0FU,
            ModeName(control->policy.mode));
    }
}
