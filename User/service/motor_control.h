#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 电机型号：RF-300CA-11400-6.0C，规格依据 1112014.pdf 第 2、3 页。
 * 额定电压为 3.0V，规定的直流使用电压范围为 1.5～4.0V。
 * 按用户确认的实物使用工况，终端板 VCC5V 接口开放 0～100% PWM。
 * 该工况与上述 PDF 的直流范围不同，不作为厂家认证的使用条件。
 * 6V 短时运行是用户提供的信息，本模块不会提高电机供电电压。
 * 本模块仅管理开环 PWM 指令，不测量电机端电压、电流或转速，
 * 也不提供硬件过流保护；开放满占空比不等于已经完成实物验证。
 */
#define MOTOR_PWM_FREQUENCY_HZ       20000U /* 预期频率，由 CubeMX 配置 TIM4 */
#define MOTOR_PWM_MAX_DUTY_PERMILLE  1000U  /* 指令上限：1000/1000，即 100% */
#define MOTOR_LEVEL_COUNT           5U     /* 有效档位为 0～4 */
#define MOTOR_LEVEL_CUSTOM          0xFFU  /* 占空比不对应任何预设档位 */

/* 接口返回执行状态，便于本地控制和后续 LoRa 命令反馈。 */
typedef enum
{
    MOTOR_RESULT_OK = 0,
    MOTOR_RESULT_NOT_READY,
    MOTOR_RESULT_INVALID_ARGUMENT,
    MOTOR_RESULT_PWM_ERROR
} MotorResult_t;

/* 在 MX_TIM4_Init() 之后调用；重复调用只归零，不重复启动通道。 */
MotorResult_t Motor_Init(void);

/* 停止电机；仅归零 CH3，不停止共享定时器或修改蜂鸣器 CH4。 */
void Motor_Stop(void);

/* 占空比单位为千分比；超出 0～1000 则拒绝，保持原输出指令。 */
MotorResult_t Motor_SetDutyPermille(uint16_t duty_permille);

/* 设置 0～4 档；无效档位返回错误，不映射成最高档。 */
MotorResult_t Motor_SetLevel(uint8_t level);

/* 以下状态表示驱动是否就绪及已接受的输出指令，不代表实际转速。 */
uint8_t Motor_IsReady(void);
uint16_t Motor_GetDutyPermille(void);
uint8_t Motor_GetLevel(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_CONTROL_H */
