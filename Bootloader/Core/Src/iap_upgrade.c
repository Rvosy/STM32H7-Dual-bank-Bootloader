/**
  ******************************************************************************
  * @file           : iap_upgrade.c
  * @brief          : IAP 升级模块实现（整合 YMODEM + IAP 写入）
  ******************************************************************************
  */

#include "iap_upgrade.h"
#include "iap_write.h"
#include "ymodem.h"
#include "lwrb.h"
#include <stdio.h>

/*============================================================================
 * 私有变量
 *============================================================================*/

static iap_writer_t s_iap_writer;

/*============================================================================
 * YMODEM 回调函数
 *============================================================================*/

static int on_begin(const char* name, uint32_t size)
{
    printf("Receiving: %s (%lu bytes)\r\n", name, (unsigned long)size); 
    /* 初始化写入器 */
    return IAP_Begin(&s_iap_writer, IAP_GetInactiveSlotBase(), size);
}

static int on_data(const uint8_t* data, uint32_t len)
{
    /* 写入 Flash */
    return IAP_Write(&s_iap_writer, data, len);
}

static int on_end(void)
{
    /* 刷新缓冲区 */
    IAP_End(&s_iap_writer);
    printf("Firmware written successfully!\r\n");
    return 0;
}

static void on_error(int code)
{
    printf("YMODEM error: %d\r\n", code);
}

/*============================================================================
 * 公共函数
 *============================================================================*/

int IAP_UpgradeViaYmodem(lwrb_t* rb, uint32_t timeout_ms)
{
    ymodem_cb_t callbacks = {
        .on_begin = on_begin,
        .on_data  = on_data,
        .on_end   = on_end,
        .on_error = on_error
    };
    
    int result = Ymodem_Receive(rb, &callbacks, timeout_ms);
    
    return (result == YMODEM_OK) ? 0 : result;
}
