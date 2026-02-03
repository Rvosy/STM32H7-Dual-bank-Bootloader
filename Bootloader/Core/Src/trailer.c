/**
  ******************************************************************************
  * @file           : trailer.c
  * @brief          : Trailer 扇区管理模块
  * @description    : 提供 trailer 记录的读/写/擦除功能，用于回滚状态机
  ******************************************************************************
  */

#include "trailer.h"
#include "boot_slots.h"
#include "stm32h7xx_hal.h"
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
    const uint32_t* p = (const uint32_t*)r;
    for (int i = 0; i < 8; i++) {
        if (p[i] != 0xFFFFFFFFu) return 0;
    }
    return 1;
}

/**
 * @brief  获取 trailer 扇区对应的 Flash Bank 编号
 */
static uint32_t get_flash_bank(uint32_t addr)
{
    /* Bank1: 0x08000000 - 0x080FFFFF, Bank2: 0x08100000 - 0x081FFFFF */
    return (addr >= FLASH_BANK2_BASE) ? FLASH_BANK_2 : FLASH_BANK_1;
}

/**
 * @brief  获取 trailer 扇区对应的 Flash Sector 编号
 * @note   STM32H7 每个 Bank 有 8 个 128KB 扇区 (Sector 0-7)
 *         trailer 位于 Slot 末尾 128KB，即每个 Bank 的最后一个扇区 (Sector 7)
 */
static uint32_t get_flash_sector(uint32_t addr)
{
    uint32_t bank_base = (addr >= FLASH_BANK2_BASE) ? FLASH_BANK2_BASE : FLASH_BANK1_BASE;
    uint32_t offset = addr - bank_base;
    return offset / 0x20000u;  /* 128KB per sector */
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

    /* 扫描整个 trailer 扇区，找到最后一条有效记录 */
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
static uint8_t s_flash_write_buf[32] __attribute__((aligned(32)));

/**
 * @brief  追加写入一条 trailer 记录
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

    /* 2) 准备 32B 对齐的数据 (使用静态缓冲区) */
    memcpy(s_flash_write_buf, rec, sizeof(tr_rec_t));

    /* 3) 按 32B (256-bit flash word) 写入 */
    HAL_StatusTypeDef status;
    
    HAL_FLASH_Unlock();
    
    /* STM32H7 使用 FLASH_TYPEPROGRAM_FLASHWORD 一次写 256 bits = 32 bytes */
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, write_addr, (uint32_t)s_flash_write_buf);
    
    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 0 : -2;
}

/**
 * @brief  擦除 trailer 扇区
 */
int trailer_erase(uint32_t base)
{
    FLASH_EraseInitTypeDef erase_cfg = {0};
    uint32_t page_error = 0;
    HAL_StatusTypeDef status;

    erase_cfg.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_cfg.Banks     = get_flash_bank(base);
    erase_cfg.Sector    = get_flash_sector(base);
    erase_cfg.NbSectors = 1;
    erase_cfg.VoltageRange = FLASH_VOLTAGE_RANGE_3;  /* 2.7V - 3.6V */

    HAL_FLASH_Unlock();
    
    status = HAL_FLASHEx_Erase(&erase_cfg, &page_error);
    
    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 0 : -1;
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
 */
uint32_t trailer_next_seq(uint32_t base)
{
    static tr_rec_t last;  /* 使用 static 避免栈对齐问题 */
    if (trailer_read_last(base, &last) == 0) {
        return last.seq + 1;
    }
    return 1;  /* 无记录时从 1 开始 */
}
