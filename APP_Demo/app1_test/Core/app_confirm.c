/**
  ******************************************************************************
  * @file           : app_confirm.c
  * @brief          : App 侧确认接口实现
  * @description    : 提供 App 自检通过后确认镜像的功能
  ******************************************************************************
  */

#include "app_confirm.h"
#include "image_header.h"
#include "stm32h7xx_hal.h"
#include <string.h>

/*============================================================================
 * 内部常量
 *============================================================================*/

/* Bank 地址定义 */
#define BOOTLOADER_SIZE       0x00020000u   /* 128KB */
#define SLOT_TOTAL_SIZE       0x000E0000u   /* 896KB (1MB - 128KB) */

/* 
 * Active Slot 逻辑地址 (Bank Swap 后 App 运行时看到的地址)
 * 无论 swap 状态如何，App 始终在逻辑地址 0x08020000 运行
 */
#define ACTIVE_SLOT_BASE      (FLASH_BANK1_BASE + BOOTLOADER_SIZE)
#define ACTIVE_TRAILER_BASE   (ACTIVE_SLOT_BASE + SLOT_TOTAL_SIZE - TRAILER_SIZE)

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
 * @brief  读取 trailer 扇区的最后一条有效记录
 */
static int trailer_read_last_app(uint32_t base, tr_rec_t* out) 
{
    const tr_rec_t* last = NULL;

    for (uint32_t off = 0; off < TRAILER_SIZE; off += sizeof(tr_rec_t)) {
        const tr_rec_t* r = (const tr_rec_t*)(base + off);
        if (rec_is_empty(r)) break;
        if (rec_is_valid(r)) {
            last = r;
        }
    }

    if (!last) return -1;
    *out = *last;
    return 0;
}

/**
 * @brief  获取下一个序列号
 */
static uint32_t trailer_next_seq_app(uint32_t base)
{
    static tr_rec_t last;  /* 使用 static 避免栈对齐问题 */
    if (trailer_read_last_app(base, &last) == 0) {
        return last.seq + 1;
    }
    return 1;
}

/* 静态 32B 对齐缓冲区，用于 Flash 写入 */
static uint8_t s_flash_write_buf[32] __attribute__((aligned(32)));

/**
 * @brief  追加写入一条 trailer 记录 (App 侧)
 */
static int trailer_append_app(uint32_t base, const tr_rec_t* rec) 
{
    uint32_t write_addr = 0;
    int found_empty = 0;

    /* 找到第一个空位 */
    for (uint32_t off = 0; off < TRAILER_SIZE; off += sizeof(tr_rec_t)) {
        const tr_rec_t* r = (const tr_rec_t*)(base + off);
        if (rec_is_empty(r)) { 
            write_addr = base + off; 
            found_empty = 1;
            break; 
        }
    }

    /* 扇区已满 */
    if (!found_empty) {
        return -1;
    }

    /* 准备 32B 对齐的数据 (使用静态缓冲区) */
    memcpy(s_flash_write_buf, rec, sizeof(tr_rec_t));

    /* 按 32B (256-bit flash word) 写入 */
    HAL_StatusTypeDef status;
    
    HAL_FLASH_Unlock();
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, write_addr, (uint32_t)s_flash_write_buf);
    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 0 : -2;
}

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  App 自检通过后调用，将当前镜像标记为 CONFIRMED
 */
int App_ConfirmSelf(void)
{
    static tr_rec_t last_rec;   /* 使用 static 避免栈对齐问题 */
    static tr_rec_t new_rec;
    memset(&new_rec, 0, sizeof(new_rec));
    
    /* 获取当前镜像的 CRC32 (来自镜像头) */
    uint32_t my_crc32 = g_image_header.img_crc32;
    
    /* 读取当前 trailer 最后一条记录 */
    if (trailer_read_last_app(ACTIVE_TRAILER_BASE, &last_rec) == 0) {
        /* 检查是否已经 CONFIRMED */
        if (last_rec.state == TR_STATE_CONFIRMED && 
            last_rec.img_crc32 == my_crc32) {
            /* 已经确认过了，不需要重复写入 */
            return 0;
        }
        
        /* 验证 CRC 绑定 */
        if (last_rec.img_crc32 != my_crc32) {
            /* CRC 不匹配，可能是 trailer 数据混乱 */
            /* 继续写入 CONFIRMED，但带上正确的 CRC */
        }
    }
    
    /* 构造 CONFIRMED 记录 */
    new_rec.magic     = TR_MAGIC;
    new_rec.seq       = trailer_next_seq_app(ACTIVE_TRAILER_BASE);
    new_rec.state     = TR_STATE_CONFIRMED;
    new_rec.attempt   = 0;  /* CONFIRMED 后 attempt 清零 */
    new_rec.img_crc32 = my_crc32;
    
    /* 写入 */
    return trailer_append_app(ACTIVE_TRAILER_BASE, &new_rec);
}

/**
 * @brief  检查当前镜像是否处于 PENDING 状态
 */
int App_IsPending(void)
{
    static tr_rec_t last_rec;  /* 使用 static 避免栈对齐问题 */
    uint32_t my_crc32 = g_image_header.img_crc32;
    
    if (trailer_read_last_app(ACTIVE_TRAILER_BASE, &last_rec) == 0) {
        if (last_rec.state == TR_STATE_PENDING && 
            last_rec.img_crc32 == my_crc32) {
            return 1;
        }
    }
    
    return 0;
}

/**
 * @brief  检查当前镜像是否已 CONFIRMED
 */
int App_IsConfirmed(void)
{
    static tr_rec_t last_rec;  /* 使用 static 避免栈对齐问题 */
    uint32_t my_crc32 = g_image_header.img_crc32;
    
    if (trailer_read_last_app(ACTIVE_TRAILER_BASE, &last_rec) == 0) {
        if (last_rec.state == TR_STATE_CONFIRMED && 
            last_rec.img_crc32 == my_crc32) {
            return 1;
        }
    }
    
    return 0;
}
