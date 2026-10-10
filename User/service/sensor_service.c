#include "sensor_service.h"
#include "CO2.h"
#include "dht22.h"
#include "light.h"
#include "soil.h"
#include "oled.h"
#include "terminal_ui.h"
#include <math.h>
#include "app_main.h"
#include <stdio.h>
#include <string.h>

#define ADC_FILTER_SAMPLES       16U
#define ADC_UPDATE_PERIOD_MS     100U
#define ADC_OFFLINE_TIMEOUT_MS   3200U
#define DHT22_UPDATE_PERIOD_MS   2000U
#define CO2_OFFLINE_TIMEOUT_MS   3000U
#define SENSOR_STATUS_PERIOD_MS   500U
#define REPORT_PERIOD_MS         1000U

volatile SensorData_t g_sensor_data;

static uint32_t adc_light_sum;
static uint32_t adc_soil_sum;
static uint8_t adc_sample_count;
static uint32_t last_adc_tick;
static uint32_t last_dht22_tick;
static uint32_t last_co2_tick;
static uint32_t last_status_tick;
static uint32_t last_report_tick;

static void Sensor_UpdateADC(uint32_t now)
{
    adc_light_sum += adc_values[0];
    adc_soil_sum += adc_values[1];
    adc_sample_count++;

    if (adc_sample_count < ADC_FILTER_SAMPLES)
    {
        return;
    }

    g_sensor_data.light_adc = (uint16_t)(adc_light_sum / ADC_FILTER_SAMPLES);
    g_sensor_data.soil_adc = (uint16_t)(adc_soil_sum / ADC_FILTER_SAMPLES);
    g_sensor_data.light_lux = GetLuxFromAdc(g_sensor_data.light_adc);
    g_sensor_data.soil_level = GetSoilHumidityFromAdc(g_sensor_data.soil_adc);
    /* 接近电源轨只能识别可疑信号，不宣称具备完整的探头断线诊断。 */
    g_sensor_data.light_valid = (uint8_t)(g_sensor_data.light_adc > 15U && g_sensor_data.light_adc < 4080U);
    g_sensor_data.soil_valid = (uint8_t)(g_sensor_data.soil_adc > 15U && g_sensor_data.soil_adc < 4080U);
    g_sensor_data.adc_update_tick = now;

    adc_light_sum = 0U;
    adc_soil_sum = 0U;
    adc_sample_count = 0U;
}

static void Sensor_UpdateDHT22(uint32_t now)
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint8_t error = DHT22_ReadData(&temperature, &humidity);

    g_sensor_data.dht22_error = error;
    if (error == 0U && (!isfinite(temperature) || !isfinite(humidity) ||
        temperature < -40.0f || temperature > 85.0f || humidity < 0.0f || humidity > 100.0f))
    {
        error = 5U;
        g_sensor_data.dht22_error = error;
    }
    if (error == 0U)
    {
        g_sensor_data.temperature = temperature;
        g_sensor_data.humidity = humidity;
        g_sensor_data.dht22_valid = 1U;
        g_sensor_data.dht22_update_tick = now;
    }
}

static void Sensor_UpdateCO2(uint32_t now)
{
    uint16_t co2_ppm = 0U;
    uint8_t error = CO2_TryRead(&co2_ppm);

    if (error == CO2_READ_NO_DATA) return;

    g_sensor_data.co2_error = error;
    if (error == 0U)
    {
        g_sensor_data.co2_ppm = co2_ppm;
        g_sensor_data.co2_valid = 1U;
        g_sensor_data.co2_update_tick = now;
    }
}

static void Sensor_UpdateValidity(uint32_t now)
{
    if ((now - g_sensor_data.adc_update_tick) > ADC_OFFLINE_TIMEOUT_MS)
    {
        g_sensor_data.light_valid = 0U;
        g_sensor_data.soil_valid = 0U;
    }

    if ((now - g_sensor_data.dht22_update_tick) > (DHT22_UPDATE_PERIOD_MS * 3U))
    {
        g_sensor_data.dht22_valid = 0U;
    }

    if ((now - g_sensor_data.co2_update_tick) > CO2_OFFLINE_TIMEOUT_MS)
    {
        g_sensor_data.co2_valid = 0U;
    }
}

void Sensor_ServiceInit(void)
{
    uint32_t now = HAL_GetTick();

    memset((void *)&g_sensor_data, 0, sizeof(g_sensor_data));
    g_sensor_data.dht22_error = 1U;
    g_sensor_data.co2_error = 1U;

    adc_light_sum = 0U;
    adc_soil_sum = 0U;
    adc_sample_count = 0U;
    last_adc_tick = now;
    last_dht22_tick = now; /* DHT22 上电后至少等待2秒再首次读取 */
    last_co2_tick = now - 20U;
    last_status_tick = now;
    last_report_tick = now;
}

void Sensor_ServiceTask(uint32_t now)
{
    if ((now - last_adc_tick) >= ADC_UPDATE_PERIOD_MS)
    {
        last_adc_tick = now;
        Sensor_UpdateADC(now);
    }

    if ((now - last_co2_tick) >= 20U)
    {
        last_co2_tick = now;
        Sensor_UpdateCO2(now);
        now = HAL_GetTick();
    }

    if ((now - last_dht22_tick) >= DHT22_UPDATE_PERIOD_MS)
    {
        last_dht22_tick = now;
        Sensor_UpdateDHT22(now);
        now = HAL_GetTick();
    }

    if ((now - last_status_tick) >= SENSOR_STATUS_PERIOD_MS)
    {
        last_status_tick = now;
        Sensor_UpdateValidity(now);
    }

    if ((now - last_report_tick) >= REPORT_PERIOD_MS)
    {
        last_report_tick = now;
        Sensor_ServiceReport();
    }
}

void Sensor_ServiceReport(void)
{
    int temperature_x10 = (int)(g_sensor_data.temperature * 10.0f);
    int humidity_x10 = (int)(g_sensor_data.humidity * 10.0f);

    printf("ADC: light=%u soil=%u status=%s\r\n",
           (unsigned int)g_sensor_data.light_adc,
           (unsigned int)g_sensor_data.soil_adc,
           (g_sensor_data.light_valid && g_sensor_data.soil_valid) ? "OK" : "ERR");

    if (g_sensor_data.dht22_valid)
    {
        printf("DHT22: T=%d.%dC H=%d.%d%%\r\n",
               temperature_x10 / 10,
               temperature_x10 < 0 ? -temperature_x10 % 10 : temperature_x10 % 10,
               humidity_x10 / 10,
               humidity_x10 % 10);
    }
    else
    {
        printf("DHT22: ERR(%u)\r\n", (unsigned int)g_sensor_data.dht22_error);
    }

    if (g_sensor_data.co2_valid)
    {
        printf("CO2: %u ppm\r\n", (unsigned int)g_sensor_data.co2_ppm);
    }
    else
    {
        printf("CO2: ERR(%u)\r\n", (unsigned int)g_sensor_data.co2_error);
    }

    printf("ENV: lux=%u soil=%u\r\n",
           (unsigned int)g_sensor_data.light_lux,
           (unsigned int)g_sensor_data.soil_level);
}

void Sensor_GetSnapshot(SensorData_t *snapshot)
{
    /* 状态只在主循环更新，ADC/UART ISR 不直接修改该结构。 */
    if (snapshot != 0) *snapshot = g_sensor_data;
}

void Sensor_ServiceDisplay(void)
{
    /* 兼容旧入口；显示所有权集中交给 UI，避免环境页覆盖控制页。 */
    Terminal_UIRefresh(HAL_GetTick());
}
