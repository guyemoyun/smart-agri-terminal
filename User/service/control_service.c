#include "control_service.h"
#include "sensor_service.h"
#include "main.h"
#include <math.h>

static ControlState_t state;
static ControlConfig_t config;

static void ReadInput(ControlInput_t *input)
{
    SensorData_t sensor;
    ActuatorState_t actuator;
    Sensor_GetSnapshot(&sensor);
    Actuator_GetState(&actuator);
    *input = (ControlInput_t){
        .co2_ppm = sensor.co2_ppm, .soil_level = sensor.soil_level,
        .soil_valid = sensor.soil_valid, .dht_valid = sensor.dht22_valid,
        .co2_valid = sensor.co2_valid, .ready_mask = actuator.ready_mask,
        .soil_tick = sensor.adc_update_tick, .dht_tick = sensor.dht22_update_tick,
        .co2_tick = sensor.co2_update_tick
    };
    if (isfinite(sensor.temperature) && sensor.temperature >= -40.0f && sensor.temperature <= 85.0f)
        input->temperature_x10 = (int16_t)(sensor.temperature * 10.0f);
    else input->dht_valid = 0U;
}

void Control_ServiceInit(uint32_t now)
{
    ControlPolicy_DefaultConfig(&config);
    ControlPolicy_Init(&state, now);
    Actuator_ServiceInit();
    Control_ServiceTask(now);
}

void Control_ServiceTask(uint32_t now)
{
    ControlInput_t input;
    ReadInput(&input);
    ControlPolicy_Update(&state, &config, &input, now);
    if (!Actuator_ServiceApply(state.relay_on, state.fan_level, state.buzzer_on))
    {
        /* 应用失败后再更新模型，让界面和遥测反映故障而非虚假输出。 */
        ReadInput(&input);
        ControlPolicy_Update(&state, &config, &input, now);
        Actuator_AllSafeOff();
    }
    HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, state.alarm_mask ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void Control_GetSnapshot(ControlSnapshot_t *snapshot)
{
    if (snapshot == 0) return;
    snapshot->policy = state;
    snapshot->auto_irrigation_enabled = config.auto_irrigation_enabled;
    Actuator_GetState(&snapshot->actuator);
}

ControlResult_t Control_SetMode(ControlMode_t mode, uint32_t now)
{
    ControlInput_t input;
    ReadInput(&input);
    ControlResult_t result = ControlPolicy_SetMode(&state, &config, &input, mode, now);
    Control_ServiceTask(now);
    return result;
}

ControlResult_t Control_SetPump(uint8_t on, uint32_t now)
{
    /* 先落实已到期的运行保护，再判断新请求，不能把超时重发报告为成功。 */
    Control_ServiceTask(now);
    ControlInput_t input;
    ReadInput(&input);
    ControlResult_t result = ControlPolicy_SetPump(&state, &config, &input, on, now);
    Control_ServiceTask(now);
    return result;
}

ControlResult_t Control_SetFan(uint8_t level, uint32_t now)
{
    Control_ServiceTask(now);
    ActuatorState_t actuator;
    Actuator_GetState(&actuator);
    if (actuator.ready_mask != ACTUATOR_READY_ALL)
    {
        state.last_result = CONTROL_NOT_READY;
        return CONTROL_NOT_READY;
    }
    ControlResult_t result = ControlPolicy_SetFan(&state, level, now);
    Control_ServiceTask(now);
    return result;
}

void Control_EmergencyStop(uint32_t now)
{
    ControlPolicy_StopAll(&state, now);
    Actuator_AllSafeOff();
    Control_ServiceTask(now);
}

void Control_ToggleMute(uint32_t now)
{
    ControlPolicy_SetMuted(&state, (uint8_t)(state.muted == 0U));
    Control_ServiceTask(now);
}

ControlResult_t Control_SetConfig(const ControlConfig_t *new_config, uint32_t now)
{
    if (!ControlPolicy_ConfigValid(new_config))
    {
        state.last_result = CONTROL_INVALID_ARGUMENT;
        return CONTROL_INVALID_ARGUMENT;
    }
    /* 参数变更停止执行器，避免旧阈值下的运行请求带入新配置。 */
    ControlPolicy_StopAll(&state, now);
    config = *new_config;
    Control_ServiceTask(now);
    return CONTROL_OK;
}
