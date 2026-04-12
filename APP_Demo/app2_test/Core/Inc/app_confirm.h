/**
  ******************************************************************************
  * @file           : app_confirm.h
  * @brief          : App 侧确认接口
  * @description    : 提供 App 自检通过后确认镜像的功能
  ******************************************************************************
  */

#ifndef __APP_CONFIRM_H
#define __APP_CONFIRM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "trailer.h"   /* tr_rec_t, TR_MAGIC, TR_STATE_*, trailer 函数 */

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  App 自检通过后调用，将当前镜像标记为 CONFIRMED
 * @note   此函数会：
 *         1. 获取当前 active slot 的 trailer 基地址
 *         2. 读取当前镜像的 CRC32
 *         3. 追加写入 CONFIRMED 记录
 * @retval 0=成功, -1=失败
 */
int App_ConfirmSelf(void);

/**
 * @brief  检查当前镜像是否处于 PENDING 状态
 * @retval 1=PENDING, 0=非 PENDING 或无记录
 */
int App_IsPending(void);

/**
 * @brief  检查当前镜像是否已 CONFIRMED
 * @retval 1=CONFIRMED, 0=未确认
 */
int App_IsConfirmed(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIRM_H */
