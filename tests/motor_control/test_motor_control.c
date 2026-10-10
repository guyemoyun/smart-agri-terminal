#include "motor_control.h"
#include "tim.h"
#include <stddef.h>
#include <stdio.h>

TIM_TypeDef mock_tim4;
TIM_HandleTypeDef htim4;

static uint32_t active_ccr3;
static uint32_t active_ccr4;
static unsigned int compare_writes;
static unsigned int start_calls;
static unsigned int stop_calls;
static unsigned int start_nonzero_compare;
static unsigned int failures;
static unsigned int cases;
static unsigned int failed_cases;
static HAL_StatusTypeDef start_result = HAL_OK;

#define CHECK(condition) do { \
    if (!(condition)) { \
        ++failures; \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
    } \
} while (0)

void Mock_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value)
{
    CHECK(timer == &htim4);
    CHECK(timer->Instance == TIM4);
    ++compare_writes;

    if (channel == TIM_CHANNEL_3)
    {
        timer->Instance->CCR3 = value;
        if ((timer->Instance->CCMR2 & TIM_CCMR2_OC3PE) == 0U)
        {
            active_ccr3 = value;
        }
    }
    else if (channel == TIM_CHANNEL_4)
    {
        timer->Instance->CCR4 = value;
        if ((timer->Instance->CCMR2 & TIM_CCMR2_OC4PE) == 0U)
        {
            active_ccr4 = value;
        }
    }
    else
    {
        CHECK(0);
    }
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    CHECK(timer == &htim4);
    CHECK(channel == TIM_CHANNEL_3);
    ++start_calls;
    if (timer->Instance->CCR3 != 0U || active_ccr3 != 0U)
    {
        ++start_nonzero_compare;
    }

    /* 错误路径也模拟已使能部分输出，以验证驱动会完成清理。 */
    timer->Instance->CCER |= TIM_CCER_CC3E;
    timer->Instance->CR1 |= TIM_CR1_CEN;
    return start_result;
}

HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    CHECK(timer == &htim4);
    CHECK(channel == TIM_CHANNEL_3);
    ++stop_calls;
    timer->Instance->CCER &= ~TIM_CCER_CC3E;
    if ((timer->Instance->CCER & TIM_CCER_CC4E) == 0U)
    {
        timer->Instance->CR1 &= ~TIM_CR1_CEN;
    }
    return HAL_OK;
}

/* 模拟硬件更新事件将两个通道的预装载值分别装入有效比较寄存器。 */
static void Mock_Update(void)
{
    active_ccr3 = mock_tim4.CCR3;
    active_ccr4 = mock_tim4.CCR4;
}

static void Check_BuzzerUnchanged(void)
{
    CHECK(mock_tim4.CCR4 == 1234U);
    CHECK(active_ccr4 == 1234U);
    CHECK((mock_tim4.CCMR2 & TIM_CCMR2_OC4PE) != 0U);
    CHECK((mock_tim4.CCER & TIM_CCER_CC4E) != 0U);
    CHECK((mock_tim4.CR1 & TIM_CR1_CEN) != 0U);
    CHECK(mock_tim4.ARR == 3599U);
}

static void Test_BeforeInit(void)
{
    CHECK(Motor_IsReady() == 0U);
    CHECK(Motor_GetDutyPermille() == 0U);
    CHECK(Motor_GetLevel() == 0U);
    CHECK(Motor_SetDutyPermille(300U) == MOTOR_RESULT_NOT_READY);
    CHECK(Motor_SetLevel(4U) == MOTOR_RESULT_NOT_READY);
    CHECK(Motor_GetDutyPermille() == 0U);
    CHECK(Motor_GetLevel() == 0U);
    Motor_Stop();
    CHECK(compare_writes == 0U);
}

static void Test_InitBeforeTimer(void)
{
    CHECK(Motor_Init() == MOTOR_RESULT_NOT_READY);
    CHECK(Motor_IsReady() == 0U);
    CHECK(start_calls == 0U);
    CHECK(compare_writes == 0U);
}

static void Test_StartFailure(void)
{
    htim4.Instance = TIM4;
    mock_tim4.ARR = 3599U;
    mock_tim4.CCR3 = 1800U;
    mock_tim4.CCR4 = 1234U;
    active_ccr3 = 1800U;
    active_ccr4 = 1234U;
    mock_tim4.CCMR2 = TIM_CCMR2_OC3PE | TIM_CCMR2_OC4PE;
    mock_tim4.CCER = TIM_CCER_CC4E;
    mock_tim4.CR1 = TIM_CR1_CEN;
    start_result = HAL_ERROR;

    CHECK(Motor_Init() == MOTOR_RESULT_PWM_ERROR);
    CHECK(start_calls == 1U);
    CHECK(stop_calls == 1U);
    CHECK(start_nonzero_compare == 0U);
    CHECK(mock_tim4.CCR3 == 0U);
    CHECK(active_ccr3 == 0U);
    CHECK((mock_tim4.CCER & TIM_CCER_CC3E) == 0U);
    CHECK(Motor_IsReady() == 0U);
    CHECK(Motor_SetLevel(1U) == MOTOR_RESULT_NOT_READY);
    CHECK(Motor_GetDutyPermille() == 0U);
    Check_BuzzerUnchanged();
}

static void Test_InitRetry(void)
{
    start_result = HAL_OK;
    CHECK(Motor_Init() == MOTOR_RESULT_OK);
    CHECK(start_calls == 2U);
    CHECK(start_nonzero_compare == 0U);
    CHECK(Motor_IsReady() == 1U);
    CHECK(Motor_GetDutyPermille() == 0U);
    CHECK(Motor_GetLevel() == 0U);
    CHECK(mock_tim4.CCR3 == 0U);
    CHECK(active_ccr3 == 0U);
    Check_BuzzerUnchanged();
}

static void Test_LevelMapping(void)
{
    static const uint16_t duties[] = {0U, 300U, 500U, 800U, 1000U};
    static const uint32_t compares[] = {0U, 1080U, 1800U, 2880U, 3600U};
    uint8_t i;

    for (i = 0U; i < MOTOR_LEVEL_COUNT; ++i)
    {
        CHECK(Motor_SetLevel(i) == MOTOR_RESULT_OK);
        CHECK(Motor_GetLevel() == i);
        CHECK(Motor_GetDutyPermille() == duties[i]);
        CHECK(mock_tim4.CCR3 == compares[i]);
        Mock_Update();
        CHECK(active_ccr3 == compares[i]);
        Check_BuzzerUnchanged();
    }
}

static void Test_EightyAndFullDuty(void)
{
    uint32_t counter;

    CHECK(Motor_SetDutyPermille(800U) == MOTOR_RESULT_OK);
    CHECK(mock_tim4.CCR3 == 2880U);
    CHECK(Motor_GetLevel() == 3U);
    CHECK(Motor_GetDutyPermille() == 800U);
    CHECK(Motor_SetDutyPermille(1000U) == MOTOR_RESULT_OK);
    CHECK(mock_tim4.CCR3 == mock_tim4.ARR + 1U);
    CHECK(Motor_GetLevel() == 4U);
    CHECK(Motor_GetDutyPermille() == 1000U);
    Mock_Update();

    /* 模拟 PWM1 的比较条件，100% 档在所有有效计数值下都应保持有效。 */
    for (counter = 0U; counter <= mock_tim4.ARR; ++counter)
    {
        CHECK(counter < active_ccr3);
    }
    Check_BuzzerUnchanged();
}

static void Test_InvalidLevels(void)
{
    unsigned int writes;
    CHECK(Motor_SetLevel(2U) == MOTOR_RESULT_OK);
    Mock_Update();
    writes = compare_writes;
    CHECK(Motor_SetLevel(5U) == MOTOR_RESULT_INVALID_ARGUMENT);
    CHECK(Motor_SetLevel(255U) == MOTOR_RESULT_INVALID_ARGUMENT);
    CHECK(compare_writes == writes);
    CHECK(mock_tim4.CCR3 == 1800U);
    CHECK(active_ccr3 == 1800U);
    CHECK(Motor_GetLevel() == 2U);
    CHECK(Motor_GetDutyPermille() == 500U);
}

static void Test_InvalidDuty(void)
{
    const unsigned int writes = compare_writes;
    CHECK(Motor_SetDutyPermille(1001U) == MOTOR_RESULT_INVALID_ARGUMENT);
    CHECK(Motor_SetDutyPermille(2000U) == MOTOR_RESULT_INVALID_ARGUMENT);
    CHECK(Motor_SetDutyPermille(65535U) == MOTOR_RESULT_INVALID_ARGUMENT);
    CHECK(compare_writes == writes);
    CHECK(mock_tim4.CCR3 == 1800U);
    CHECK(active_ccr3 == 1800U);
    CHECK(Motor_GetLevel() == 2U);
    CHECK(Motor_GetDutyPermille() == 500U);
}

static void Test_CustomDuty(void)
{
    CHECK(Motor_SetDutyPermille(350U) == MOTOR_RESULT_OK);
    CHECK(mock_tim4.CCR3 == 1260U);
    CHECK(Motor_GetDutyPermille() == 350U);
    CHECK(Motor_GetLevel() == MOTOR_LEVEL_CUSTOM);
    CHECK(Motor_SetDutyPermille(300U) == MOTOR_RESULT_OK);
    CHECK(Motor_GetLevel() == 1U);
}

static void Test_ImmediateStop(void)
{
    CHECK(Motor_SetLevel(4U) == MOTOR_RESULT_OK);
    Mock_Update();
    CHECK(active_ccr3 == 3600U);
    Motor_Stop();
    /* 停止不需要先调用模拟更新事件，比较寄存器立即归零。 */
    CHECK(active_ccr3 == 0U);
    CHECK(mock_tim4.CCR3 == 0U);
    CHECK(Motor_GetDutyPermille() == 0U);
    CHECK(Motor_GetLevel() == 0U);
    CHECK(Motor_IsReady() == 1U);
    CHECK((mock_tim4.CCMR2 & TIM_CCMR2_OC3PE) != 0U);
    Check_BuzzerUnchanged();
}

static void Test_ZeroDutyMeansStop(void)
{
    CHECK(Motor_SetDutyPermille(350U) == MOTOR_RESULT_OK);
    Mock_Update();
    CHECK(Motor_SetDutyPermille(0U) == MOTOR_RESULT_OK);
    CHECK(active_ccr3 == 0U);
    CHECK(Motor_GetLevel() == 0U);
    CHECK(Motor_GetDutyPermille() == 0U);
}

static void Test_ReinitDoesNotRestart(void)
{
    unsigned int starts;
    CHECK(Motor_SetLevel(4U) == MOTOR_RESULT_OK);
    Mock_Update();
    starts = start_calls;
    CHECK(Motor_Init() == MOTOR_RESULT_OK);
    CHECK(Motor_Init() == MOTOR_RESULT_OK);
    CHECK(start_calls == starts);
    CHECK(active_ccr3 == 0U);
    CHECK(Motor_IsReady() == 1U);
    CHECK(Motor_GetLevel() == 0U);
    CHECK(Motor_GetDutyPermille() == 0U);
    Check_BuzzerUnchanged();
}

static void Test_AllAcceptedDuties(void)
{
    uint16_t duty;
    for (duty = 0U; duty <= MOTOR_PWM_MAX_DUTY_PERMILLE; ++duty)
    {
        const uint32_t expected = (3600U * duty) / 1000U;
        CHECK(Motor_SetDutyPermille(duty) == MOTOR_RESULT_OK);
        CHECK(Motor_GetDutyPermille() == duty);
        CHECK(mock_tim4.CCR3 == expected);
        CHECK(mock_tim4.CCR3 <= 3600U);
    }
    Motor_Stop();
    Check_BuzzerUnchanged();
}

static void Test_StopPreservesDisabledPreload(void)
{
    mock_tim4.CCMR2 &= ~TIM_CCMR2_OC3PE;
    CHECK(Motor_SetDutyPermille(350U) == MOTOR_RESULT_OK);
    CHECK(active_ccr3 == 1260U);
    Motor_Stop();
    CHECK((mock_tim4.CCMR2 & TIM_CCMR2_OC3PE) == 0U);
    CHECK(active_ccr3 == 0U);
    Check_BuzzerUnchanged();
}

static void Run(const char *name, void (*test)(void))
{
    const unsigned int before = failures;
    ++cases;
    test();
    if (before == failures)
    {
        printf("PASS %s\n", name);
    }
    else
    {
        ++failed_cases;
    }
}

int main(void)
{
    Run("before_init_no_cached_command", Test_BeforeInit);
    Run("init_requires_timer", Test_InitBeforeTimer);
    Run("start_failure_cleanup", Test_StartFailure);
    Run("retry_stays_off", Test_InitRetry);
    Run("level_mapping", Test_LevelMapping);
    Run("eighty_and_full_duty", Test_EightyAndFullDuty);
    Run("invalid_level_preserves_output", Test_InvalidLevels);
    Run("invalid_duty_preserves_output", Test_InvalidDuty);
    Run("custom_and_preset_state", Test_CustomDuty);
    Run("stop_clears_active_compare", Test_ImmediateStop);
    Run("zero_duty_reports_stopped", Test_ZeroDutyMeansStop);
    Run("reinit_does_not_restart", Test_ReinitDoesNotRestart);
    Run("all_1001_accepted_duties", Test_AllAcceptedDuties);
    Run("stop_preserves_preload_configuration", Test_StopPreservesDisabledPreload);
    printf("%u/%u cases passed; %u assertions failed\n",
           cases - failed_cases, cases, failures);
    return failures == 0U ? 0 : 1;
}
