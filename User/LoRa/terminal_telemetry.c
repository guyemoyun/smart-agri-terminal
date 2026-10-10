#include "terminal_telemetry.h"
#include <math.h>

static void Put16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8U);
}

uint16_t Terminal_Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc >> 1U) ^ ((crc & 1U) != 0U ? 0xA001U : 0U));
    }
    return crc;
}

size_t Terminal_EncodeTelemetry(uint8_t *out, size_t capacity, uint16_t node,
    uint16_t sequence, const SensorData_t *sensor, const ControlSnapshot_t *control, uint32_t now)
{
    uint8_t mask = 0U;
    int16_t temperature = 0;
    uint16_t humidity = 0U;
    if (out == 0 || sensor == 0 || control == 0 || capacity < TELEMETRY_FRAME_SIZE) return 0U;
    const uint8_t adc_fresh = (uint8_t)((now - sensor->adc_update_tick) <= 3200U);
    const uint8_t dht_fresh = (uint8_t)((now - sensor->dht22_update_tick) <= 6000U);
    const uint8_t co2_fresh = (uint8_t)((now - sensor->co2_update_tick) <= 3000U);
    if (sensor->light_valid && adc_fresh) mask |= 1U;
    if (sensor->soil_valid && adc_fresh) mask |= 2U;
    if (sensor->dht22_valid && dht_fresh && isfinite(sensor->temperature) && isfinite(sensor->humidity) &&
        sensor->temperature >= -40.0f && sensor->temperature <= 85.0f &&
        sensor->humidity >= 0.0f && sensor->humidity <= 100.0f)
    {
        mask |= 4U;
        temperature = (int16_t)(sensor->temperature * 10.0f + (sensor->temperature >= 0.0f ? 0.5f : -0.5f));
        humidity = (uint16_t)(sensor->humidity * 10.0f + 0.5f);
    }
    if (sensor->co2_valid && co2_fresh) mask |= 8U;
    out[0] = 0xA5U; out[1] = TELEMETRY_VERSION; Put16(&out[2], node);
    out[4] = 1U; Put16(&out[5], sequence); out[7] = TELEMETRY_PAYLOAD_SIZE;
    Put16(&out[8], (uint16_t)temperature); Put16(&out[10], humidity);
    Put16(&out[12], (mask & 8U) ? sensor->co2_ppm : 0U);
    Put16(&out[14], (mask & 1U) ? sensor->light_lux : 0U);
    out[16] = (mask & 2U) ? sensor->soil_level : 0U; out[17] = mask;
    out[18] = (uint8_t)control->policy.mode; out[19] = control->actuator.relay_on;
    out[20] = control->actuator.fan_level; out[21] = control->actuator.buzzer_on;
    Put16(&out[22], control->actuator.fan_duty_permille);
    Put16(&out[24], control->policy.alarm_mask); out[26] = control->actuator.ready_mask;
    out[27] = (uint8_t)((control->policy.muted ? 1U : 0U) |
                       (control->auto_irrigation_enabled ? 2U : 0U));
    const uint32_t seconds = now / 1000U;
    out[28] = (uint8_t)seconds; out[29] = (uint8_t)(seconds >> 8U);
    out[30] = (uint8_t)(seconds >> 16U); out[31] = (uint8_t)(seconds >> 24U);
    Put16(&out[32], Terminal_Crc16(out, 32U));
    return TELEMETRY_FRAME_SIZE;
}
