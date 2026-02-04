/**
  ******************************************************************************
  * @file           : iap_upgrade.h
  * @brief          : IAP 升级模块（整合 YMODEM + IAP 写入）
  ******************************************************************************
  */
#ifndef __IAP_UPGRADE_H
#define __IAP_UPGRADE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ringbuf.h"

/**
 * @brief  通过 YMODEM 协议进行固件升级
 * @param  rb: 环形缓冲区指针（用于 UART 接收）
 * @param  timeout_ms: 超时时间 (ms)
 * @retval 0=成功, <0=失败
 */
int IAP_UpgradeViaYmodem(ringbuf_t* rb, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __IAP_UPGRADE_H */
