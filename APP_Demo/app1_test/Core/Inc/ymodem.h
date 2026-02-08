/**
  ******************************************************************************
  * @file           : ymodem.h
  * @brief          : YMODEM 协议接收模块 (平台无关)
  * @description    : 提供 YMODEM 协议接收功能，使用环形缓冲区
  * @note           : 需要实现 ymodem_port.h 中的平台接口
  ******************************************************************************
  */

#ifndef __YMODEM_H
#define __YMODEM_H

#include <stdint.h>
#include "lwrb.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

/* YMODEM 控制字符 */
#define YMODEM_SOH      0x01    /* 128 字节数据包头 */
#define YMODEM_STX      0x02    /* 1024 字节数据包头 */
#define YMODEM_EOT      0x04    /* 传输结束 */
#define YMODEM_ACK      0x06    /* 确认 */
#define YMODEM_NAK      0x15    /* 否定确认 */
#define YMODEM_CAN      0x18    /* 取消传输 */
#define YMODEM_C        0x43    /* 'C' - 请求 CRC 模式 */

/* 数据包大小 */
#define YMODEM_PACKET_128   128
#define YMODEM_PACKET_1K    1024

/* 错误码 */
#define YMODEM_OK               0
#define YMODEM_ERR_TIMEOUT      -1
#define YMODEM_ERR_CANCEL       -2
#define YMODEM_ERR_CRC          -3
#define YMODEM_ERR_SEQ          -4
#define YMODEM_ERR_CALLBACK     -5
#define YMODEM_ERR_TOO_LARGE    -6
#define YMODEM_ERR_PARAM        -7

/*============================================================================
 * 回调函数类型
 *============================================================================*/

typedef struct {
    /**
     * @brief  传输开始回调
     * @param  name: 文件名
     * @param  size: 文件大小
     * @retval 0=继续, <0=取消
     */
    int (*on_begin)(const char* name, uint32_t size);
    
    /**
     * @brief  数据接收回调
     * @param  data: 数据指针
     * @param  len: 数据长度
     * @retval 0=继续, <0=取消
     */
    int (*on_data)(const uint8_t* data, uint32_t len);
    
    /**
     * @brief  传输结束回调
     * @retval 0=成功, <0=失败
     */
    int (*on_end)(void);
    
    /**
     * @brief  错误回调
     * @param  code: 错误码
     */
    void (*on_error)(int code);
} ymodem_cb_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  YMODEM 接收
 * @param  rb: 环形缓冲区 (需要提前初始化并启动 DMA)
 * @param  cb: 回调函数
 * @param  timeout_ms: 超时时间 (毫秒)
 * @retval 0=成功, <0=错误码
 */
int Ymodem_Receive(lwrb_t* rb, const ymodem_cb_t* cb, uint32_t timeout_ms);

/**
 * @brief  取消 YMODEM 传输
 */
void Ymodem_Cancel(void);

#endif /* __YMODEM_H */

