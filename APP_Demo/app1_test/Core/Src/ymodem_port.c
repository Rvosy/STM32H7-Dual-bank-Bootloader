/**
  ******************************************************************************
  * @file           : ymodem_port.c
  * @brief          : YMODEM 平台抽象层实现 (STM32H7 + HAL)
  * @description    : 针对 STM32H7 平台的接口实现
  ******************************************************************************
  */

#include "ymodem_port.h"
#include "lwrb.h"
#include "usart.h"
#include <stdio.h>
#include <stdarg.h>

/*============================================================================
 * 平台接口实现
 *============================================================================*/

void YmodemPort_SendByte(uint8_t ch)
{
    HAL_UART_Transmit(&huart1, &ch, 1, HAL_MAX_DELAY);
}

uint32_t YmodemPort_GetTick(void)
{
    return HAL_GetTick();
}

void YmodemPort_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}

void YmodemPort_UpdateRxHead(void* rb)
{
    (void)rb;
    // do nothing: RxEventCallback already feeds lwrb via lwrb_write()
}


void YmodemPort_InvalidateCache(void* buf, uint32_t size)
{
    /* STM32H7 有 D-Cache，需要 Invalidate */
    SCB_InvalidateDCache_by_Addr((uint32_t*)((uint32_t)buf & ~31U), 
                                  (size + 31) & ~31U);
}

void YmodemPort_Log(const char* fmt, ...)
{
    va_list args;
    // va_start(args, fmt);
    // vprintf(fmt, args);
    // va_end(args);
}
