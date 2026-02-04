/**
  ******************************************************************************
  * @file           : ymodem_port.h
  * @brief          : YMODEM 平台抽象层接口
  * @description    : 用户需要实现这些接口以适配不同平台
  ******************************************************************************
  */

#ifndef __YMODEM_PORT_H
#define __YMODEM_PORT_H

#include <stdint.h>

/*============================================================================
 * 平台接口 - 用户必须实现
 *============================================================================*/

/**
 * @brief  发送单个字节
 * @param  ch: 要发送的字节
 */
void YmodemPort_SendByte(uint8_t ch);

/**
 * @brief  获取当前时间戳 (毫秒)
 * @retval 当前时间戳
 */
uint32_t YmodemPort_GetTick(void);

/**
 * @brief  延时 (毫秒)
 * @param  ms: 延时时间
 */
void YmodemPort_Delay(uint32_t ms);

/**
 * @brief  更新环形缓冲区 head 位置 (从 DMA 计数器)
 * @param  rb: 环形缓冲区指针
 * @note   需要调用 RingBuf_UpdateHead_DMA(rb, DMA_COUNTER)
 */
void YmodemPort_UpdateRxHead(void* rb);

/**
 * @brief  刷新接收缓冲区的 Cache (可选)
 * @param  buf: 缓冲区地址
 * @param  size: 缓冲区大小
 * @note   用于有 D-Cache 的 MCU (如 Cortex-M7)
 *         无 Cache 的 MCU 可以留空实现
 */
void YmodemPort_InvalidateCache(void* buf, uint32_t size);

/*============================================================================
 * 日志输出 - 可选实现
 *============================================================================*/

/**
 * @brief  日志输出
 * @param  fmt: 格式化字符串
 * @note   可以实现为 printf 或留空
 */
void YmodemPort_Log(const char* fmt, ...);

#endif /* __YMODEM_PORT_H */
