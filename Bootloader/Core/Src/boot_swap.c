/**
  ******************************************************************************
  * @file           : boot_swap.c
  * @brief          : Bank Swap 管理模块
  * @description    : 提供 STM32H7 双 Bank 交换功能
  ******************************************************************************
  */

#include "boot_swap.h"
#include "boot_core.h"
#include "stm32h7xx_hal.h"

/*============================================================================
 * 外部变量
 *============================================================================*/

extern uint32_t g_JumpInit;

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  获取当前 Bank Swap 状态
 */
uint8_t Boot_GetSwapState(void)
{
    FLASH_OBProgramInitTypeDef ob = {0};
    HAL_FLASHEx_OBGetConfig(&ob);
    return (ob.USERConfig & OB_SWAP_BANK_ENABLE) ? 1 : 0;
}

/**
 * @brief  设置 Bank Swap 状态，会触发芯片复位
 */
void Boot_SetSwapBank(uint32_t enable)
{
    FLASH_OBProgramInitTypeDef ob = {0};

    __disable_irq();

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    /* 读取当前 Option Bytes 配置 */
    HAL_FLASHEx_OBGetConfig(&ob);

    /* 配置 Swap Bank 选项 */
    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType   = OB_USER_SWAP_BANK;
    ob.USERConfig = enable ? OB_SWAP_BANK_ENABLE : OB_SWAP_BANK_DISABLE;

    if (HAL_FLASHEx_OBProgram(&ob) != HAL_OK) {
        /* 编程失败，死循环 */
        while (1) {}
    }

    /* 触发 Option Bytes 重载，此处会产生复位 */
    if (HAL_FLASH_OB_Launch() != HAL_OK) {
        while (1) {}
    }

    /* 如果 OB_Launch 没有立即复位，手动复位 */
    g_JumpInit = BOOT_MAGIC;
    NVIC_SystemReset();
    
    /* 永远不会执行到这里 */
    while (1) {}
}
