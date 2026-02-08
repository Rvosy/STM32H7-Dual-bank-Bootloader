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
#define MAX_PACKET_ERRORS       20      /* 新增：包错误最大次数，避免无限NAK */

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
    if (!data || !filename || !filesize) return -1;

    /* bounded 查找 '\0'，防止 strlen 越界 */
    uint32_t name_len = 0;
    while (name_len < len && data[name_len] != '\0') {
        name_len++;
    }

    if (name_len == 0) {
        /* 空文件名表示 batch 结束 */
        filename[0] = '\0';
        *filesize = 0;
        return 0;
    }

    if (name_len >= len) {
        /* 没有 '\0' 终止符，非法包 */
        return -1;
    }

    /* 复制文件名 */
    uint32_t copy_len = (name_len < 127) ? name_len : 127;
    memcpy(filename, data, copy_len);
    filename[copy_len] = '\0';

    /* 文件大小字段从 name 后的下一字节开始（ASCII），同样 bounded */
    *filesize = 0;
    uint32_t p = name_len + 1;
    if (p < len) {
        /* 只解析连续数字，避免 strtoul 扫过 len */
        uint32_t val = 0;
        uint32_t any = 0;
        while (p < len) {
            uint8_t c = data[p];
            if (c < '0' || c > '9') break;
            any = 1;
            val = val * 10u + (uint32_t)(c - '0');
            p++;
        }
        if (any) *filesize = val;
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

    if (!rb || !cb) {
        return YMODEM_ERR_PARAM;
    }

    char filename[128];
    uint32_t filesize = 0;
    uint32_t received_bytes = 0;
    uint8_t expected_seq = 0;
    int retry_count = 0;
    int packet_errs = 0; /* 新增 */
    int state = 0;  /* 0=等待开始, 1=接收数据, 2=等待结束 */

    YmodemPort_Log("[YMODEM] Waiting for sender (send 'C')...\r\n");
    send_char(YMODEM_C);

    while (1) {
        uint8_t header;
        uint32_t packet_size = 0;  /* 关键：初始化，避免未赋值 */
        uint8_t eot_handled = 0;

        /* 读取包头 */
        if (read_byte(rb, &header, timeout_ms) != 0) {
            if (state == 0) {
                retry_count++;
                if (retry_count >= MAX_RETRY) {
                    YmodemPort_Log("[YMODEM] Timeout waiting for sender\r\n");
                    if (cb->on_error) cb->on_error(YMODEM_ERR_TIMEOUT);
                    return YMODEM_ERR_TIMEOUT;
                }
                send_char(YMODEM_C);
                continue;
            } else {
                YmodemPort_Log("[YMODEM] Timeout during transfer\r\n");
                Ymodem_Cancel();
                if (cb->on_error) cb->on_error(YMODEM_ERR_TIMEOUT);
                return YMODEM_ERR_TIMEOUT;
            }
        }

        retry_count = 0;

        switch (header) {
            case YMODEM_SOH:
                packet_size = YMODEM_PACKET_128;
                break;

            case YMODEM_STX:
                packet_size = YMODEM_PACKET_1K;
                break;

            case YMODEM_EOT:
                /* 修复：state==0 时必须处理掉，不能落到读包体逻辑 */
                if (state == 0) {
                    continue; /* 忽略噪声/残留 */
                }
                if (state == 1) {
                    send_char(YMODEM_NAK);
                    state = 2;
                    eot_handled = 1;
                } else if (state == 2) {
                    send_char(YMODEM_ACK);
                    send_char(YMODEM_C);
                    expected_seq = 0;
                    eot_handled = 1;
                }
                if (eot_handled) continue;
                /* 其他状态不应该到这里 */
                continue;

            case YMODEM_CAN:
                /* 可选：更严格可以再读一个 CAN 确认，这里先保持你原逻辑 */
                YmodemPort_Log("[YMODEM] Transfer cancelled by sender\r\n");
                if (cb->on_error) cb->on_error(YMODEM_ERR_CANCEL);
                return YMODEM_ERR_CANCEL;

            default:
                continue;
        }

        /* 读包体：SeqNo + ~SeqNo + Data + CRC */
        uint32_t total_len = 2 + packet_size + 2;

        if (wait_for_bytes(rb, total_len, INTER_CHAR_TIMEOUT * 10) != 0) {
            YmodemPort_Log("[YMODEM] Incomplete packet\r\n");
            send_char(YMODEM_NAK);
            if (++packet_errs >= MAX_PACKET_ERRORS) {
                YmodemPort_Log("[YMODEM] Too many packet errors\r\n");
                Ymodem_Cancel();
                if (cb->on_error) cb->on_error(YMODEM_ERR_CRC);
                return YMODEM_ERR_CRC;
            }
            continue;
        }

        packet_buf[0] = header;

        /* 强校验：必须读满 */
        if (lwrb_read(rb, &packet_buf[1], total_len) != total_len) {
            send_char(YMODEM_NAK);
            if (++packet_errs >= MAX_PACKET_ERRORS) {
                Ymodem_Cancel();
                if (cb->on_error) cb->on_error(YMODEM_ERR_TIMEOUT);
                return YMODEM_ERR_TIMEOUT;
            }
            continue;
        }

        uint8_t seq_no = packet_buf[1];
        uint8_t seq_comp = packet_buf[2];
        uint8_t* data = &packet_buf[3];
        uint16_t recv_crc = ((uint16_t)packet_buf[3 + packet_size] << 8)
                          |  (uint16_t)packet_buf[3 + packet_size + 1];

        if ((uint8_t)(seq_no ^ seq_comp) != 0xFF) {
            send_char(YMODEM_NAK);
            if (++packet_errs >= MAX_PACKET_ERRORS) {
                Ymodem_Cancel();
                if (cb->on_error) cb->on_error(YMODEM_ERR_SEQ);
                return YMODEM_ERR_SEQ;
            }
            continue;
        }

        uint16_t calc_crc = calc_crc16(data, packet_size);
        if (calc_crc != recv_crc) {
            send_char(YMODEM_NAK);
            if (++packet_errs >= MAX_PACKET_ERRORS) {
                Ymodem_Cancel();
                if (cb->on_error) cb->on_error(YMODEM_ERR_CRC);
                return YMODEM_ERR_CRC;
            }
            continue;
        }

        /* CRC/格式都正确 -> 清 packet_errs */
        packet_errs = 0;

        /* 序列号检查 */
        if (seq_no != expected_seq) {
            if (seq_no == (uint8_t)(expected_seq - 1)) {
                send_char(YMODEM_ACK);
                continue;
            }
            YmodemPort_Log("[YMODEM] Sequence error (expect=%d, recv=%d)\r\n",
                           expected_seq, seq_no);
            Ymodem_Cancel();
            if (cb->on_error) cb->on_error(YMODEM_ERR_SEQ);
            return YMODEM_ERR_SEQ;
        }

        /* ---------- 处理 packet 0（文件信息/结束） ---------- */

        if (seq_no == 0 && (state == 0 || state == 2)) {
            int r = parse_file_info(data, packet_size, filename, &filesize);
            if (r != 0) {
                send_char(YMODEM_NAK);
                if (++packet_errs >= MAX_PACKET_ERRORS) {
                    Ymodem_Cancel();
                    if (cb->on_error) cb->on_error(YMODEM_ERR_PARAM);
                    return YMODEM_ERR_PARAM;
                }
                continue;
            }

            if (filename[0] == '\0') {
                /* 空文件名 = batch 结束 */
                if (state == 2) {
                    YmodemPort_Log("\r\n[YMODEM] Transfer complete: %lu bytes\r\n",
                                   (unsigned long)received_bytes);
                    if (cb->on_end) cb->on_end();
                } else {
                    YmodemPort_Log("[YMODEM] All transfers complete\r\n");
                }
                send_char(YMODEM_ACK);
                return YMODEM_OK;
            }

            /* 非空文件名：不论 state==0 还是 state==2，都当作“开始新文件” */
            YmodemPort_Log("[YMODEM] File: %s, Size: %lu bytes\r\n",
                           filename, (unsigned long)filesize);

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
            send_char(YMODEM_C);
            continue;
        }

        /* ---------- 数据包 ---------- */

        if (state >= 1) {
            uint32_t data_len = packet_size;

            if (filesize > 0 && received_bytes + data_len > filesize) {
                data_len = filesize - received_bytes;
            }

            if (cb->on_data) {
                if (cb->on_data(data, data_len) != 0) {
                    YmodemPort_Log("[YMODEM] Data callback error\r\n");
                    Ymodem_Cancel();
                    return YMODEM_ERR_CALLBACK;
                }
            }

            received_bytes += data_len;
            expected_seq = (uint8_t)(expected_seq + 1);

            if (filesize > 0) {
                YmodemPort_Log("\r[YMODEM] Progress: %lu/%lu (%lu%%)",
                               (unsigned long)received_bytes,
                               (unsigned long)filesize,
                               (unsigned long)(received_bytes * 100 / filesize));
            }

            send_char(YMODEM_ACK);
            continue;
        }

        /* 不应该走到这里，兜底 ACK */
        send_char(YMODEM_ACK);
    }
}