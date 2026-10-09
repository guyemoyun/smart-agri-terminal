#ifndef LORA_SERVICE_H
#define LORA_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Lora_ServiceInit(void);
void Lora_ServiceTask(uint32_t now);

#ifdef __cplusplus
}
#endif

#endif /* LORA_SERVICE_H */
