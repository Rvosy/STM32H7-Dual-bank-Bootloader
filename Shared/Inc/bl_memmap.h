/**
  ******************************************************************************
  * @file           : bl_memmap.h
  * @brief          : STM32H743 Dual Bank Bootloader 内存布局常量
  * @description    : 所有地址常量和 Magic Number 的唯一真相源
  *                   Bootloader 和 App 共享此头文件，消除重复定义
  ******************************************************************************
  */

#ifndef BL_MEMMAP_H
#define BL_MEMMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * STM32H7xx Flash 基地址
 *============================================================================*/

#define BL_FLASH_BANK1_BASE    0x08000000u   /* Flash Bank1 起始 */
#define BL_FLASH_BANK2_BASE    0x08100000u   /* Flash Bank2 起始 */

/*============================================================================
 * Flash 物理参数
 *============================================================================*/

#define BL_SECTOR_SIZE         0x00020000u   /* 128KB per sector */
#define BL_FLASH_WORD_SIZE     32u           /* 256-bit = 32 bytes per flash word */

/*============================================================================
 * 内存布局分区大小
 *
 * Bank1:                          Bank2:
 * 0x08000000 Bootloader (128KB)   0x08100000 Bootloader Mirror (128KB)
 * 0x08020000 App Slot A (768KB)   0x08120000 App Slot B (768KB)
 * 0x080E0000 Trailer A  (128KB)   0x081E0000 Trailer B  (128KB)
 *============================================================================*/

#define BOOTLOADER_SIZE        0x00020000u   /* Bootloader 占用 128KB */
#define SLOT_TOTAL_SIZE        0x000E0000u   /* Slot 总大小 896KB (1MB - 128KB) */
#define TRAILER_SIZE           0x00020000u   /* Trailer 扇区 128KB */
#define APP_SLOT_SIZE          (SLOT_TOTAL_SIZE - TRAILER_SIZE) /* App 可用 768KB */
#define HDR_SIZE               0x200u        /* 镜像头大小 512B */

/*============================================================================
 * Slot 逻辑地址
 * 无论 Bank Swap 状态如何，Bootloader 始终从逻辑地址看到这组地址
 *============================================================================*/

#define ACTIVE_SLOT_BASE       (BL_FLASH_BANK1_BASE + BOOTLOADER_SIZE)   /* 0x08020000 */
#define INACTIVE_SLOT_BASE     (BL_FLASH_BANK2_BASE + BOOTLOADER_SIZE)   /* 0x08120000 */
#define ACTIVE_TRAILER_BASE    (ACTIVE_SLOT_BASE + SLOT_TOTAL_SIZE - TRAILER_SIZE)   /* 0x080E0000 */
#define INACTIVE_TRAILER_BASE  (INACTIVE_SLOT_BASE + SLOT_TOTAL_SIZE - TRAILER_SIZE) /* 0x081E0000 */

/*============================================================================
 * Slot 扇区数
 *============================================================================*/

#define APP_SECTOR_COUNT       6u   /* App 区域 768KB / 128KB = 6 个扇区 */
#define SLOT_SECTOR_COUNT      7u   /* 含 Trailer 896KB / 128KB = 7 个扇区 */

/*============================================================================
 * 跨模块 Magic Numbers
 *============================================================================*/

#define BOOT_MAGIC             0xB007A55Au   /* Boot 跳转标志 (g_JumpInit) */
#define BL_DFU_MAGIC           0x5555AAAAu   /* DFU 模式入口标志 */
#define BL_ROM_BOOTLOADER      0x1FF09800u   /* STM32H7 系统 Bootloader 地址 */

#ifdef __cplusplus
}
#endif

#endif /* BL_MEMMAP_H */
