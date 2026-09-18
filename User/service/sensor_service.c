#include "sensor_service.h"
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
#define ADC_OFFLINE_TIMEOUT_MS   3200U
#define DHT22_UPDATE_PERIOD_MS   2000U
#define CO2_UPDATE_PERIOD_MS     1000U
#define CO2_READ_TIMEOUT_MS       800U
#define CO2_OFFLINE_TIMEOUT_MS   3000U
#define SENSOR_STATUS_PERIOD_MS   500U
#define OLED_UPDATE_PERIOD_MS     200U
#define REPORT_PERIOD_MS         1000U
#define SENSOR_FONT_SIZE          16U

volatile SensorData_t g_sensor_data;

static uint32_t adc_light_sum;
static uint32_t adc_soil_sum;
static uint8_t adc_sample_count;
static uint32_t last_adc_tick;
static uint32_t last_dht22_tick;
static uint32_t last_co2_tick;
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
    g_sensor_data.light_valid = 1U;
    g_sensor_data.soil_valid = 1U;
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
    uint8_t error = CO2_get_data(&co2_ppm, CO2_READ_TIMEOUT_MS);

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

static void OLED_ShowFloat1(uint8_t x, uint8_t y, float value, uint8_t int_len)
{
    uint8_t current_x = x;
    uint16_t value_x10;

    if (value < 0.0f)
    {
        OLED_ShowChar(current_x, y, '-', SENSOR_FONT_SIZE);
        current_x += 8U;
        value = -value;
    }

    value_x10 = (uint16_t)(value * 10.0f + 0.5f);
    OLED_ShowNum(current_x, y, value_x10 / 10U, int_len, SENSOR_FONT_SIZE);
    current_x = (uint8_t)(current_x + 8U * int_len);
    OLED_ShowChar(current_x, y, '.', SENSOR_FONT_SIZE);
    current_x += 8U;
    OLED_ShowChar(current_x, y, (uint8_t)('0' + value_x10 % 10U), SENSOR_FONT_SIZE);
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
    last_co2_tick = now - CO2_UPDATE_PERIOD_MS;
    last_status_tick = now;
    last_oled_tick = now;
    last_report_tick = now;
}

void Sensor_ServiceTask(uint32_t now)
{
    if ((now - last_adc_tick) >= ADC_UPDATE_PERIOD_MS)
    {
        last_adc_tick = now;
        Sensor_UpdateADC(now);
    }

    if ((now - last_co2_tick) >= CO2_UPDATE_PERIOD_MS)
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

void Sensor_ServiceDisplay(void)
{
    static uint8_t initialized;
    static float last_temperature;
    static float last_humidity;
    static uint16_t last_co2_ppm;
    static uint16_t last_light_lux;
    static uint8_t last_soil_level;
    static uint8_t last_dht22_valid;
    static uint8_t last_co2_valid;
    static uint8_t last_light_valid;
    static uint8_t last_soil_valid;

    if (!initialized ||
        last_temperature != g_sensor_data.temperature ||
        last_humidity != g_sensor_data.humidity ||
        last_dht22_valid != g_sensor_data.dht22_valid)
    {
        OLED_Fill_Area(0, 0, 127, 15, 0U);
        OLED_ShowString(0, 0, "T:", SENSOR_FONT_SIZE);
        if (g_sensor_data.dht22_valid)
            OLED_ShowFloat1(16, 0, g_sensor_data.temperature, 2U);
        else
            OLED_ShowString(16, 0, "--.-", SENSOR_FONT_SIZE);

        OLED_ShowString(60, 0, "H:", SENSOR_FONT_SIZE);
        if (g_sensor_data.dht22_valid)
            OLED_ShowFloat1(76, 0, g_sensor_data.humidity, 2U);
        else
            OLED_ShowString(76, 0, "--.-", SENSOR_FONT_SIZE);

        OLED_RefreshPages(0U, 1U);
        last_temperature = g_sensor_data.temperature;
        last_humidity = g_sensor_data.humidity;
        last_dht22_valid = g_sensor_data.dht22_valid;
    }

    if (!initialized ||
        last_co2_ppm != g_sensor_data.co2_ppm ||
        last_co2_valid != g_sensor_data.co2_valid)
    {
        OLED_Fill_Area(0, 16, 127, 31, 0U);
        if (g_sensor_data.co2_valid)
        {
            OLED_ShowString(0, 16, "CO2:", SENSOR_FONT_SIZE);
            OLED_ShowNum(32, 16, g_sensor_data.co2_ppm, 4U, SENSOR_FONT_SIZE);
            OLED_ShowString(68, 16, "ppm", SENSOR_FONT_SIZE);
        }
        else
        {
            OLED_ShowString(0, 16, "CO2:no data", SENSOR_FONT_SIZE);
        }
        OLED_RefreshPages(2U, 3U);
        last_co2_ppm = g_sensor_data.co2_ppm;
        last_co2_valid = g_sensor_data.co2_valid;
    }

    if (!initialized ||
        last_light_lux != g_sensor_data.light_lux ||
        last_light_valid != g_sensor_data.light_valid)
    {
        OLED_Fill_Area(0, 32, 127, 47, 0U);
        OLED_ShowString(0, 32, "Lux:", SENSOR_FONT_SIZE);
        if (g_sensor_data.light_valid)
            OLED_ShowNum(32, 32, g_sensor_data.light_lux, 3U, SENSOR_FONT_SIZE);
        else
            OLED_ShowString(32, 32, "---", SENSOR_FONT_SIZE);
        OLED_RefreshPages(4U, 5U);
        last_light_lux = g_sensor_data.light_lux;
        last_light_valid = g_sensor_data.light_valid;
    }

    if (!initialized ||
        last_soil_level != g_sensor_data.soil_level ||
        last_soil_valid != g_sensor_data.soil_valid)
    {
        OLED_Fill_Area(0, 48, 127, 63, 0U);
        OLED_ShowString(0, 48, "Soil:", SENSOR_FONT_SIZE);
        if (g_sensor_data.soil_valid)
            OLED_ShowNum(40, 48, g_sensor_data.soil_level, 1U, SENSOR_FONT_SIZE);
        else
            OLED_ShowChar(40, 48, '-', SENSOR_FONT_SIZE);
        OLED_RefreshPages(6U, 7U);
        last_soil_level = g_sensor_data.soil_level;
        last_soil_valid = g_sensor_data.soil_valid;
    }

    initialized = 1U;
}
