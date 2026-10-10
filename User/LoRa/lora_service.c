#include "lora_service.h"
#include "llcc68_p2p.h"
#include "terminal_telemetry.h"
#include "button_service.h"
#include <stdio.h>
#include <string.h>

#define INIT_RETRY_MS 5000U
#define REPORT_PERIOD_MS 5000U
#define TX_TIMEOUT_MS 1500U
#define NODE_ID 1U

static LoraStatus_t state;
static uint32_t last_init_attempt, last_report;
static uint16_t sequence;

void Lora_ServiceInit(void)
{
    memset(&state, 0, sizeof(state));
    sequence = 0U;
    last_init_attempt = HAL_GetTick() - INIT_RETRY_MS;
    last_report = HAL_GetTick() - REPORT_PERIOD_MS;
    printf("LoRa: telemetry v2 service ready, radio not checked\r\n");
}

void Lora_GetStatus(LoraStatus_t *status)
{
    if (status != 0) *status = state;
}

void Lora_ServiceTask(uint32_t now)
{
    if (state.ready == 0U)
    {
        ControlSnapshot_t control;
        Control_GetSnapshot(&control);
        /* 当前初始化含有有界延时，只在执行器关闭且按键释放时尝试。 */
        if (control.actuator.relay_on || control.actuator.fan_duty_permille || control.actuator.buzzer_on || Button_IsHeld() ||
            (now - last_init_attempt) < INIT_RETRY_MS) return;
        last_init_attempt = now;
        if (llcc68_init(&llcc68_ctx) != LLCC68_STATUS_OK)
        {
            ++state.init_error_count;
            printf("LoRa: init failed (%lu)\r\n", (unsigned long)state.init_error_count);
            return;
        }
        /* SPI 无响应时读取全零也可能返回 HAL_OK，检查实际芯片待机模式。 */
        llcc68_chip_status_t chip;
        if (llcc68_get_status(&llcc68_ctx, &chip) != LLCC68_STATUS_OK ||
            (chip.chip_mode != LLCC68_CHIP_MODE_STBY_XOSC && chip.chip_mode != LLCC68_CHIP_MODE_STBY_RC))
        {
            ++state.init_error_count;
            return;
        }
        state.ready = 1U;
        last_report = HAL_GetTick() - REPORT_PERIOD_MS;
        return;
    }

    if (state.tx_busy != 0U)
    {
        const llcc68_tx_result_t result = llcc68_lora_tx_poll(&llcc68_ctx, now);
        if (result == LLCC68_TX_PENDING) return;
        state.tx_busy = 0U;
        last_report = HAL_GetTick();
        if (result == LLCC68_TX_DONE)
        {
            ++state.tx_done_count; /* 仅表示芯片发送结束，不代表网关收到。 */
            printf("LoRa: TX_DONE seq=%u\r\n", (unsigned int)state.last_sequence);
        }
        else
        {
            ++state.tx_error_count;
            state.ready = 0U;
            last_init_attempt = HAL_GetTick();
        }
        return;
    }
    if ((now - last_report) < REPORT_PERIOD_MS) return;
    uint8_t frame[TELEMETRY_FRAME_SIZE];
    SensorData_t sensor;
    ControlSnapshot_t control;
    Sensor_GetSnapshot(&sensor);
    Control_GetSnapshot(&control);
    const size_t length = Terminal_EncodeTelemetry(frame, sizeof(frame), NODE_ID, sequence, &sensor, &control, now);
    state.last_sequence = sequence++;
    if (length != TELEMETRY_FRAME_SIZE ||
        llcc68_lora_send_start(&llcc68_ctx, frame, (uint8_t)length, TX_TIMEOUT_MS) != LLCC68_STATUS_OK)
    {
        ++state.tx_error_count;
        state.ready = 0U;
        last_init_attempt = HAL_GetTick();
        return;
    }
    state.tx_busy = 1U;
}
