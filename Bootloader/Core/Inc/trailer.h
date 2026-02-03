#pragma once
#include <stdint.h>

#define TR_MAGIC 0x544C5252u  // 'TLRR' 随便选一个固定值
#define TRAILER_SIZE      0x00020000u  // 128KB


typedef enum {
  TR_STATE_PENDING   = 0xAAAA0002u,
  TR_STATE_CONFIRMED = 0xAAAA0003u,
  TR_STATE_REJECTED  = 0xAAAA0004u,
} tr_state_t;

/* 32B 对齐：贴合 STM32H7 flash word(256-bit=32B) */
typedef struct __attribute__((packed, aligned(32))) {
  uint32_t magic;
  uint32_t seq;
  uint32_t state;
  uint32_t attempt;
  uint32_t img_crc32;
  uint32_t rsv[3];   // padding to 32B
} tr_rec_t;

/* Boot侧：读/写 */
int trailer_read_last(uint32_t trailer_base, tr_rec_t* out);
int trailer_append(uint32_t trailer_base, const tr_rec_t* rec);
