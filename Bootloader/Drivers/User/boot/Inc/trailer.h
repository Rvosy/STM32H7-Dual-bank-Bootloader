#pragma once
#include <stdint.h>

/*============================================================================
 * 常量定义
 *============================================================================*/

#define TR_MAGIC          0x544C5252u   /* 'TLRR' trailer magic */
#define TRAILER_SIZE      0x00020000u   /* 128KB trailer 扇区大小 */
#define MAX_ATTEMPTS      3u            /* 最大尝试次数，超过则回滚 */

/*============================================================================
 * 状态机常量 (使用 #define 避免 enum 超出 int 范围)
 *============================================================================*/

#define TR_STATE_NEW       0xAAAA0001u   /* 新镜像，尚未尝试启动 */
#define TR_STATE_PENDING   0xAAAA0002u   /* 待确认，正在试运行 */
#define TR_STATE_CONFIRMED 0xAAAA0003u   /* 已确认，App 自检通过 */
#define TR_STATE_REJECTED  0xAAAA0004u   /* 已拒绝，需要回滚 */

typedef uint32_t tr_state_t;

/*============================================================================
 * Trailer 记录结构体 (32B 大小，贴合 STM32H7 flash word)
 * 注意：不使用 aligned(32) 以避免栈分配问题，写入时使用静态缓冲区
 *============================================================================*/

typedef struct __attribute__((packed)) {
  uint32_t magic;       /* 魔数，必须为 TR_MAGIC */
  uint32_t seq;         /* 序列号，递增 */
  uint32_t state;       /* 状态：TR_STATE_xxx */
  uint32_t attempt;     /* 尝试次数 1..N */
  uint32_t img_crc32;   /* 绑定的镜像 CRC32，防止写错槽 */
  uint32_t rsv[3];      /* 保留，padding to 32B */
} tr_rec_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  读取 trailer 扇区的最后一条有效记录
 * @param  trailer_base: trailer 扇区基地址
 * @param  out: 输出的记录指针
 * @retval 0=成功, -1=无有效记录
 */
int trailer_read_last(uint32_t trailer_base, tr_rec_t* out);

/**
 * @brief  追加写入一条 trailer 记录 (32B flash word)
 * @param  trailer_base: trailer 扇区基地址
 * @param  rec: 要写入的记录
 * @retval 0=成功, -1=扇区已满需要擦除, -2=写入失败
 */
int trailer_append(uint32_t trailer_base, const tr_rec_t* rec);

/**
 * @brief  擦除 trailer 扇区 (仅在需要清空/写满时调用)
 * @param  trailer_base: trailer 扇区基地址
 * @retval 0=成功, -1=擦除失败
 */
int trailer_erase(uint32_t trailer_base);

/**
 * @brief  检查 trailer 扇区是否写满
 * @param  trailer_base: trailer 扇区基地址
 * @retval 1=已满, 0=未满
 */
int trailer_is_full(uint32_t trailer_base);

/**
 * @brief  获取下一个序列号
 * @param  trailer_base: trailer 扇区基地址
 * @retval 下一个序列号 (当前最大 seq + 1，若无记录则返回 1)
 */
uint32_t trailer_next_seq(uint32_t trailer_base);
