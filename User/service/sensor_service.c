#include "sensor_service.h"
#include "adc.h"
#include "CO2.h"
#include "dht22.h"
#include "light.h"
#include "soil.h"
#include "oled.h"
#include "app_main.h"
#include <stdio.h>
#include <string.h>

#define ADC_FILTER_SAMPLES       16U
#define ADC_UPDATE_PERIOD_MS     100U
#define DHT22_UPDATE_PERIOD_MS   2000U
#define CO2_OFFLINE_TIMEOUT_MS   3000U
#define SENSOR_STATUS_PERIOD_MS   500U
#define OLED_UPDATE_PERIOD_MS     500U
#define REPORT_PERIOD_MS         1000U

volatile SensorData_t g_sensor_data;

static uint32_t adc_light_sum;
static uint32_t adc_soil_sum;
static uint8_t adc_sample_count;
static uint32_t last_adc_tick;
static uint32_t last_dht22_tick;
static uint32_t last_status_tick;
static uint32_t last_oled_tick;
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
    g_sensor_data.light_valid = (g_sensor_data.light_adc <= 4095U);
    g_sensor_data.soil_valid = (g_sensor_data.soil_adc <= 4095U);
    g_sensor_data.adc_update_tick = now;

    adc_light_sum = 0;
    adc_soil_sum = 0;
    adc_sample_count = 0;
}

static void Sensor_UpdateDHT22(uint32_t now)
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint8_t error = DHT22_ReadData(&temperature, &humidity);

    g_sensor_data.dht22_error = error;
    if (error == 0U)
    {
        g_sensor_data.temperature = temperature;
        g_sensor_data.humidity = humidity;
        g_sensor_data.dht22_valid = 1U;
        g_sensor_data.dht22_update_tick = now;
    }
    else
    {
        g_sensor_data.dht22_valid = 0U;
    }
}

static void Sensor_UpdateCO2(uint32_t now)
{
    uint8_t frame[CO2_RX_BUF_SIZE];
    uint16_t co2_ppm = 0;
    uint8_t error;

    if (!CO2_TakeFrame(frame))
    {
        return;
    }

    error = CO2_ParseFrame(frame, &co2_ppm);
    g_sensor_data.co2_error = error;
    if (error == 0U)
    {
        g_sensor_data.co2_ppm = co2_ppm;
        g_sensor_data.co2_valid = 1U;
        g_sensor_data.co2_update_tick = now;
    }
    else
    {
        g_sensor_data.co2_valid = 0U;
    }
}

static void Sensor_UpdateValidity(uint32_t now)
{
    if ((now - g_sensor_data.adc_update_tick) > (ADC_UPDATE_PERIOD_MS * 5U))
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
    memset((void *)&g_sensor_data, 0, sizeof(g_sensor_data));
    g_sensor_data.dht22_error = 1U;
    g_sensor_data.co2_error = 1U;
    adc_light_sum = 0;
    adc_soil_sum = 0;
    adc_sample_count = 0;
    last_adc_tick = HAL_GetTick();
    last_dht22_tick = last_adc_tick - DHT22_UPDATE_PERIOD_MS;
    last_status_tick = last_adc_tick;
    last_oled_tick = last_adc_tick;
    last_report_tick = last_adc_tick;
}

void Sensor_ServiceTask(uint32_t now)
{
    Sensor_UpdateCO2(now);

    if ((now - last_adc_tick) >= ADC_UPDATE_PERIOD_MS)
    {
        last_adc_tick = now;
        Sensor_UpdateADC(now);
    }

    if ((now - last_dht22_tick) >= DHT22_UPDATE_PERIOD_MS)
    {
        last_dht22_tick = now;
        Sensor_UpdateDHT22(now);
    }

    if ((now - last_status_tick) >= SENSOR_STATUS_PERIOD_MS)
    {
        last_status_tick = now;
        Sensor_UpdateValidity(now);
    }

    if ((now - last_oled_tick) >= OLED_UPDATE_PERIOD_MS)
    {
        last_oled_tick = now;
        Sensor_ServiceDisplay();
    }

    if ((now - last_report_tick) >= REPORT_PERIOD_MS)
    {
        last_report_tick = now;
        Sensor_ServiceReport();
    }
}

void Sensor_ServiceReport(void)
{
    int temperature_c10 = (int)(g_sensor_data.temperature * 10.0f);
    int humidity_c10 = (int)(g_sensor_data.humidity * 10.0f);

    printf("ADC: light=%u soil=%u\r\n",
           (unsigned int)g_sensor_data.light_adc,
           (unsigned int)g_sensor_data.soil_adc);

    if (g_sensor_data.dht22_valid)
    {
        printf("DHT22: temp=%d.%d C humi=%d.%d %%RH\r\n",
               temperature_c10 / 10, temperature_c10 < 0 ? -temperature_c10 % 10 : temperature_c10 % 10,
               humidity_c10 / 10, humidity_c10 % 10);
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
        printf("CO2: OFFLINE ERR(%u)\r\n", (unsigned int)g_sensor_data.co2_error);
    }

    printf("ENV: lux=%u soil=%u status=DHT:%s CO2:%s ADC:%s\r\n",
           (unsigned int)g_sensor_data.light_lux,
           (unsigned int)g_sensor_data.soil_level,
           g_sensor_data.dht22_valid ? "OK" : "ERR",
           g_sensor_data.co2_valid ? "OK" : "ERR",
           (g_sensor_data.light_valid && g_sensor_data.soil_valid) ? "OK" : "ERR");
}

void Sensor_ServiceDisplay(void)
{
    char line[48];
    int temperature_c10 = (int)(g_sensor_data.temperature * 10.0f);
    int humidity_c10 = (int)(g_sensor_data.humidity * 10.0f);

    OLED_Clear();

    if (g_sensor_data.dht22_valid)
    {
        (void)snprintf(line, sizeof(line), "T:%d.%dC H:%d.%d%%",
                       temperature_c10 / 10, temperature_c10 < 0 ? -temperature_c10 % 10 : temperature_c10 % 10,
                       humidity_c10 / 10, humidity_c10 % 10);
    }
    else
    {
        (void)snprintf(line, sizeof(line), "T:--.-C H:--.-%%");
    }
    OLED_ShowString(0, 0, line, 8);

    if (g_sensor_data.co2_valid)
    {
        (void)snprintf(line, sizeof(line), "CO2:%uppm", (unsigned int)g_sensor_data.co2_ppm);
    }
    else
    {
        (void)snprintf(line, sizeof(line), "CO2:OFFLINE");
    }
    OLED_ShowString(0, 16, line, 8);

    (void)snprintf(line, sizeof(line), "L:%u S:%u",
                   (unsigned int)g_sensor_data.light_lux,
                   (unsigned int)g_sensor_data.soil_level);
    OLED_ShowString(0, 32, line, 8);

    (void)snprintf(line, sizeof(line), "A:%s D:%s C:%s",
                   (g_sensor_data.light_valid && g_sensor_data.soil_valid) ? "OK" : "ERR",
                   g_sensor_data.dht22_valid ? "OK" : "ERR",
                   g_sensor_data.co2_valid ? "OK" : "ERR");
    OLED_ShowString(0, 48, line, 8);
}
