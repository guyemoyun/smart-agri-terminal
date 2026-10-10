#ifndef CO2_PARSER_H
#define CO2_PARSER_H
#include <stdint.h>
#define CO2_FRAME_SIZE 6U
typedef struct { uint8_t bytes[CO2_FRAME_SIZE], length; } CO2Parser_t;
void CO2Parser_Init(CO2Parser_t *parser);
/* 返回 1 为完整有效帧，0 为继续接收，2 为校验错误后重新寻找帧头。 */
uint8_t CO2Parser_Feed(CO2Parser_t *parser, uint8_t byte, uint16_t *ppm);
#endif
