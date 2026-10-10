#include "CO2.h"
#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart2;
static uint32_t tick,primask;
static unsigned int failures,restarts,aborts,delay_calls;
static HAL_StatusTypeDef receive_result=HAL_OK;
#define CHECK(x) do { if (!(x)) { ++failures; printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
uint32_t HAL_GetTick(void){return tick;}
void HAL_Delay(uint32_t delay){tick+=delay;++delay_calls;}
uint32_t __get_PRIMASK(void){return primask;}
void __disable_irq(void){primask=1;}
void __set_PRIMASK(uint32_t value){primask=value;}
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *uart,uint8_t *buffer,uint16_t size)
{
    CHECK(uart==&huart2 && buffer==co2_buffer && size==6);++restarts;return receive_result;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{CHECK(uart==&huart2);++aborts;return HAL_OK;}
static void Feed(const uint8_t *bytes,uint8_t length)
{memcpy(co2_buffer,bytes,length);CO2_UART_Callback(length);}
int main(void)
{
    uint16_t ppm=0xFFFF;
    const uint8_t valid[]={0x2C,3,0xE8,0,0,0x17};
    CO2_UART_Receive_Start();
    CHECK(CO2_TryRead(&ppm)==CO2_READ_NO_DATA && delay_calls==0 && ppm==0xFFFF);
    Feed(valid,2);CHECK(CO2_TryRead(&ppm)==CO2_READ_NO_DATA);
    Feed(valid+2,4);CHECK(CO2_TryRead(&ppm)==CO2_READ_OK && ppm==1000);
    Feed(valid,6);memset(co2_buffer,0,sizeof(co2_buffer));
    CHECK(CO2_TryRead(&ppm)==CO2_READ_OK && ppm==1000);
    for(unsigned int i=0;i<11;++i)Feed(valid,6);
    CHECK(CO2_TryRead(&ppm)==CO2_READ_OVERFLOW);
    CHECK(CO2_TryRead(&ppm)==CO2_READ_NO_DATA);
    Feed(valid,6);CHECK(CO2_TryRead(&ppm)==CO2_READ_OK);
    primask=1;Feed(valid,6);CHECK(CO2_TryRead(&ppm)==CO2_READ_OK && primask==1);
    primask=0;
    CO2_UART_Error();tick=100;
    CHECK(CO2_TryRead(&ppm)==CO2_READ_UART_ERROR && aborts==1);
    Feed(valid,6);CHECK(CO2_TryRead(&ppm)==CO2_READ_OK);
    receive_result=HAL_ERROR;CO2_UART_Error();tick=200;
    CHECK(CO2_TryRead(&ppm)==CO2_READ_UART_ERROR && aborts==2);
    receive_result=HAL_OK;tick=300;
    CHECK(CO2_TryRead(&ppm)==CO2_READ_UART_ERROR && aborts==3);
    Feed(valid,6);CHECK(CO2_TryRead(&ppm)==CO2_READ_OK && delay_calls==0);
    CHECK(CO2_TryRead(0)==CO2_READ_ARGUMENT);
    printf("co2 ring/recovery tests: %u assertions failed, %u receive restarts\n",failures,restarts);
    return failures ? 1:0;
}
