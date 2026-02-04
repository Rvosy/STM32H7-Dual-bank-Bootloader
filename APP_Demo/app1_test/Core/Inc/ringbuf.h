/**
  ******************************************************************************
  * @file           : ringbuf.h
  * @brief          : 环形缓冲区模块
  * @description    : 提供线程安全的环形缓冲区，用于串口 DMA 接收
  ******************************************************************************
  */

#ifndef __RINGBUF_H
#define __RINGBUF_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * 数据结构
 *============================================================================*/

typedef struct {
    uint8_t* buffer;            /* 缓冲区指针 */
    uint32_t size;              /* 缓冲区大小 (必须是 2 的幂) */
    volatile uint32_t head;     /* 写入位置 (DMA/ISR 更新) */
    volatile uint32_t tail;     /* 读取位置 (用户更新) */
    volatile uint8_t overflow;  /* 溢出标志 */
} ringbuf_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  初始化环形缓冲区
 * @param  rb: 环形缓冲区实例
 * @param  buf: 缓冲区内存
 * @param  size: 缓冲区大小 (必须是 2 的幂，如 256, 512, 1024)
 */
void RingBuf_Init(ringbuf_t* rb, uint8_t* buf, uint32_t size);

/**
 * @brief  重置环形缓冲区
 * @param  rb: 环形缓冲区实例
 */
void RingBuf_Reset(ringbuf_t* rb);

/**
 * @brief  获取可读数据长度
 * @param  rb: 环形缓冲区实例
 * @retval 可读字节数
 */
uint32_t RingBuf_Available(ringbuf_t* rb);

/**
 * @brief  获取剩余空间
 * @param  rb: 环形缓冲区实例
 * @retval 剩余字节数
 */
uint32_t RingBuf_Free(ringbuf_t* rb);

/**
 * @brief  检查缓冲区是否为空
 * @param  rb: 环形缓冲区实例
 * @retval 1=空, 0=非空
 */
int RingBuf_IsEmpty(ringbuf_t* rb);

/**
 * @brief  检查是否发生过溢出
 * @param  rb: 环形缓冲区实例
 * @retval 1=溢出, 0=正常
 */
int RingBuf_HasOverflow(ringbuf_t* rb);

/**
 * @brief  清除溢出标志
 * @param  rb: 环形缓冲区实例
 */
void RingBuf_ClearOverflow(ringbuf_t* rb);

/**
 * @brief  读取一个字节 (不移动 tail)
 * @param  rb: 环形缓冲区实例
 * @param  offset: 相对于 tail 的偏移
 * @param  out: 输出字节
 * @retval 0=成功, -1=无数据
 */
int RingBuf_Peek(ringbuf_t* rb, uint32_t offset, uint8_t* out);

/**
 * @brief  读取一个字节
 * @param  rb: 环形缓冲区实例
 * @param  out: 输出字节
 * @retval 0=成功, -1=无数据
 */
int RingBuf_ReadByte(ringbuf_t* rb, uint8_t* out);

/**
 * @brief  读取一个字符 (简化接口)
 * @param  rb: 环形缓冲区实例
 * @retval 读取的字符 (0-255), -1=无数据
 */
int RingBuf_GetChar(ringbuf_t* rb);

/**
 * @brief  读取多个字节
 * @param  rb: 环形缓冲区实例
 * @param  buf: 输出缓冲区
 * @param  len: 要读取的长度
 * @retval 实际读取的字节数
 */
uint32_t RingBuf_Read(ringbuf_t* rb, uint8_t* buf, uint32_t len);

/**
 * @brief  跳过指定字节数
 * @param  rb: 环形缓冲区实例
 * @param  len: 要跳过的字节数
 * @retval 实际跳过的字节数
 */
uint32_t RingBuf_Skip(ringbuf_t* rb, uint32_t len);

/**
 * @brief  写入一个字节 (用于非 DMA 模式)
 * @param  rb: 环形缓冲区实例
 * @param  data: 写入的字节
 * @retval 0=成功, -1=缓冲区满
 */
int RingBuf_WriteByte(ringbuf_t* rb, uint8_t data);

/**
 * @brief  更新 head 位置 (用于 DMA 模式)
 * @note   在 DMA 半传输/完成中断中调用
 * @param  rb: 环形缓冲区实例
 * @param  dma_remaining: DMA NDTR 寄存器值 (剩余传输数)
 */
void RingBuf_UpdateHead_DMA(ringbuf_t* rb, uint32_t dma_remaining);

#endif /* __RINGBUF_H */
