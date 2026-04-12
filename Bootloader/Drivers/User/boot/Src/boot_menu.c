/**
  ******************************************************************************
  * @file           : boot_menu.c
  * @brief          : DFU 菜单与升级交互模块
  * @description    : 提供 DFU 菜单显示、按键交互、Ymodem 升级流程
  *                   从 boot_core.c 拆分出来，分离决策逻辑与用户交互
  ******************************************************************************
  */

#include "boot_menu.h"
#include "boot_core.h"
#include "iap_write.h"
#include "iap_upgrade.h"
#include "bl_log.h"
#include "key.h"
#include "lwrb.h"
#include <stdio.h>

/* 外部引用 (定义在 main.c) */
extern lwrb_t uart_rb;
extern void UartDmaRx_ResetPos(void);

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  执行 Ymodem 升级流程
 * @note   擦除 Inactive Slot → 清空 UART 缓冲 → Ymodem 接收 → 复位
 * @retval 0=成功(已触发复位,不会到达), <0=失败
 */
int Boot_YmodemUpgrade(void)
{
    BL_LOG("[Boot] Starting Ymodem upgrade...\r\n");

    /* 擦除 Inactive Slot (含 Trailer) */
    if (IAP_EraseSlot() != 0) {
        BL_LOG("Failed to erase slot!\r\n");
        return -1;
    }

    /* 清空 UART 环形缓冲区和 DMA 位置 */
    lwrb_reset(&uart_rb);
    UartDmaRx_ResetPos();

    /* 执行 Ymodem 接收 */
    int result = IAP_UpgradeViaYmodem(&uart_rb, 2000);
    if (result == 0) {
        BL_LOG("[Boot] Upgrade successful, rebooting...\r\n");
        g_JumpInit = 0;
        NVIC_SystemReset();
        /* 不会到达 */
    }

    BL_LOG("[Boot] Ymodem upgrade failed\r\n");
    return -2;
}

/**
 * @brief  进入 DFU 菜单
 * @note   显示选项菜单，阻塞等待按键操作，不会返回
 */
void Boot_DfuMenu(void)
{
    /* 显示选择菜单 */
    printf("\r\n");
    printf("============================================\r\n");
    printf("  No valid image found. Select an option:\r\n");
    printf("--------------------------------------------\r\n");
    printf("  [KEY0] Ymodem Firmware Upgrade             \r\n");
    printf("  [KEY1] Enter System DFU Mode               \r\n");
    printf("============================================\r\n");
    printf("Waiting for key press...\r\n");

    Key_GetFlag();  /* 清除之前的按键事件 */

    while (1) {
        uint8_t flag = Key_GetFlag();

        if (flag & KEY_FLAG_KEY0) {
            /* KEY0: Ymodem 升级 */
            BL_LOG("[Boot] KEY0 pressed: Starting Ymodem upgrade...\r\n");
            int ym_result = Boot_YmodemUpgrade();
            if (ym_result != 0) {
                BL_LOG("[Boot] Upgrade failed! Press KEY0 to retry.\r\n");
            }
            /* 失败则继续循环等待按键 */
        }

        if (flag & KEY_FLAG_KEY1) {
            /* KEY1: 进入 System DFU 模式 */
            BL_LOG("[Boot] KEY1 pressed: Entering DFU mode...\r\n");
            g_JumpInit = BL_DFU_MAGIC;
            __DSB();
            NVIC_SystemReset();
            /* 不会到达 */
        }
    }
}
