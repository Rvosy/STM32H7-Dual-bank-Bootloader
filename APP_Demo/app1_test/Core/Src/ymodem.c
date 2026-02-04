/**
  ******************************************************************************
  * @file           : ymodem.c
  * @brief          : YMODEM 协议接收模块实现 (平台无关)
  * @description    : 使用环形缓冲区实现 YMODEM 接收
  ******************************************************************************
  */

#include "ymodem.h"
#include "ymodem_port.h"
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * 私有常量
 *============================================================================*/

#define PACKET_HEADER_SIZE      3       /* SOH/STX + SeqNo + ~SeqNo */
#define PACKET_CRC_SIZE         2       /* CRC16 */
#define PACKET_OVERHEAD         (PACKET_HEADER_SIZE + PACKET_CRC_SIZE)

#define MAX_RETRY               10      /* 最大重试次数 */
#define INTER_CHAR_TIMEOUT      100     /* 字符间超时 (ms) */

/*============================================================================
 * 私有函数
 *============================================================================*/

/**
 * @brief  计算 CRC16-CCITT
 */
static uint16_t calc_crc16(const uint8_t* data, uint32_t len)
{
    uint16_t crc = 0;
    
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

/**
 * @brief  发送单个字符
 */
static void send_char(uint8_t ch)
{
    YmodemPort_SendByte(ch);
}

/**
 * @brief  等待指定数量的字节
 * @retval 0=成功, -1=超时
 */
static int wait_for_bytes(lwrb_t* rb, uint32_t count, uint32_t timeout_ms)
{
    uint32_t start = YmodemPort_GetTick();

    while (lwrb_get_full(rb) < count) {
        if ((YmodemPort_GetTick() - start) > timeout_ms) {
            return -1;
        }
        // 可以加一点点让步，避免死等占满CPU
        // YmodemPort_Delay(1);
        // 或者在这里喂狗
    }
    return 0;
}


/**
 * @brief  读取一个字节
 * @retval 0=成功, -1=超时
 */
static int read_byte(lwrb_t* rb, uint8_t* out, uint32_t timeout_ms)
{
    if (wait_for_bytes(rb, 1, timeout_ms) != 0) {
        return -1;
    }
    
    return (lwrb_read(rb, out, 1) == 1) ? 0 : -1;
}

/**
 * @brief  解析文件信息包 (packet 0)
 */
static int parse_file_info(const uint8_t* data, uint32_t len, 
                           char* filename, uint32_t* filesize)
{
    /* 文件名 (以 0 结尾) */
    const char* name = (const char*)data;
    uint32_t name_len = strlen(name);
    
    if (name_len == 0) {
        /* 空包表示传输结束 */
        filename[0] = '\0';
        *filesize = 0;
        return 0;
    }
    
    strncpy(filename, name, 127);
    filename[127] = '\0';
    
    /* 文件大小 (紧跟文件名之后，ASCII 格式) */
    if (name_len + 1 < len) {
        *filesize = strtoul((const char*)(data + name_len + 1), NULL, 10);
    } else {
        *filesize = 0;
    }
    
    return 0;
}

/*============================================================================
 * 公共函数实现
 *============================================================================*/

void Ymodem_Cancel(void)
{
    /* 发送多个 CAN 取消传输 */
    for (int i = 0; i < 5; i++) {
        send_char(YMODEM_CAN);
        YmodemPort_Delay(10);
    }
}

int Ymodem_Receive(lwrb_t* rb, const ymodem_cb_t* cb, uint32_t timeout_ms)
{
    static uint8_t packet_buf[YMODEM_PACKET_1K + PACKET_OVERHEAD];
    char filename[128];
    uint32_t filesize = 0;
    uint32_t received_bytes = 0;
    uint8_t expected_seq = 0;
    int retry_count = 0;
    int state = 0;  /* 0=等待开始, 1=接收数据, 2=等待结束 */
    
    YmodemPort_Log("[YMODEM] Waiting for sender (send 'C')...\r\n");
    
    // 丢弃升级命令后的残留数据，使用库API，不要碰指针
    

    // 关键：如果你DMA→lwrb_write是通过 old_pos 增量搬运的
    // 那你还需要把 old_pos 一起清零，否则会把一段旧数据“再搬一次”或出现错位。
    // 例如提供一个函数：UartDmaRx_ResetPos(); 在这里调用你自己实现：old_pos = 当前DMA pos 或 0

    send_char(YMODEM_C);
    // uint32_t t0 = YmodemPort_GetTick();
    // uint8_t debug_first = 0;
    // while (YmodemPort_GetTick() - t0 < 2000) {
    //     uint8_t b;
    //     if (lwrb_read(rb, &b, 1) == 1) {
    //         // 把首字节值打出来（注意：别用printf走同一串口，先点灯或存变量）
    //         debug_first = b;
    //         YmodemPort_Log("[YMODEM] First byte received: 0x%02X\r\n", debug_first);
    //         break;
    //     }
    // }

    
    while (1) {
        uint8_t header;
        uint32_t packet_size;
        
        /* 读取包头 */
        if (read_byte(rb, &header, timeout_ms) != 0) {
            /* 超时 */
            if (state == 0) {
                /* 还在等待开始，重发 'C' */
                retry_count++;
                if (retry_count >= MAX_RETRY) {
                    YmodemPort_Log("[YMODEM] Timeout waiting for sender\r\n");
                    if (cb->on_error) cb->on_error(YMODEM_ERR_TIMEOUT);
                    return YMODEM_ERR_TIMEOUT;
                }
                send_char(YMODEM_C);
                continue;
            } else {
                /* 数据传输中超时 */
                YmodemPort_Log("[YMODEM] Timeout during transfer\r\n");
                Ymodem_Cancel();
                if (cb->on_error) cb->on_error(YMODEM_ERR_TIMEOUT);
                return YMODEM_ERR_TIMEOUT;
            }
        }
        
        retry_count = 0;  /* 收到数据，重置重试计数 */
        
        /* 解析包头 */
        switch (header) {
            case YMODEM_SOH:
                packet_size = YMODEM_PACKET_128;
                break;
                
            case YMODEM_STX:
                packet_size = YMODEM_PACKET_1K;
                break;
                
            case YMODEM_EOT:
                /* 传输结束 */
                if (state == 1) {
                    /* 第一个 EOT，发送 NAK */
                    send_char(YMODEM_NAK);
                    state = 2;
                    continue;
                } else if (state == 2) {
                    /* 第二个 EOT，发送 ACK + C */
                    send_char(YMODEM_ACK);
                    send_char(YMODEM_C);
                    
                    /* 重置期望序列号，等待结束包 (packet 0，空文件名) */
                    expected_seq = 0;
                    continue;
                }
                break;
                
            case YMODEM_CAN:
                /* 发送方取消 */
                YmodemPort_Log("[YMODEM] Transfer cancelled by sender\r\n");
                if (cb->on_error) cb->on_error(YMODEM_ERR_CANCEL);
                return YMODEM_ERR_CANCEL;
                
            default:
                /* 未知字符，忽略 */
                continue;
        }
        
        /* 读取完整数据包 */
        uint32_t total_len = 2 + packet_size + 2;  /* SeqNo + ~SeqNo + Data + CRC */
        if (wait_for_bytes(rb, total_len, INTER_CHAR_TIMEOUT * 10) != 0) {
            YmodemPort_Log("[YMODEM] Incomplete packet\r\n");
            send_char(YMODEM_NAK);
            continue;
        }
        
        /* 读取数据包 */
        packet_buf[0] = header;
        lwrb_read(rb, &packet_buf[1], total_len);
        
        uint8_t seq_no = packet_buf[1];
        uint8_t seq_comp = packet_buf[2];
        uint8_t* data = &packet_buf[3];
        uint16_t recv_crc = (packet_buf[3 + packet_size] << 8) | packet_buf[3 + packet_size + 1];
        
        /* 验证序列号补码 */
        if ((seq_no ^ seq_comp) != 0xFF) {
            send_char(YMODEM_NAK);
            continue;
        }
        
        /* 验证 CRC */
        uint16_t calc_crc = calc_crc16(data, packet_size);
        if (calc_crc != recv_crc) {
            send_char(YMODEM_NAK);
            continue;
        }
        
        /* 验证序列号 */
        if (seq_no != expected_seq) {
            if (seq_no == (uint8_t)(expected_seq - 1)) {
                /* 重复包，发送 ACK 但不处理 */
                send_char(YMODEM_ACK);
                continue;
            }
            YmodemPort_Log("[YMODEM] Sequence error (expect=%d, recv=%d)\r\n", expected_seq, seq_no);
            Ymodem_Cancel();
            if (cb->on_error) cb->on_error(YMODEM_ERR_SEQ);
            return YMODEM_ERR_SEQ;
        }
        
        /* 处理数据包 */
        if (state == 0 && seq_no == 0) {
            /* Packet 0: 文件信息 */
            parse_file_info(data, packet_size, filename, &filesize);
            
            if (filename[0] == '\0') {
                /* 空文件名 = 传输完全结束 */
                YmodemPort_Log("[YMODEM] All transfers complete\r\n");
                send_char(YMODEM_ACK);
                return YMODEM_OK;
            }
            
            YmodemPort_Log("[YMODEM] File: %s, Size: %lu bytes\r\n", filename, (unsigned long)filesize);
            
            /* 调用开始回调 */
            if (cb->on_begin) {
                if (cb->on_begin(filename, filesize) != 0) {
                    YmodemPort_Log("[YMODEM] Callback rejected transfer\r\n");
                    Ymodem_Cancel();
                    return YMODEM_ERR_CALLBACK;
                }
            }
            
            state = 1;
            expected_seq = 1;
            received_bytes = 0;
            
            send_char(YMODEM_ACK);
            send_char(YMODEM_C);  /* 请求数据包 */
            continue;
        }
        
        if (state >= 1 && seq_no != 0) {
            /* 数据包 */
            uint32_t data_len = packet_size;
            
            /* 如果知道文件大小，裁剪最后一个包 */
            if (filesize > 0 && received_bytes + data_len > filesize) {
                data_len = filesize - received_bytes;
            }
            
            /* 调用数据回调 */
            if (cb->on_data) {
                if (cb->on_data(data, data_len) != 0) {
                    YmodemPort_Log("[YMODEM] Data callback error\r\n");
                    Ymodem_Cancel();
                    return YMODEM_ERR_CALLBACK;
                }
            }
            
            received_bytes += data_len;
            expected_seq = (expected_seq + 1) & 0xFF;
            
            /* 进度显示 */
            if (filesize > 0) {
                YmodemPort_Log("\r[YMODEM] Progress: %lu/%lu (%lu%%)", 
                       (unsigned long)received_bytes, (unsigned long)filesize,
                       (unsigned long)(received_bytes * 100 / filesize));
            }
            
            send_char(YMODEM_ACK);
            continue;
        }
        
        if (state == 2 && seq_no == 0) {
            /* 结束时的 packet 0 (空文件名) */
            parse_file_info(data, packet_size, filename, &filesize);
            
            if (filename[0] == '\0') {
                /* 传输完成 */
                YmodemPort_Log("\r\n[YMODEM] Transfer complete: %lu bytes\r\n", (unsigned long)received_bytes);
                
                if (cb->on_end) {
                    cb->on_end();
                }
                
                send_char(YMODEM_ACK);
                return YMODEM_OK;
            }
        }
        
        /* 默认发送 ACK */
        send_char(YMODEM_ACK);
    }
}