#ifndef TERMINAL_TELEMETRY_H
#define TERMINAL_TELEMETRY_H
#include <stdint.h>
#include <stddef.h>
#include "sensor_service.h"
#include "control_service.h"

#define TELEMETRY_VERSION 2U
#define TELEMETRY_PAYLOAD_SIZE 24U
#define TELEMETRY_FRAME_SIZE 34U
uint16_t Terminal_Crc16(const uint8_t *data, size_t length);
/* 只编码已获取的快照；返回长度，容量不足或指针为空则返回 0。 */
size_t Terminal_EncodeTelemetry(uint8_t *out, size_t capacity, uint16_t node,
    uint16_t sequence, const SensorData_t *sensor, const ControlSnapshot_t *control, uint32_t now);
#endif
