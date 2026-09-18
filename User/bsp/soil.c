#include "soil.h"

#define EPSILON (1e-4f)

typedef struct
{
    uint32_t res;
    uint8_t humi;
} Soil_TypeDef;

static const Soil_TypeDef Soil[] =
{
    {100000, 1}, {50000, 2}, {20000, 3}, {10000, 4}
};

uint8_t GetSoilHumidityFromAdc(uint16_t adc_value)
{
    float voltage = (float)adc_value * 3.3f / 4096.0f;
    uint32_t resistance;
    uint8_t humi = 0;

    if ((3.3f - voltage) < EPSILON)
    {
        return 1;
    }

    resistance = (uint32_t)(10.0f * voltage * 1000.0f / (3.3f - voltage));
    for (uint32_t i = 0; i < (sizeof(Soil) / sizeof(Soil[0])); i++)
    {
        humi = Soil[i].humi;
        if (resistance >= Soil[i].res)
        {
            break;
        }
    }
    return humi;
}

uint8_t GetSoilHumidity(void)
{
    extern volatile uint16_t adc_values[2];
    return GetSoilHumidityFromAdc(adc_values[1]);
}
