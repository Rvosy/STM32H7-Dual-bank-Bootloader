/**
  ******************************************************************************
  * @file           : boot_swap.c
  * @brief          : Bank Swap 管理模块
  * @description    : 提供 STM32H7 双 Bank 交换功能
  ******************************************************************************
  */

#include "boot_swap.h"
#include "boot_core.h"
#include "bl_log.h"
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
 * @brief  内部辅助：尝试写入并 launch Option Bytes
 * @retval 0=成功（实际不会返回，因为 OB_Launch 会复位）, <0=失败
 */
static int try_program_option_bytes(uint32_t enable)
{
    FLASH_OBProgramInitTypeDef ob = {0};
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    HAL_FLASHEx_OBGetConfig(&ob);
    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType   = OB_USER_SWAP_BANK;
    ob.USERConfig = enable ? OB_SWAP_BANK_ENABLE : OB_SWAP_BANK_DISABLE;

    status = HAL_FLASHEx_OBProgram(&ob);
    if (status != HAL_OK) {
        HAL_FLASH_OB_Lock();
        HAL_FLASH_Lock();
        return -1;  /* Program 失败 */
    }

    /* OB_Launch 正常会触发系统复位，不会返回 */
    status = HAL_FLASH_OB_Launch();
    if (status != HAL_OK) {
        HAL_FLASH_OB_Lock();
        HAL_FLASH_Lock();
        return -2;  /* Launch 失败 */
    }

    /* 极端情况下 OB_Launch 未立即复位，手动兜底 */
    return 0;
}

/**
 * @brief  设置 Bank Swap 状态，会触发芯片复位
 * @note   失败处理：先重试 1 次；仍失败则设置 BL_SWAP_FAIL_MAGIC 后软复位，
 *         让 main 早期分支识别后进入 DFU 菜单，避免"砖头"。
 */
void Boot_SetSwapBank(uint32_t enable)
{
    int rc;

    __disable_irq();

    rc = try_program_option_bytes(enable);
    if (rc == 0) {
        /* 成功路径：设置跳转 magic 后软复位 */
        g_JumpInit = BOOT_MAGIC;
        NVIC_SystemReset();
        while (1) {}
    }

    /* === 第一次失败：恢复中断打印日志，给一次重试机会 === */
    __enable_irq();
    BL_LOG("[Swap] OB program failed (rc=%d), retrying...\r\n", rc);
    HAL_Delay(10);
    __disable_irq();

    rc = try_program_option_bytes(enable);
    if (rc == 0) {
        g_JumpInit = BOOT_MAGIC;
        NVIC_SystemReset();
        while (1) {}
    }

    /* === 两次都失败：设置失败 magic 后软复位，让 main 引导到 DFU === */
    __enable_irq();
    BL_LOG("[Swap] OB program failed again (rc=%d), entering DFU on next boot\r\n", rc);

    g_JumpInit = BL_SWAP_FAIL_MAGIC;
    __DSB();
    NVIC_SystemReset();

    /* 永远不会执行到这里 */
    while (1) {}
}
