/**
  ******************************************************************************
  * @file           : iap_write.c
  * @brief          : IAP (In-Application Programming) 模块
  * @description    : 提供非活动 Bank 的擦除和写入功能，用于固件升级
  ******************************************************************************
  */

#include "iap_write.h"
#include "bl_flash_ops.h"
#include "bl_log.h"
#include <string.h>

/*============================================================================
 * 内部常量 (实际值来自 bl_memmap.h，通过 iap_write.h 间接引入)
 *============================================================================*/

/* 逻辑地址 */
#define LOGICAL_SLOT_INACTIVE_BASE  INACTIVE_SLOT_BASE  /* 0x08120000 */

/*============================================================================
 * 公共函数实现 - 地址查询
 *============================================================================*/

/**
 * @brief  获取非活动 Slot 的基地址
 */
uint32_t IAP_GetInactiveSlotBase(void)
{
    /* 非活动 Slot 始终在逻辑地址 0x08120000 */
    return LOGICAL_SLOT_INACTIVE_BASE;
}

/**
 * @brief  获取非活动 Slot 的 App 区域大小 (不含 trailer)
 */
uint32_t IAP_GetInactiveSlotSize(void)
{
    return APP_SLOT_SIZE;  /* 768KB */
}

/*============================================================================
 * 公共函数实现 - 擦除操作
 *============================================================================*/

/**
 * @brief  内部函数：擦除非活动 Slot 的指定扇区 (含 Trailer)
 */
static int IAP_EraseSectorRaw(uint32_t sector_index)
{
    /* 检查扇区索引有效性 (0-6，含 trailer) */
    if (sector_index >= SLOT_SECTOR_COUNT) {
        BL_LOG("[IAP] Invalid sector index: %lu (max=%u)\r\n",
               (unsigned long)sector_index, SLOT_SECTOR_COUNT - 1);
        return -1;
    }

    /* 计算扇区地址 */
    uint32_t sector_addr = LOGICAL_SLOT_INACTIVE_BASE + (sector_index * IAP_SECTOR_SIZE);

    BL_LOG("[IAP] Erasing sector %lu at 0x%08lX...\r\n",
           (unsigned long)sector_index, (unsigned long)sector_addr);

    if (BL_Flash_EraseSector(sector_addr) != 0) {
        return -2;
    }

    BL_LOG("[IAP] Sector %lu erased OK\r\n", (unsigned long)sector_index);
    return 0;
}

/**
 * @brief  擦除非活动 Slot 的指定扇区 (仅 App 区域，不含 Trailer)
 */
int IAP_EraseSector(uint32_t sector_index)
{
    /* 检查扇区索引有效性 (0-5，不含 trailer) */
    if (sector_index >= APP_SECTOR_COUNT) {
        BL_LOG("[IAP] Invalid sector index: %lu (max=%u)\r\n",
               (unsigned long)sector_index, APP_SECTOR_COUNT - 1);
        return -1;
    }

    return IAP_EraseSectorRaw(sector_index);
}

/**
 * @brief  擦除非活动 Slot 的全部区域 (包含 App + Trailer)
 */
int IAP_EraseSlot(void)
{
    /* 连 trailer 一起擦除，确保旧镜像留下的 PENDING/REJECTED 状态不会污染新升级结果。 */
    BL_LOG("[IAP] Erasing inactive slot (0x%08lX, %lu sectors, including trailer)...\r\n",
           (unsigned long)LOGICAL_SLOT_INACTIVE_BASE, (unsigned long)SLOT_SECTOR_COUNT);

    for (uint32_t i = 0; i < SLOT_SECTOR_COUNT; i++) {
        if (IAP_EraseSectorRaw(i) != 0) {
            BL_LOG("[IAP] Slot erase failed at sector %lu\r\n", (unsigned long)i);
            return -1;
        }
    }

    BL_LOG("[IAP] Slot erase complete\r\n");
    return 0;
}

/**
 * @brief  擦除指定地址范围
 */
int IAP_EraseRange(uint32_t start_addr, uint32_t size)
{
    uint32_t slot_base = LOGICAL_SLOT_INACTIVE_BASE;
    uint32_t slot_end  = slot_base + APP_SLOT_SIZE;

    /* 检查地址范围有效性 */
    if (start_addr < slot_base || start_addr >= slot_end) {
        BL_LOG("[IAP] Start address 0x%08lX out of range\r\n", (unsigned long)start_addr);
        return -1;
    }

    if (start_addr + size > slot_end) {
        BL_LOG("[IAP] Range exceeds slot boundary\r\n");
        return -1;
    }

    /* 计算需要擦除的扇区范围 */
    uint32_t first_sector = (start_addr - slot_base) / IAP_SECTOR_SIZE;
    uint32_t last_sector  = (start_addr + size - 1 - slot_base) / IAP_SECTOR_SIZE;

    BL_LOG("[IAP] Erasing range 0x%08lX - 0x%08lX (sectors %lu-%lu)...\r\n",
           (unsigned long)start_addr, (unsigned long)(start_addr + size - 1),
           (unsigned long)first_sector, (unsigned long)last_sector);

    for (uint32_t i = first_sector; i <= last_sector; i++) {
        if (IAP_EraseSector(i) != 0) {
            return -2;
        }
    }

    return 0;
}

/*============================================================================
 * 公共函数实现 - 写入操作
 *============================================================================*/

/**
 * @brief  开始 IAP 写入会话
 */
int IAP_Begin(iap_writer_t* w, uint32_t dst_base, uint32_t dst_size)
{
    if (!w) return -1;

    uint32_t slot_base = LOGICAL_SLOT_INACTIVE_BASE;
    uint32_t slot_end  = slot_base + APP_SLOT_SIZE;

    /* 检查目标地址有效性 */
    if (dst_base < slot_base || dst_base >= slot_end) {
        BL_LOG("[IAP] Invalid base address 0x%08lX\r\n", (unsigned long)dst_base);
        return -2;
    }

    if (dst_base + dst_size > slot_end) {
        BL_LOG("[IAP] Size exceeds slot boundary\r\n");
        return -3;
    }

    /* 初始化写入上下文。真正写 Flash 时按 32B 聚合，避免破坏 STM32H7 的 flash word 约束。 */
    w->base  = dst_base;
    w->limit = dst_base + dst_size;
    w->addr  = dst_base;
    w->fill  = 0;
    memset(w->buf32, 0xFF, sizeof(w->buf32));  /* 填充 0xFF */

    BL_LOG("[IAP] Write session started: 0x%08lX - 0x%08lX\r\n",
           (unsigned long)w->base, (unsigned long)w->limit);

    return 0;
}

/**
 * @brief  写入数据
 */
int IAP_Write(iap_writer_t* w, const uint8_t* data, uint32_t len)
{
    if (!w || !data) return -1;

    while (len > 0) {
        /* 只有在缓冲区为空时才允许检查 addr>=limit。
           若缓冲区里还有未刷新的尾包，说明这 32B flash word 仍属于合法范围。 */
        if (w->addr >= w->limit && w->fill == 0) {
            BL_LOG("[IAP] Write overflow\r\n");
            return -2;
        }

        /* 先把数据拼满一个 flash word，再一次性写入 Flash。 */
        uint32_t space = IAP_FLASH_WORD_SIZE - w->fill;
        uint32_t copy_len = (len < space) ? len : space;

        memcpy(&w->buf32[w->fill], data, copy_len);
        w->fill += copy_len;
        data    += copy_len;
        len     -= copy_len;

        /* 缓冲区凑满 32B 后再落盘，符合 H7 的最小编程粒度。 */
        if (w->fill == IAP_FLASH_WORD_SIZE) {
            if (BL_Flash_WriteWord(w->addr, w->buf32) != 0) {
                BL_LOG("[IAP] Write failed at 0x%08lX\r\n", (unsigned long)w->addr);
                return -3;
            }

            w->addr += IAP_FLASH_WORD_SIZE;
            w->fill = 0;
            memset(w->buf32, 0xFF, sizeof(w->buf32));  /* 重置为 0xFF */
        }
    }

    return 0;
}

/**
 * @brief  结束 IAP 写入会话
 */
int IAP_End(iap_writer_t* w)
{
    if (!w) return -1;

    /* 尾包不足 32B 时补 0xFF 再写入。
       这样既满足最小写入粒度，也与“未写区域保持擦除态”语义一致。 */
    if (w->fill > 0) {
        /* buf32 已经预填充 0xFF，直接写入 */
        if (BL_Flash_WriteWord(w->addr, w->buf32) != 0) {
            BL_LOG("[IAP] Final write failed at 0x%08lX\r\n", (unsigned long)w->addr);
            return -2;
        }

        w->addr += IAP_FLASH_WORD_SIZE;
        w->fill = 0;
    }

    BL_LOG("[IAP] Write session complete: %lu bytes written\r\n",
           (unsigned long)(w->addr - w->base));

    return 0;
}
