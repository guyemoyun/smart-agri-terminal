#include "co2_parser.h"
#include <string.h>

void CO2Parser_Init(CO2Parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
}

uint8_t CO2Parser_Feed(CO2Parser_t *parser, uint8_t byte, uint16_t *ppm)
{
    uint8_t sum = 0U;
    if (parser->length == 0U && byte != 0x2CU) return 0U;
    parser->bytes[parser->length++] = byte;
    if (parser->length < CO2_FRAME_SIZE) return 0U;
    for (uint8_t i = 0U; i < 5U; ++i) sum = (uint8_t)(sum + parser->bytes[i]);
    if (sum == parser->bytes[5])
    {
        *ppm = (uint16_t)(((uint16_t)parser->bytes[1] << 8U) | parser->bytes[2]);
        parser->length = 0U;
        return 1U;
    }
    /* 错位或坏帧不能丢弃后面潜在的帧头，用窗口中的下一个帧头重同步。 */
    for (uint8_t i = 1U; i < CO2_FRAME_SIZE; ++i)
    {
        if (parser->bytes[i] == 0x2CU)
        {
            parser->length = (uint8_t)(CO2_FRAME_SIZE - i);
            memmove(parser->bytes, &parser->bytes[i], parser->length);
            return 2U;
        }
    }
    parser->length = 0U;
    return 2U;
}
