#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t light_adc;
    uint16_t soil_adc;
    uint16_t light_lux;
    uint8_t soil_level;

    float temperature;
    float humidity;
    uint16_t co2_ppm;

    uint8_t light_valid;
    uint8_t soil_valid;
    uint8_t dht22_valid;
    uint8_t co2_valid;

    uint8_t dht22_error;
    uint8_t co2_error;

    uint32_t adc_update_tick;
    uint32_t dht22_update_tick;
    uint32_t co2_update_tick;
} SensorData_t;

extern volatile SensorData_t g_sensor_data;

void Sensor_ServiceInit(void);
void Sensor_ServiceTask(uint32_t now);
void Sensor_ServiceReport(void);
void Sensor_ServiceDisplay(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_SERVICE_H */
