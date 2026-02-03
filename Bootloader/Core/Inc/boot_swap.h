#ifndef __BOOT_SWAP_H
#define __BOOT_SWAP_H

#include <stdint.h>

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  获取当前 Bank Swap 状态
 * @retval 0=未交换, 1=已交换
 */
uint8_t Boot_GetSwapState(void);

/**
 * @brief  设置 Bank Swap 状态，会触发芯片复位
 * @param  enable: 1=启用交换, 0=禁用交换
 * @note   此函数不会返回，会触发系统复位
 */
void Boot_SetSwapBank(uint32_t enable);

#endif /* __BOOT_SWAP_H */
