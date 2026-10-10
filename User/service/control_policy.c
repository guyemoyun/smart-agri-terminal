#include "control_policy.h"
#include <string.h>

static uint8_t Fresh(uint8_t valid, uint32_t tick, uint32_t max_age, uint32_t now)
{
    return (uint8_t)(valid != 0U && (now - tick) <= max_age);
}

static uint8_t SoilFresh(const ControlConfig_t *config, const ControlInput_t *input, uint32_t now)
{
    return (uint8_t)(input->soil_level >= 1U && input->soil_level <= 4U &&
                    Fresh(input->soil_valid, input->soil_tick, config->soil_max_age_ms, now));
}

static void StopPump(ControlState_t *state, uint32_t now)
{
    if (state->relay_on != 0U)
    {
        state->pump_stop_tick = now;
        state->cooldown_active = 1U;
    }
    state->relay_on = 0U;
}

static ControlResult_t Result(ControlState_t *state, ControlResult_t result)
{
    state->last_result = result;
    return result;
}

void ControlPolicy_DefaultConfig(ControlConfig_t *config)
{
    *config = (ControlConfig_t){
        .temperature_on_x10 = 300, .temperature_off_x10 = 280,
        .co2_on_ppm = 1200U, .co2_off_ppm = 1000U,
        .temperature_fan_level = 2U, .co2_fan_level = 3U,
        .soil_dry_level = 1U, .soil_wet_level = 3U,
        /* 探头未校准前，估算的干湿等级不能直接启动自动灌溉。 */
        .auto_irrigation_enabled = 0U,
        .pump_max_ms = 10000U, .pump_cooldown_ms = 60000U,
        .manual_fan_lease_ms = 60000U, .soil_max_age_ms = 3200U,
        .dht_max_age_ms = 6000U, .co2_max_age_ms = 3000U,
        .startup_grace_ms = 8000U
    };
}

uint8_t ControlPolicy_ConfigValid(const ControlConfig_t *config)
{
    return (uint8_t)(config != 0 &&
        config->temperature_off_x10 < config->temperature_on_x10 &&
        config->temperature_off_x10 >= -400 && config->temperature_on_x10 <= 850 &&
        config->co2_off_ppm < config->co2_on_ppm &&
        config->temperature_fan_level >= 1U && config->temperature_fan_level <= 4U &&
        config->co2_fan_level >= 1U && config->co2_fan_level <= 4U &&
        config->soil_dry_level >= 1U && config->soil_wet_level <= 4U &&
        config->soil_dry_level < config->soil_wet_level && config->auto_irrigation_enabled <= 1U &&
        config->pump_max_ms > 0U && config->pump_max_ms <= 600000U &&
        config->pump_cooldown_ms > 0U && config->pump_cooldown_ms <= 600000U &&
        config->manual_fan_lease_ms > 0U && config->manual_fan_lease_ms <= 600000U &&
        config->soil_max_age_ms > 0U && config->soil_max_age_ms <= 600000U &&
        config->dht_max_age_ms > 0U && config->dht_max_age_ms <= 600000U &&
        config->co2_max_age_ms > 0U && config->co2_max_age_ms <= 600000U &&
        config->startup_grace_ms <= 600000U);
}

void ControlPolicy_Init(ControlState_t *state, uint32_t now)
{
    memset(state, 0, sizeof(*state));
    state->mode = CONTROL_MODE_MANUAL;
    state->init_tick = now;
}

void ControlPolicy_StopAll(ControlState_t *state, uint32_t now)
{
    StopPump(state, now);
    state->fan_level = 0U;
    state->temperature_request = 0U;
    state->co2_request = 0U;
    state->mode = CONTROL_MODE_SAFE;
    state->latched_alarm |= ALARM_EMERGENCY;
    state->alarm_mask |= ALARM_EMERGENCY;
    state->muted = 0U;
    state->last_result = CONTROL_OK;
}

void ControlPolicy_SetMuted(ControlState_t *state, uint8_t muted)
{
    state->muted = (uint8_t)(muted != 0U);
    if (state->muted != 0U) state->buzzer_on = 0U;
}

ControlResult_t ControlPolicy_SetMode(ControlState_t *state, const ControlConfig_t *config,
                                     const ControlInput_t *input, ControlMode_t mode, uint32_t now)
{
    if (mode != CONTROL_MODE_MANUAL && mode != CONTROL_MODE_AUTO && mode != CONTROL_MODE_SAFE)
        return Result(state, CONTROL_INVALID_ARGUMENT);
    if (mode == CONTROL_MODE_SAFE)
    {
        ControlPolicy_StopAll(state, now);
        return CONTROL_OK;
    }
    if (!ControlPolicy_ConfigValid(config) || input->ready_mask != ACTUATOR_READY_ALL)
        return Result(state, CONTROL_NOT_READY);
    /* 重复模式命令不能清零正在运行的定时保护或绕过冷却。 */
    if (state->mode == mode) return Result(state, CONTROL_OK);
    StopPump(state, now);
    state->fan_level = 0U;
    state->temperature_request = 0U;
    state->co2_request = 0U;
    state->latched_alarm = 0U; /* 明确切换模式作为 SAFE 的人工确认解锁。 */
    state->mode = mode;
    return Result(state, CONTROL_OK);
}

ControlResult_t ControlPolicy_SetPump(ControlState_t *state, const ControlConfig_t *config,
                                     const ControlInput_t *input, uint8_t on, uint32_t now)
{
    if (on > 1U) return Result(state, CONTROL_INVALID_ARGUMENT);
    if (!ControlPolicy_ConfigValid(config)) return Result(state, CONTROL_NOT_READY);
    if (state->mode != CONTROL_MODE_MANUAL) return Result(state, CONTROL_WRONG_MODE);
    if (on == 0U)
    {
        StopPump(state, now);
        return Result(state, CONTROL_OK);
    }
    if (input->ready_mask != ACTUATOR_READY_ALL) return Result(state, CONTROL_NOT_READY);
    if (!SoilFresh(config, input, now)) return Result(state, CONTROL_SENSOR_INVALID);
    if (state->relay_on != 0U) return Result(state, CONTROL_OK);
    if (state->cooldown_active != 0U && (now - state->pump_stop_tick) < config->pump_cooldown_ms)
        return Result(state, CONTROL_COOLDOWN);
    state->cooldown_active = 0U;
    state->relay_on = 1U;
    state->pump_start_tick = now;
    return Result(state, CONTROL_OK);
}

ControlResult_t ControlPolicy_SetFan(ControlState_t *state, uint8_t level, uint32_t now)
{
    if (level > 4U) return Result(state, CONTROL_INVALID_ARGUMENT);
    if (state->mode != CONTROL_MODE_MANUAL) return Result(state, CONTROL_WRONG_MODE);
    /* 同一设置的重发不延长租约，避免重复命令无限续期。 */
    if (state->fan_level != level) state->manual_fan_tick = now;
    state->fan_level = level;
    return Result(state, CONTROL_OK);
}

void ControlPolicy_Update(ControlState_t *state, const ControlConfig_t *config,
                          const ControlInput_t *input, uint32_t now)
{
    uint16_t alarm = state->latched_alarm;
    if (!ControlPolicy_ConfigValid(config))
    {
        StopPump(state, now);
        state->mode = CONTROL_MODE_SAFE;
        state->fan_level = 0U;
        state->alarm_mask = (uint16_t)(alarm | ALARM_CONFIG);
        state->buzzer_on = (uint8_t)(state->muted == 0U);
        return;
    }

    const uint8_t soil_ok = SoilFresh(config, input, now);
    const uint8_t dht_ok = (uint8_t)(input->temperature_x10 >= -400 && input->temperature_x10 <= 850 &&
        Fresh(input->dht_valid, input->dht_tick, config->dht_max_age_ms, now));
    const uint8_t co2_ok = Fresh(input->co2_valid, input->co2_tick, config->co2_max_age_ms, now);
    if ((now - state->init_tick) >= config->startup_grace_ms)
    {
        if (!soil_ok) alarm |= ALARM_SOIL_INVALID;
        if (!dht_ok) alarm |= ALARM_DHT_INVALID;
        if (!co2_ok) alarm |= ALARM_CO2_INVALID;
    }

    if (input->ready_mask != ACTUATOR_READY_ALL)
    {
        alarm |= ALARM_ACTUATOR;
        state->mode = CONTROL_MODE_SAFE;
    }
    if (state->relay_on != 0U && (now - state->pump_start_tick) >= config->pump_max_ms)
    {
        state->latched_alarm |= ALARM_PUMP_TIMEOUT;
        alarm |= ALARM_PUMP_TIMEOUT;
        state->mode = CONTROL_MODE_SAFE;
    }
    if (state->cooldown_active != 0U && (now - state->pump_stop_tick) >= config->pump_cooldown_ms)
        state->cooldown_active = 0U;

    if (!dht_ok || input->temperature_x10 <= config->temperature_off_x10)
        state->temperature_request = 0U;
    else if (input->temperature_x10 >= config->temperature_on_x10)
        state->temperature_request = 1U;
    if (!co2_ok || input->co2_ppm <= config->co2_off_ppm)
        state->co2_request = 0U;
    else if (input->co2_ppm >= config->co2_on_ppm)
        state->co2_request = 1U;
    if (state->temperature_request != 0U) alarm |= ALARM_TEMPERATURE;
    if (state->co2_request != 0U) alarm |= ALARM_CO2_HIGH;

    if (state->mode == CONTROL_MODE_SAFE)
    {
        StopPump(state, now);
        state->fan_level = 0U;
    }
    else if (state->mode == CONTROL_MODE_AUTO)
    {
        uint8_t temperature_level = state->temperature_request ? config->temperature_fan_level : 0U;
        uint8_t co2_level = state->co2_request ? config->co2_fan_level : 0U;
        state->fan_level = temperature_level > co2_level ? temperature_level : co2_level;
        if (!config->auto_irrigation_enabled || !soil_ok || input->soil_level >= config->soil_wet_level)
            StopPump(state, now);
        else if (state->relay_on == 0U && state->cooldown_active == 0U && input->soil_level <= config->soil_dry_level)
        {
            state->relay_on = 1U;
            state->pump_start_tick = now;
        }
    }
    else
    {
        if (!soil_ok) StopPump(state, now);
        if (state->fan_level != 0U && (now - state->manual_fan_tick) >= config->manual_fan_lease_ms)
            state->fan_level = 0U;
    }

    /* 出现新的告警位时重新提示，静音不能屏蔽后续新增的故障。 */
    if ((alarm & (uint16_t)~state->alarm_mask) != 0U) state->muted = 0U;
    state->alarm_mask = alarm;
    state->buzzer_on = (uint8_t)(alarm != 0U && state->muted == 0U && ((now / 250U) & 1U) == 0U);
}
