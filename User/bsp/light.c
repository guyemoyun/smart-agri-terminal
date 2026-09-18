#include "light.h"

#define EPSILON (1e-4f)

typedef struct
{
    uint32_t ohm;
    uint16_t lux;
} PhotoRes_TypeDef;

static const PhotoRes_TypeDef GL5528[] =
{
    {100000, 0}, {70000, 1}, {50000, 1}, {40000, 1},
    {30000, 2}, {20000, 4}, {15000, 5}, {10000, 10},
    {7000, 17}, {5000, 29}, {4000, 45}, {3000, 68},
    {2000, 124}, {1000, 350}
};

uint16_t GetLuxFromAdc(uint16_t adc_value)
{
    float voltage = (float)adc_value * 3.3f / 4096.0f;
    uint32_t resistance;
    uint16_t lux = 0;

    if ((3.3f - voltage) < EPSILON)
    {
        return 0;
    }

    resistance = (uint32_t)(10.0f * voltage * 1000.0f / (3.3f - voltage));
    for (uint32_t i = 0; i < (sizeof(GL5528) / sizeof(GL5528[0])); i++)
    {
        lux = GL5528[i].lux;
        if (resistance >= GL5528[i].ohm)
        {
            break;
        }
    }
    return lux;
}

uint16_t GetLux(void)
{
    extern volatile uint16_t adc_values[2];
    return GetLuxFromAdc(adc_values[0]);
}
