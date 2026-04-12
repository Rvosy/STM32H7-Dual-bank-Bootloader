#ifndef __BOOT_MENU_H
#define __BOOT_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  进入 DFU 菜单 (按键选择升级方式)
 * @note   此函数包含阻塞式菜单循环，不会返回（除非升级成功触发复位）
 *         - KEY0: 启动 Ymodem 固件升级
 *         - KEY1: 进入 System DFU 模式
 *         升级成功后自动 NVIC_SystemReset()
 */
void Boot_DfuMenu(void);

/**
 * @brief  执行 Ymodem 升级流程
 * @note   擦除 Inactive Slot → 接收固件 → 成功则复位
 * @retval 0=成功(已触发复位), <0=失败(返回调用者)
 */
int Boot_YmodemUpgrade(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_MENU_H */
