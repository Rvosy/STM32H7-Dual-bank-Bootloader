#include "trailer.h"
#include "stm32h7xx_hal.h"

static int rec_is_valid(const tr_rec_t* r) {
  return (r->magic == TR_MAGIC);
}

int trailer_read_last(uint32_t base, tr_rec_t* out) {
  const tr_rec_t* p = (const tr_rec_t*)base;
  const tr_rec_t* last = NULL;

  // 扫描整个 trailer 扇区
  for (uint32_t off = 0; off < TRAILER_SIZE; off += sizeof(tr_rec_t)) {
    const tr_rec_t* r = (const tr_rec_t*)(base + off);
    if (!rec_is_valid(r)) break;          // 遇到空/无效就停止（append-only）
    last = r;
  }

  if (!last) return -1;
  *out = *last;
  return 0;
}

int trailer_append(uint32_t base, const tr_rec_t* rec) {
  // 1) 找到空位
  uint32_t write_addr = base;
  for (uint32_t off = 0; off < TRAILER_SIZE; off += sizeof(tr_rec_t)) {
    const tr_rec_t* r = (const tr_rec_t*)(base + off);
    if (!rec_is_valid(r)) { write_addr = base + off; break; }
  }

  // 2) 按 32B 写入（STM32H7 要求 flash word 32B 对齐写）
  // 这里你要用 HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr, data)
  // data 要指向 32B 数据
  HAL_FLASH_Unlock();
  // ...
  HAL_FLASH_Lock();
  return 0;
}
