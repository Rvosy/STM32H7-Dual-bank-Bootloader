/**
  ******************************************************************************
  * @file           : app_confirm.c
  * @brief          : App 侧确认接口实现
  * @description    : 提供 App 自检通过后确认镜像的功能
  *                   使用共享 trailer 模块进行读写操作
  ******************************************************************************
  */

#include "app_confirm.h"
#include "image_header.h"
#include <string.h>

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  App 自检通过后调用，将当前镜像标记为 CONFIRMED
 */
int App_ConfirmSelf(void)
{
    static tr_rec_t last_rec;   /* 使用 static 避免栈对齐问题 */
    static tr_rec_t new_rec;
    memset(&new_rec, 0, sizeof(new_rec));

    /* 获取当前镜像的 CRC32 (来自镜像头) */
    uint32_t my_crc32 = g_image_header.img_crc32;

    /* 读取当前 trailer 最后一条记录 */
    if (trailer_read_last(ACTIVE_TRAILER_BASE, &last_rec) == 0) {
        /* 检查是否已经 CONFIRMED */
        if (last_rec.state == TR_STATE_CONFIRMED &&
            last_rec.img_crc32 == my_crc32) {
            /* 已经确认过了，不需要重复写入 */
            return 0;
        }

        /* 验证 CRC 绑定 */
        if (last_rec.img_crc32 != my_crc32) {
            /* CRC 不匹配，可能是 trailer 数据混乱 */
            /* 继续写入 CONFIRMED，但带上正确的 CRC */
        }
    }

    /* 构造 CONFIRMED 记录 */
    new_rec.magic     = TR_MAGIC;
    new_rec.seq       = trailer_next_seq(ACTIVE_TRAILER_BASE);
    new_rec.state     = TR_STATE_CONFIRMED;
    new_rec.attempt   = 0;  /* CONFIRMED 后 attempt 清零 */
    new_rec.img_crc32 = my_crc32;

    /* 写入 */
    return trailer_append(ACTIVE_TRAILER_BASE, &new_rec);
}

/**
 * @brief  检查当前镜像是否处于 PENDING 状态
 */
int App_IsPending(void)
{
    static tr_rec_t last_rec;  /* 使用 static 避免栈对齐问题 */
    uint32_t my_crc32 = g_image_header.img_crc32;

    if (trailer_read_last(ACTIVE_TRAILER_BASE, &last_rec) == 0) {
        if (last_rec.state == TR_STATE_PENDING &&
            last_rec.img_crc32 == my_crc32) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief  检查当前镜像是否已 CONFIRMED
 */
int App_IsConfirmed(void)
{
    static tr_rec_t last_rec;  /* 使用 static 避免栈对齐问题 */
    uint32_t my_crc32 = g_image_header.img_crc32;

    if (trailer_read_last(ACTIVE_TRAILER_BASE, &last_rec) == 0) {
        if (last_rec.state == TR_STATE_CONFIRMED &&
            last_rec.img_crc32 == my_crc32) {
            return 1;
        }
    }

    return 0;
}
