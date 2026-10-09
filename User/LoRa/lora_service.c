#include "lora_service.h"
#include "llcc68_p2p.h"
#include "sensor_service.h"
#include <stdio.h>
#include <stdbool.h>

#define LORA_SERVICE_INIT_RETRY_MS       2000U
#define LORA_SERVICE_REPORT_PERIOD_MS    5000U
#define LORA_SERVICE_TX_TIMEOUT_MS       1500U
#define LORA_DEVICE_ID                   0x0001U
#define LORA_FRAME_MAGIC                 0xA5U
#define LORA_FRAME_VERSION               0x01U
#define LORA_FRAME_TYPE_SENSOR           0x01U
#define LORA_SENSOR_PAYLOAD_LENGTH       10U
#define LORA_FRAME_LENGTH                20U

static bool lora_ready;
static uint32_t next_init_tick;
static uint32_t next_report_tick;
static uint16_t frame_sequence;
static uint32_t tx_success_count;
static uint32_t tx_error_count;

static uint16_t lora_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;

    for (i = 0U; i < length; ++i)
    {
        uint8_t bit;
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

static void put_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)(value >> 8U);
}

static uint16_t temperature_to_x10(float value)
{
    int32_t scaled = (int32_t)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    return (uint16_t)(int16_t)scaled;
}

static uint8_t build_sensor_frame(uint8_t *frame)
{
    uint8_t valid_mask = 0U;
    uint16_t crc;
    uint16_t humidity_x10;
    uint16_t temperature_x10;
    const SensorData_t *sensor = (const SensorData_t *)&g_sensor_data;

    if (sensor->light_valid != 0U)
    {
        valid_mask |= 0x01U;
    }
    if (sensor->soil_valid != 0U)
    {
        valid_mask |= 0x02U;
    }
    if (sensor->dht22_valid != 0U)
    {
        valid_mask |= 0x04U;
    }
    if (sensor->co2_valid != 0U)
    {
        valid_mask |= 0x08U;
    }

    temperature_x10 = temperature_to_x10(sensor->temperature);
    humidity_x10 = (uint16_t)(sensor->humidity * 10.0f + 0.5f);

    frame[0] = LORA_FRAME_MAGIC;
    frame[1] = LORA_FRAME_VERSION;
    put_u16_le(&frame[2], LORA_DEVICE_ID);
    frame[4] = LORA_FRAME_TYPE_SENSOR;
    put_u16_le(&frame[5], frame_sequence++);
    frame[7] = LORA_SENSOR_PAYLOAD_LENGTH;

    put_u16_le(&frame[8], temperature_x10);
    put_u16_le(&frame[10], humidity_x10);
    put_u16_le(&frame[12], sensor->co2_ppm);
    put_u16_le(&frame[14], sensor->light_lux);
    frame[16] = sensor->soil_level;
    frame[17] = valid_mask;

    crc = lora_crc16(frame, 18U);
    put_u16_le(&frame[18], crc);
    return LORA_FRAME_LENGTH;
}

void Lora_ServiceInit(void)
{
    lora_ready = false;
    next_init_tick = 0U;
    next_report_tick = 0U;
    frame_sequence = 0U;
    tx_success_count = 0U;
    tx_error_count = 0U;
    printf("LoRa service: waiting for LLCC68\\r\\n");
}

void Lora_ServiceTask(uint32_t now)
{
    uint8_t frame[LORA_FRAME_LENGTH];
    uint8_t frame_length;
    llcc68_status_t status;

    /* Foreground side of the DIO1 interrupt handshake. */
    llcc68_process_irq();

    if (!lora_ready)
    {
        if ((int32_t)(now - next_init_tick) < 0)
        {
            return;
        }

        printf("LoRa: initializing LLCC68\\r\\n");
        status = llcc68_init(&llcc68_ctx);
        if (status != LLCC68_STATUS_OK)
        {
            printf("LoRa: init failed (%d), retry in %u ms\\r\\n",
                   (int)status, (unsigned int)LORA_SERVICE_INIT_RETRY_MS);
            next_init_tick = HAL_GetTick() + LORA_SERVICE_INIT_RETRY_MS;
            return;
        }

        lora_ready = true;
        next_report_tick = HAL_GetTick();
        printf("LoRa: ready, SF9/BW125/470.5MHz\\r\\n");
        return;
    }

    if ((int32_t)(now - next_report_tick) < 0)
    {
        return;
    }

    frame_length = build_sensor_frame(frame);
    status = llcc68_lora_send(&llcc68_ctx, frame, frame_length, LORA_SERVICE_TX_TIMEOUT_MS);
    if (status == LLCC68_STATUS_OK)
    {
        ++tx_success_count;
        printf("LoRa: sensor frame sent, seq=%u ok=%lu\\r\\n",
               (unsigned int)(uint16_t)(frame_sequence - 1U),
               (unsigned long)tx_success_count);
    }
    else
    {
        ++tx_error_count;
        printf("LoRa: sensor frame failed, err=%lu\\r\\n",
               (unsigned long)tx_error_count);
    }

    next_report_tick = HAL_GetTick() + LORA_SERVICE_REPORT_PERIOD_MS;
}
