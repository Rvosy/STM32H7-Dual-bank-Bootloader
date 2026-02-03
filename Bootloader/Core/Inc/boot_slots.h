#ifndef __BOOT_SLOTS_H
#define __BOOT_SLOTS_H

#include <stdint.h>

/*============================================================================
 * 物理地址常量 (硬件层面，不随 swap 变化)
 *============================================================================*/

#define FLASH_BANK1_BASE      0x08000000u   /* Bank1 物理基地址 */
#define FLASH_BANK2_BASE      0x08100000u   /* Bank2 物理基地址 */
#define FLASH_BANK_SIZE       0x00100000u   /* 每个 Bank 1MB */

#define BOOTLOADER_SIZE       0x00020000u   /* Bootloader 占用 128KB */
#define SLOT_TOTAL_SIZE       0x000E0000u   /* Slot 总大小 896KB (1MB - 128KB) */

/*============================================================================
 * Slot 信息结构体
 *============================================================================*/

typedef struct {
    uint32_t base;              /* Slot 基地址 */
    uint32_t trailer_base;      /* Trailer 基地址 */
} slot_info_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  获取当前活动 Slot (Bootloader 执行后会跳转的 Slot)
 * @note   活动 Slot 始终在逻辑地址 0x08020000 (Bank1 + 128KB)
 *         但物理上可能是 Bank1 或 Bank2 (取决于 swap 状态)
 * @retval slot_info_t 包含基地址和 trailer 地址
 */
slot_info_t Boot_GetActiveSlot(void);

/**
 * @brief  获取非活动 Slot (用于 OTA 写入新固件)
 * @note   非活动 Slot 始终在逻辑地址 0x08120000 (Bank2 + 128KB)
 *         但物理上可能是 Bank2 或 Bank1 (取决于 swap 状态)
 * @retval slot_info_t 包含基地址和 trailer 地址
 */
slot_info_t Boot_GetInactiveSlot(void);

/**
 * @brief  根据 swap 状态获取物理 Bank 地址
 * @param  is_active: 1=活动 Slot, 0=非活动 Slot
 * @retval 物理 Bank 基地址 (用于 Flash 操作)
 */
uint32_t Boot_GetPhysicalBankBase(int is_active);

/**
 * @brief  获取 Slot 对应的 App 入口地址
 * @param  slot: Slot 信息
 * @param  hdr_size: 镜像头大小
 * @retval App 入口地址
 */
static inline uint32_t Boot_GetAppEntry(slot_info_t slot, uint32_t hdr_size)
{
    return slot.base + hdr_size;
}

#endif /* __BOOT_SLOTS_H */
