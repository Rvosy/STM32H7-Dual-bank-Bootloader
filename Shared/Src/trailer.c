/**
  ******************************************************************************
  * @file           : trailer.c
  * @brief          : Trailer 扇区管理模块 (Bootloader / App 共用)
  * @description    : 提供 trailer 记录的读/写/擦除功能，用于回滚状态机
  ******************************************************************************
  */

#include "trailer.h"
#include "bl_flash_ops.h"
#include <string.h>

/*============================================================================
 * 内部函数
 *============================================================================*/

/**
 * @brief  检查记录是否有效
 */
static int rec_is_valid(const tr_rec_t* r)
{
    return (r->magic == TR_MAGIC);
}

/**
 * @brief  检查一个 32B 区域是否为空 (全 0xFF)
 */
static int rec_is_empty(const tr_rec_t* r)
{
    /* 擦除后的 Flash 全为 0xFF，因此“空记录”就是 32B 全 0xFF。 */
    const uint32_t* p = (const uint32_t*)r;
    for (int i = 0; i < 8; i++) {
        if (p[i] != 0xFFFFFFFFu) return 0;
    }
    return 1;
}

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  读取 trailer 扇区的最后一条有效记录
 */
int trailer_read_last(uint32_t base, tr_rec_t* out)
{
    const tr_rec_t* last = NULL;

    /* trailer 采用 append-only 日志结构，因此从头扫到第一个空洞即可，
       最后一个有效项就是当前状态。 */
    for (uint32_t off = 0; off < TRAILER_SIZE; off += sizeof(tr_rec_t)) {
        const tr_rec_t* r = (const tr_rec_t*)(base + off);

        /* 遇到空记录停止 (append-only 特性) */
        if (rec_is_empty(r)) break;

        /* 有效记录则更新 last */
        if (rec_is_valid(r)) {
            last = r;
        }
    }

    if (!last) return -1;

    *out = *last;
    return 0;
}

/* 静态 32B 对齐缓冲区，用于 Flash 写入 */
static uint8_t s_trailer_write_buf[32] __attribute__((aligned(32)));

/**
 * @brief  追加写入一条 trailer 记录
 * @note   通过 BL_Flash_WriteWord 统一管理 IRQ 保护和 DCache
 */
int trailer_append(uint32_t base, const tr_rec_t* rec)
{
    uint32_t write_addr = 0;
    int found_empty = 0;

    /* 1) 找到第一个空位 */
    for (uint32_t off = 0; off < TRAILER_SIZE; off += sizeof(tr_rec_t)) {
        const tr_rec_t* r = (const tr_rec_t*)(base + off);
        if (rec_is_empty(r)) {
            write_addr = base + off;
            found_empty = 1;
            break;
        }
    }

    /* 扇区已满，需要先擦除 */
    if (!found_empty) {
        return -1;
    }

    /* 2) 准备 32B 对齐的数据。
       trailer 记录尺寸刻意设计成一个 flash word，保证单条记录原子落盘。 */
    memset(s_trailer_write_buf, 0xFF, sizeof(s_trailer_write_buf));
    memcpy(s_trailer_write_buf, rec, sizeof(tr_rec_t));

    /* 3) 通过统一接口写入 (带 IRQ 保护 + DCache 管理) */
    return BL_Flash_WriteWord(write_addr, s_trailer_write_buf);
}

/**
 * @brief  擦除 trailer 扇区
 * @note   通过 BL_Flash_EraseSector 统一管理 IRQ 保护和 DCache
 */
int trailer_erase(uint32_t base)
{
    return BL_Flash_EraseSector(base);
}

/**
 * @brief  检查 trailer 扇区是否写满
 */
int trailer_is_full(uint32_t base)
{
    /* 检查最后一个 slot 是否为空 */
    uint32_t last_slot = base + TRAILER_SIZE - sizeof(tr_rec_t);
    const tr_rec_t* r = (const tr_rec_t*)last_slot;
    return !rec_is_empty(r);
}

/**
 * @brief  获取下一个序列号
 * @note   防御性溢出保护：接近 UINT32_MAX 时环绕回 1，避免 seq 溢出后
 *         与旧记录冲突造成时序混乱（实际几乎不可达，纯粹代价为零的保险）。
 */
uint32_t trailer_next_seq(uint32_t base)
{
    static tr_rec_t last;  /* 使用 static 避免栈对齐问题 */
    if (trailer_read_last(base, &last) == 0) {
        /* 接近 UINT32_MAX 时环绕，避免理论溢出 */
        if (last.seq >= 0xFFFFFFFEu) {
            return 1;
        }
        return last.seq + 1;
    }
    return 1;  /* 无记录时从 1 开始 */
}
