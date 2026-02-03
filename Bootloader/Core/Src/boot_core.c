/**
  ******************************************************************************
  * @file           : boot_core.c
  * @brief          : Boot 核心逻辑模块
  * @description    : 提供镜像选择、跳转等核心 Bootloader 功能
  ******************************************************************************
  */

#include "boot_core.h"
#include "boot_image.h"
#include "boot_slots.h"
#include "boot_swap.h"
#include "gpio.h"
#include <stdio.h>

/*============================================================================
 * 私有函数
 *============================================================================*/

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  检查是否应该立即跳转到 App
 */
int Boot_ShouldJump(void)
{
    return (g_JumpInit == BOOT_MAGIC);
}

/**
 * @brief  跳转到 App (在外设初始化之前调用，状态干净)
 */
void Boot_JumpToApp(void)
{
    /* 获取活动 Slot，计算 App 入口 */
    slot_info_t active = Boot_GetActiveSlot();
    uint32_t entry = Boot_GetAppEntry(active, HDR_SIZE);
    
    /* 设置向量表偏移 */
    SCB->VTOR = entry;
    
    /* 数据同步屏障，确保所有写操作完成 */
    __DSB();
    __ISB();
    
    /* 设置主堆栈指针 */
    __set_MSP(*(__IO uint32_t *)entry);
    
    /* 跳转到 Reset Handler */
    ((void (*)(void))(*(__IO uint32_t *)(entry + 4)))();
    
    /* 不应到达这里 */
    while (1) {}
}

/**
 * @brief  选择最佳镜像并跳转
 */
void Boot_SelectAndJump(void)
{
    /* 获取 active/inactive slot 信息 */
    slot_info_t active_slot   = Boot_GetActiveSlot();
    slot_info_t inactive_slot = Boot_GetInactiveSlot();
    
    /* 检查两个 Slot 的镜像 */
    image_t active   = Boot_InspectImage(active_slot.base);
    image_t inactive = Boot_InspectImage(inactive_slot.base);

    printf("[Boot] Swap state: %d\r\n", Boot_GetSwapState());
    printf("[Boot] Active   Slot (0x%08X): %s\r\n", active_slot.base, active.valid ? "valid" : "invalid");
    printf("[Boot] Inactive Slot (0x%08X): %s\r\n", inactive_slot.base, inactive.valid ? "valid" : "invalid");

    if (active.valid) {
        printf("[Boot] Active   ver=%d.%d.%d size=%d bytes CRC32=0x%08X\r\n", 
               active.hdr->ver.major, active.hdr->ver.minor, active.hdr->ver.patch, 
               active.hdr->img_size, active.hdr->img_crc32);
    }
    if (inactive.valid) {
        printf("[Boot] Inactive ver=%d.%d.%d size=%d bytes CRC32=0x%08X\r\n", 
               inactive.hdr->ver.major, inactive.hdr->ver.minor, inactive.hdr->ver.patch, 
               inactive.hdr->img_size, inactive.hdr->img_crc32);
    }

    /* 1. 两个都无效 → 进入 DFU 模式 */
    if (!active.valid && !inactive.valid) {
        printf("[Boot] No valid image, entering DFU mode...\r\n");
        while (1) {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            HAL_Delay(100);
        }
    }

    /* 2. 判断是否需要 Bank Swap
     *    只有当 inactive 有效且版本比 active 高时才需要 Swap */
    if (inactive.valid && (!active.valid || Boot_SemverCompare(inactive.hdr->ver, active.hdr->ver) > 0)) {
        printf("[Boot] Inactive slot is better, need Bank Swap...\r\n");
        
        uint8_t current_swap = Boot_GetSwapState();
        Boot_SetSwapBank(current_swap ? 0 : 1);  /* 切换状态，会触发复位 */
        /* 不会返回，复位后 active/inactive 互换 */
    }

    /* 3. 当前 Active Slot 是最佳选择（或唯一有效的），跳转 */
    if (!active.valid) {
        /* 理论上不应该到这里，但做个保护 */
        printf("[Boot] Error: Active slot invalid but no swap?\r\n");
        while (1) {}
    }

    printf("[Boot] Jumping to Active Slot at 0x%08X...\r\n", active.app_entry);
    
    /* 设置跳转参数，然后软复位 */
    g_JumpInit = BOOT_MAGIC;
    
    __DSB();
    NVIC_SystemReset();
    
    /* 不应到达 */
    while (1) {}
}
