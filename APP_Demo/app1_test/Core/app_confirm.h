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

#include <stdint.h>

/*============================================================================
 * 常量定义 (与 Bootloader 侧保持一致)
 *============================================================================*/

#define TR_MAGIC          0x544C5252u   /* 'TLRR' trailer magic */
#define TRAILER_SIZE      0x00020000u   /* 128KB trailer 扇区大小 */

/*============================================================================
 * 状态机常量 (与 Bootloader 侧保持一致)
 *============================================================================*/

#define TR_STATE_NEW       0xAAAA0001u   /* 新镜像，尚未尝试启动 */
#define TR_STATE_PENDING   0xAAAA0002u   /* 待确认，正在试运行 */
#define TR_STATE_CONFIRMED 0xAAAA0003u   /* 已确认，App 自检通过 */
#define TR_STATE_REJECTED  0xAAAA0004u   /* 已拒绝，需要回滚 */

typedef uint32_t tr_state_t;

/*============================================================================
 * Trailer 记录结构体 (32B 大小，与 Bootloader 侧保持一致)
 *============================================================================*/

typedef struct __attribute__((packed)) {
  uint32_t magic;       /* 魔数，必须为 TR_MAGIC */
  uint32_t seq;         /* 序列号，递增 */
  uint32_t state;       /* 状态：TR_STATE_xxx */
  uint32_t attempt;     /* 尝试次数 1..N */
  uint32_t img_crc32;   /* 绑定的镜像 CRC32，防止写错槽 */
  uint32_t rsv[3];      /* 保留，padding to 32B */
} tr_rec_t;

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
