/**
  ******************************************************************************
  * @file           : ringbuf.c
  * @brief          : 环形缓冲区模块实现
  ******************************************************************************
  */

#include "ringbuf.h"
#include <string.h>

/*============================================================================
 * 公共函数实现
 *============================================================================*/

void RingBuf_Init(ringbuf_t* rb, uint8_t* buf, uint32_t size)
{
    rb->buffer = buf;
    rb->size   = size;
    rb->head   = 0;
    rb->tail   = 0;
    rb->overflow = 0;
}

void RingBuf_Reset(ringbuf_t* rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->overflow = 0;
}

uint32_t RingBuf_Available(ringbuf_t* rb)
{
    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    
    if (head >= tail) {
        return head - tail;
    } else {
        return rb->size - tail + head;
    }
}

uint32_t RingBuf_Free(ringbuf_t* rb)
{
    return rb->size - RingBuf_Available(rb) - 1;
}

int RingBuf_IsEmpty(ringbuf_t* rb)
{
    return (rb->head == rb->tail);
}

int RingBuf_HasOverflow(ringbuf_t* rb)
{
    return rb->overflow;
}

void RingBuf_ClearOverflow(ringbuf_t* rb)
{
    rb->overflow = 0;
}

int RingBuf_Peek(ringbuf_t* rb, uint32_t offset, uint8_t* out)
{
    if (offset >= RingBuf_Available(rb)) {
        return -1;
    }
    
    uint32_t pos = (rb->tail + offset) % rb->size;
    *out = rb->buffer[pos];
    return 0;
}

int RingBuf_ReadByte(ringbuf_t* rb, uint8_t* out)
{
    if (RingBuf_IsEmpty(rb)) {
        return -1;
    }
    
    *out = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    return 0;
}

int RingBuf_GetChar(ringbuf_t* rb)
{
    if (RingBuf_IsEmpty(rb)) {
        return -1;
    }
    
    uint8_t ch = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    return (int)ch;
}

uint32_t RingBuf_Read(ringbuf_t* rb, uint8_t* buf, uint32_t len)
{
    uint32_t available = RingBuf_Available(rb);
    if (len > available) {
        len = available;
    }
    
    if (len == 0) {
        return 0;
    }
    
    /* 计算第一段长度 (从 tail 到 buffer 末尾) */
    uint32_t first_part = rb->size - rb->tail;
    if (first_part > len) {
        first_part = len;
    }
    
    /* 复制第一段 */
    memcpy(buf, &rb->buffer[rb->tail], first_part);
    
    /* 如果有第二段 (从 buffer 开头) */
    if (len > first_part) {
        memcpy(buf + first_part, rb->buffer, len - first_part);
    }
    
    rb->tail = (rb->tail + len) % rb->size;
    return len;
}

uint32_t RingBuf_Skip(ringbuf_t* rb, uint32_t len)
{
    uint32_t available = RingBuf_Available(rb);
    if (len > available) {
        len = available;
    }
    
    rb->tail = (rb->tail + len) % rb->size;
    return len;
}

int RingBuf_WriteByte(ringbuf_t* rb, uint8_t data)
{
    uint32_t next_head = (rb->head + 1) % rb->size;
    
    if (next_head == rb->tail) {
        /* 缓冲区满 */
        return -1;
    }
    
    rb->buffer[rb->head] = data;
    rb->head = next_head;
    return 0;
}

void RingBuf_UpdateHead_DMA(ringbuf_t* rb, uint32_t dma_remaining)
{
    /* DMA 的 NDTR 是剩余传输数，head = size - NDTR */
    uint32_t new_head = rb->size - dma_remaining;
    uint32_t old_head = rb->head;
    uint32_t tail = rb->tail;
    
    /* 计算 DMA 写入了多少字节 */
    uint32_t written;
    if (new_head >= old_head) {
        written = new_head - old_head;
    } else {
        written = rb->size - old_head + new_head;  /* 回绕 */
    }
    
    /* 计算更新前的空闲空间 */
    uint32_t free_space;
    if (old_head >= tail) {
        free_space = rb->size - (old_head - tail) - 1;
    } else {
        free_space = tail - old_head - 1;
    }
    
    /* 检测溢出: DMA 写入量超过空闲空间 */
    if (written > free_space) {
        rb->overflow = 1;
        /* 丢弃旧数据: 把 tail 推到 head 后面一点 */
        rb->tail = (new_head + 1) % rb->size;
    }
    
    rb->head = new_head;
}
