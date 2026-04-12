/**
  ******************************************************************************
  * @file           : bl_flash_ops.h
  * @brief          : STM32H7 Flash 操作统一接口
  * @description    : 提供擦除、写入 Flash 的底层原语，统一 IRQ 保护和 DCache 管理
  *                   供 Bootloader 和 App 共用
  ******************************************************************************
  */

#ifndef __BL_FLASH_OPS_H
#define __BL_FLASH_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "bl_memmap.h"

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  擦除单个 Flash 扇区 (带 IRQ 保护 + DCache 管理)
 * @param  addr: 扇区内任意地址
 * @retval 0=成功, <0=失败
 */
int BL_Flash_EraseSector(uint32_t addr);

/**
 * @brief  写入一个 Flash Word (256-bit = 32B，带 IRQ 保护 + DCache 管理)
 * @param  addr: 目标地址 (必须 32B 对齐)
 * @param  data: 源数据指针 (至少 32B)
 * @retval 0=成功, <0=失败
 */
int BL_Flash_WriteWord(uint32_t addr, const uint8_t* data);

#ifdef __cplusplus
}
#endif

#endif /* __BL_FLASH_OPS_H */
