/**
  ******************************************************************************
  * @file           : boot_core.c
  * @brief          : Boot 核心逻辑模块
  * @description    : 提供镜像选择、跳转等核心 Bootloader 功能
  ******************************************************************************
  */

#include "boot_core.h"
#include "boot_image.h"
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
    uint32_t entry = APP_A_ENTRY;
    
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
    image_t A = Boot_InspectImage(SLOT_A_BASE);  /* 0x08020000 - 当前 Bank 的 App */
    image_t B = Boot_InspectImage(SLOT_B_BASE);  /* 0x08120000 - 另一个 Bank 的 App */

    printf("[Boot] Swap state: %d\r\n", Boot_GetSwapState());
    printf("[Boot] Slot A (0x%08X): %s\r\n", SLOT_A_BASE, A.valid ? "valid" : "invalid");
    printf("[Boot] Slot B (0x%08X): %s\r\n", SLOT_B_BASE, B.valid ? "valid" : "invalid");

    if (A.valid) {
        printf("[Boot] A ver=%d.%d.%d size=%d bytes CRC32=0x%08X\r\n", 
               A.hdr->ver.major, A.hdr->ver.minor, A.hdr->ver.patch, 
               A.hdr->img_size, A.hdr->img_crc32);
    }
    if (B.valid) {
        printf("[Boot] B ver=%d.%d.%d size=%d bytes CRC32=0x%08X\r\n", 
               B.hdr->ver.major, B.hdr->ver.minor, B.hdr->ver.patch, 
               B.hdr->img_size, B.hdr->img_crc32);
    }

    /* 1. 两个都无效 → 进入 DFU 模式 */
    if (!A.valid && !B.valid) {
        printf("[Boot] No valid image, entering DFU mode...\r\n");
        while (1) {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            HAL_Delay(100);
        }
    }

    /* 2. 判断是否需要 Bank Swap
     *    只有当 B 有效且版本比 A 高时才需要 Swap */
    if (B.valid && (!A.valid || Boot_SemverCompare(B.hdr->ver, A.hdr->ver) > 0)) {
        printf("[Boot] Slot B is better, need Bank Swap...\r\n");
        
        uint8_t current_swap = Boot_GetSwapState();
        Boot_SetSwapBank(current_swap ? 0 : 1);  /* 切换状态，会触发复位 */
        /* 不会返回，复位后 A/B 地址映射互换 */
    }

    /* 3. 当前 Slot A 是最佳选择（或唯一有效的），跳转到 0x08020000 */
    if (!A.valid) {
        /* 理论上不应该到这里，但做个保护 */
        printf("[Boot] Error: Slot A invalid but no swap?\r\n");
        while (1) {}
    }

    printf("[Boot] Jumping to Slot A at 0x%08X...\r\n", A.app_entry);
    
    /* 设置跳转参数，然后软复位 */
    g_JumpInit = BOOT_MAGIC;
    
    __DSB();
    NVIC_SystemReset();
    
    /* 不应到达 */
    while (1) {}
}
