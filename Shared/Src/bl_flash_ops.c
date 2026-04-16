/**
  ******************************************************************************
  * @file           : bl_flash_ops.c
  * @brief          : STM32H7 Flash 操作统一实现
  * @description    : 提供擦除、写入 Flash 的底层原语，统一 IRQ 保护和 DCache 管理
  *                   供 Bootloader 和 App 共用
  ******************************************************************************
  */

#include "bl_flash_ops.h"
#include "bl_log.h"
#include "stm32h7xx_hal.h"
#include <string.h>

/*============================================================================
 * 静态缓冲区 (32B 对齐，用于 Flash 写入)
 *============================================================================*/

static uint8_t s_flash_write_buf[32] __attribute__((aligned(32)));

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief  获取地址对应的 Flash Bank 编号
 */
static uint32_t get_flash_bank(uint32_t addr)
{
    /* 这里依据物理地址判断 Bank。
       调用方传入的是实际要擦写的 Flash 地址，而不是逻辑 slot 编号。 */
    return (addr >= BL_FLASH_BANK2_BASE) ? FLASH_BANK_2 : FLASH_BANK_1;
}

/**
 * @brief  获取地址对应的 Flash Sector 编号 (0-7)
 * @note   STM32H7 每个 Bank 有 8 个 128KB 扇区
 */
static uint32_t get_flash_sector(uint32_t addr)
{
    uint32_t bank_base = (addr >= BL_FLASH_BANK2_BASE) ? BL_FLASH_BANK2_BASE : BL_FLASH_BANK1_BASE;
    uint32_t offset = addr - bank_base;
    return offset / BL_SECTOR_SIZE;
}

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  擦除单个 Flash 扇区 (带 IRQ 保护 + DCache 管理)
 * @param  addr: 扇区内任意地址
 * @retval 0=成功, -1=失败
 */
int BL_Flash_EraseSector(uint32_t addr)
{
    FLASH_EraseInitTypeDef erase_cfg = {0};
    uint32_t sector_error = 0;
    HAL_StatusTypeDef status;

    erase_cfg.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_cfg.Banks        = get_flash_bank(addr);
    erase_cfg.Sector       = get_flash_sector(addr);
    erase_cfg.NbSectors    = 1;
    erase_cfg.VoltageRange = FLASH_VOLTAGE_RANGE_3;  /* 2.7V - 3.6V */

    /* 关中断 + 清缓存，避免执行期间被中断打断或继续读到旧缓存行。 */
    __disable_irq();
    SCB_CleanDCache();

    HAL_FLASH_Unlock();

    status = HAL_FLASHEx_Erase(&erase_cfg, &sector_error);

    HAL_FLASH_Lock();

    /* 擦除后必须同时 clean+invalidate，确保后续 CPU 读到的是擦除后的真实内容。 */
    SCB_CleanInvalidateDCache();
    __enable_irq();

    if (status != HAL_OK) {
        BL_LOG("[Flash] Erase failed: bank=%lu, sector=%lu, error=0x%08lX\r\n",
               (unsigned long)erase_cfg.Banks, (unsigned long)erase_cfg.Sector,
               (unsigned long)sector_error);
        return -1;
    }

    return 0;
}

/**
 * @brief  写入一个 Flash Word (256-bit = 32B，带 IRQ 保护 + DCache 管理)
 * @param  addr: 目标地址 (必须 32B 对齐)
 * @param  data: 源数据指针 (至少 32B)
 * @retval 0=成功, -1=失败
 */
int BL_Flash_WriteWord(uint32_t addr, const uint8_t* data)
{
    HAL_StatusTypeDef status;

    /* HAL 要求写入源按 32B 对齐；统一复制到静态对齐缓冲，调用方就不必自己保证。 */
    memcpy(s_flash_write_buf, data, BL_FLASH_WORD_SIZE);

    /* 写 Flash 期间同样要避免中断和脏缓存干扰。 */
    __disable_irq();
    SCB_CleanDCache();

    HAL_FLASH_Unlock();

    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr, (uint32_t)s_flash_write_buf);

    HAL_FLASH_Lock();

    /* 写完成后刷新缓存，避免随后校验/读取时看到旧值。 */
    SCB_CleanInvalidateDCache();
    __enable_irq();

    if (status != HAL_OK) {
        BL_LOG("[Flash] Write failed at 0x%08lX\r\n", (unsigned long)addr);
        return -1;
    }

    return 0;
}
