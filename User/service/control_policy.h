#ifndef CONTROL_POLICY_H
#define CONTROL_POLICY_H

#include <stdint.h>

/* 纯 C 控制模型，不依赖 HAL，时间统一使用可回绕的毫秒计数。 */
typedef enum { CONTROL_MODE_MANUAL = 0, CONTROL_MODE_AUTO, CONTROL_MODE_SAFE } ControlMode_t;
typedef enum
{
    CONTROL_OK = 0, CONTROL_INVALID_ARGUMENT, CONTROL_WRONG_MODE,
    CONTROL_SENSOR_INVALID, CONTROL_COOLDOWN, CONTROL_NOT_READY
} ControlResult_t;

#define ACTUATOR_READY_MOTOR  0x01U
#define ACTUATOR_READY_BUZZER 0x02U
#define ACTUATOR_READY_RELAY  0x04U
#define ACTUATOR_READY_ALL   0x07U

#define ALARM_TEMPERATURE   0x0001U
#define ALARM_CO2_HIGH      0x0002U
#define ALARM_SOIL_INVALID  0x0004U
#define ALARM_DHT_INVALID   0x0008U
#define ALARM_CO2_INVALID   0x0010U
#define ALARM_PUMP_TIMEOUT  0x0020U
#define ALARM_EMERGENCY     0x0040U
#define ALARM_ACTUATOR      0x0080U
#define ALARM_CONFIG        0x0100U

typedef struct
{
    int16_t temperature_x10;
    uint16_t co2_ppm;
    uint8_t soil_level;
    uint8_t soil_valid, dht_valid, co2_valid;
    uint8_t ready_mask;
    uint32_t soil_tick, dht_tick, co2_tick;
} ControlInput_t;

typedef struct
{
    int16_t temperature_on_x10, temperature_off_x10;
    uint16_t co2_on_ppm, co2_off_ppm;
    uint8_t temperature_fan_level, co2_fan_level;
    uint8_t soil_dry_level, soil_wet_level, auto_irrigation_enabled;
    uint32_t pump_max_ms, pump_cooldown_ms, manual_fan_lease_ms;
    uint32_t soil_max_age_ms, dht_max_age_ms, co2_max_age_ms, startup_grace_ms;
} ControlConfig_t;

typedef struct
{
    ControlMode_t mode;
    ControlResult_t last_result;
    uint8_t relay_on, fan_level, buzzer_on, muted;
    uint8_t temperature_request, co2_request, cooldown_active;
    uint16_t alarm_mask, latched_alarm;
    uint32_t init_tick, pump_start_tick, pump_stop_tick, manual_fan_tick;
} ControlState_t;

void ControlPolicy_DefaultConfig(ControlConfig_t *config);
uint8_t ControlPolicy_ConfigValid(const ControlConfig_t *config);
void ControlPolicy_Init(ControlState_t *state, uint32_t now);
void ControlPolicy_Update(ControlState_t *state, const ControlConfig_t *config,
                          const ControlInput_t *input, uint32_t now);
ControlResult_t ControlPolicy_SetMode(ControlState_t *state, const ControlConfig_t *config,
                                     const ControlInput_t *input, ControlMode_t mode, uint32_t now);
ControlResult_t ControlPolicy_SetPump(ControlState_t *state, const ControlConfig_t *config,
                                     const ControlInput_t *input, uint8_t on, uint32_t now);
ControlResult_t ControlPolicy_SetFan(ControlState_t *state, uint8_t level, uint32_t now);
void ControlPolicy_StopAll(ControlState_t *state, uint32_t now);
void ControlPolicy_SetMuted(ControlState_t *state, uint8_t muted);

#endif
