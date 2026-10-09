#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R300C 电机额定电压为 3V，而终端板电机接口接在 VCC5V。
 * 在完成电机端电压、电流和负载测试前，软件先将 PWM 最大指令
 * 限制为 60%，避免第一版程序以 100% 占空比持续施加 5V。
 */
#define MOTOR_PWM_FREQUENCY_HZ       20000U
#define MOTOR_PWM_MAX_DUTY_PERMILLE  600U
#define MOTOR_LEVEL_COUNT            5U

void Motor_Init(void);
void Motor_Stop(void);
void Motor_SetDutyPermille(uint16_t duty_permille);
void Motor_SetLevel(uint8_t level);
uint16_t Motor_GetDutyPermille(void);
uint8_t Motor_GetLevel(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_CONTROL_H */
