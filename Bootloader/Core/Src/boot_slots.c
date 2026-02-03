/**
  ******************************************************************************
  * @file           : boot_slots.c
  * @brief          : Slot 地址映射模块
  * @description    : 根据 Bank Swap 状态计算 active/inactive Slot 地址
  ******************************************************************************
  */

#include "boot_slots.h"
#include "boot_swap.h"
#include "trailer.h"

/*============================================================================
 * 内部常量
 *============================================================================*/

/* 逻辑地址 (CPU 视角，swap 后会变化) */
#define LOGICAL_SLOT_ACTIVE_BASE      (FLASH_BANK1_BASE + BOOTLOADER_SIZE)  /* 0x08020000 */
#define LOGICAL_SLOT_INACTIVE_BASE    (FLASH_BANK2_BASE + BOOTLOADER_SIZE)  /* 0x08120000 */

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  获取当前活动 Slot
 * @note   活动 Slot = CPU 执行的 Slot = 逻辑地址 0x08020000
 *         无论 swap 状态如何，Bootloader 总是跳转到 0x08020000
 */
slot_info_t Boot_GetActiveSlot(void)
{
    slot_info_t slot;
    
    /* 活动 Slot 始终在逻辑地址 0x08020000 */
    slot.base = LOGICAL_SLOT_ACTIVE_BASE;
    slot.trailer_base = slot.base + SLOT_TOTAL_SIZE - TRAILER_SIZE;
    
    return slot;
}

/**
 * @brief  获取非活动 Slot
 * @note   非活动 Slot = OTA 写入目标 = 逻辑地址 0x08120000
 */
slot_info_t Boot_GetInactiveSlot(void)
{
    slot_info_t slot;
    
    /* 非活动 Slot 始终在逻辑地址 0x08120000 */
    slot.base = LOGICAL_SLOT_INACTIVE_BASE;
    slot.trailer_base = slot.base + SLOT_TOTAL_SIZE - TRAILER_SIZE;
    
    return slot;
}

/**
 * @brief  根据 swap 状态获取物理 Bank 地址
 * @note   这个函数用于需要知道物理 Bank 的场景 (如擦除整个 Bank)
 *         
 *         Swap=0 (未交换):
 *           - Active  Slot (0x08020000) → 物理 Bank1
 *           - Inactive Slot (0x08120000) → 物理 Bank2
 *         
 *         Swap=1 (已交换):
 *           - Active  Slot (0x08020000) → 物理 Bank2
 *           - Inactive Slot (0x08120000) → 物理 Bank1
 */
uint32_t Boot_GetPhysicalBankBase(int is_active)
{
    uint8_t swap = Boot_GetSwapState();
    
    if (is_active) {
        /* 活动 Slot 的物理 Bank */
        return swap ? FLASH_BANK2_BASE : FLASH_BANK1_BASE;
    } else {
        /* 非活动 Slot 的物理 Bank */
        return swap ? FLASH_BANK1_BASE : FLASH_BANK2_BASE;
    }
}
