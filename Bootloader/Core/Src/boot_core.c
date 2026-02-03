/**
  ******************************************************************************
  * @file           : boot_core.c
  * @brief          : Boot 核心逻辑模块
  * @description    : 提供镜像选择、跳转、回滚状态机等核心 Bootloader 功能
  ******************************************************************************
  */

#include "boot_core.h"
#include "boot_image.h"
#include "boot_slots.h"
#include "boot_swap.h"
#include "trailer.h"
#include "gpio.h"
#include <stdio.h>
#include <string.h>

/*============================================================================
 * 私有函数声明
 *============================================================================*/

static int trailer_write_pending(slot_info_t slot, uint32_t img_crc32);
static int trailer_write_rejected(slot_info_t slot, uint32_t img_crc32);
static int trailer_increment_attempt(slot_info_t slot, const tr_rec_t* current);

/*============================================================================
 * 私有函数实现
 *============================================================================*/

/**
 * @brief  写入 PENDING 状态记录 (attempt=1)
 */
static int trailer_write_pending(slot_info_t slot, uint32_t img_crc32)
{
    static tr_rec_t rec;  /* 使用 static 避免栈对齐问题 */
    memset(&rec, 0, sizeof(rec));
    
    /* 如果扇区满了，先擦除 */
    if (trailer_is_full(slot.trailer_base)) {
        if (trailer_erase(slot.trailer_base) != 0) {
            return -1;
        }
    }
    
    rec.magic     = TR_MAGIC;
    rec.seq       = trailer_next_seq(slot.trailer_base);
    rec.state     = TR_STATE_PENDING;
    rec.attempt   = 1;
    rec.img_crc32 = img_crc32;
    
    return trailer_append(slot.trailer_base, &rec);
}

/**
 * @brief  写入 REJECTED 状态记录
 */
static int trailer_write_rejected(slot_info_t slot, uint32_t img_crc32)
{
    static tr_rec_t rec;  /* 使用 static 避免栈对齐问题 */
    memset(&rec, 0, sizeof(rec));
    
    /* 如果扇区满了，先擦除 */
    if (trailer_is_full(slot.trailer_base)) {
        if (trailer_erase(slot.trailer_base) != 0) {
            return -1;
        }
    }
    
    rec.magic     = TR_MAGIC;
    rec.seq       = trailer_next_seq(slot.trailer_base);
    rec.state     = TR_STATE_REJECTED;
    rec.attempt   = 0;
    rec.img_crc32 = img_crc32;
    
    return trailer_append(slot.trailer_base, &rec);
}

/**
 * @brief  递增 attempt 计数并写入新记录
 */
static int trailer_increment_attempt(slot_info_t slot, const tr_rec_t* current)
{
    static tr_rec_t rec;  /* 使用 static 避免栈对齐问题 */
    memset(&rec, 0, sizeof(rec));
    
    /* 如果扇区满了，先擦除 */
    if (trailer_is_full(slot.trailer_base)) {
        if (trailer_erase(slot.trailer_base) != 0) {
            return -1;
        }
    }
    
    rec.magic     = TR_MAGIC;
    rec.seq       = trailer_next_seq(slot.trailer_base);
    rec.state     = TR_STATE_PENDING;
    rec.attempt   = current->attempt + 1;
    rec.img_crc32 = current->img_crc32;
    
    return trailer_append(slot.trailer_base, &rec);
}

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  检查是否应该立即跳转到 App
 */
int Boot_ShouldJump(void)
{
    return (g_JumpInit == BOOT_MAGIC);
}

/**
 * @brief  跳转到 App (在外设初始化之前调用，状态干净)
 */
void Boot_JumpToApp(void)
{
    /* 获取活动 Slot，计算 App 入口 */
    slot_info_t active = Boot_GetActiveSlot();
    uint32_t entry = Boot_GetAppEntry(active, HDR_SIZE);
    
    /* 设置向量表偏移 */
    SCB->VTOR = entry;
    
    /* 数据同步屏障，确保所有写操作完成 */
    __DSB();
    __ISB();
    
    /* 设置主堆栈指针 */
    __set_MSP(*(__IO uint32_t *)entry);
    
    /* 跳转到 Reset Handler */
    ((void (*)(void))(*(__IO uint32_t *)(entry + 4)))();
    
    /* 不应到达这里 */
    while (1) {}
}

/**
 * @brief  执行回滚状态机决策
 */
rollback_action_t Boot_RollbackDecision(void)
{
    slot_info_t active_slot   = Boot_GetActiveSlot();
    slot_info_t inactive_slot = Boot_GetInactiveSlot();
    
    /* 检查两个 Slot 的镜像 */
    image_t active   = Boot_InspectImage(active_slot.base);
    image_t inactive = Boot_InspectImage(inactive_slot.base);
    
    /* 读取 trailer 记录 (使用 static 避免栈对齐问题) */
    static tr_rec_t active_tr;
    static tr_rec_t inactive_tr;
    memset(&active_tr, 0, sizeof(active_tr));
    memset(&inactive_tr, 0, sizeof(inactive_tr));
    int has_active_tr   = (trailer_read_last(active_slot.trailer_base, &active_tr) == 0);
    int has_inactive_tr = (trailer_read_last(inactive_slot.trailer_base, &inactive_tr) == 0);
    
    printf("[Boot] Active   Slot (0x%08lX): %s", (unsigned long)active_slot.base, active.valid ? "valid" : "invalid");
    if (has_active_tr) {
        printf(", trailer: state=0x%08lX, attempt=%lu, crc=0x%08lX", 
               (unsigned long)active_tr.state, (unsigned long)active_tr.attempt, (unsigned long)active_tr.img_crc32);
    }
    printf("\r\n");
    
    printf("[Boot] Inactive Slot (0x%08lX): %s", (unsigned long)inactive_slot.base, inactive.valid ? "valid" : "invalid");
    if (has_inactive_tr) {
        printf(", trailer: state=0x%08lX, attempt=%lu, crc=0x%08lX", 
               (unsigned long)inactive_tr.state, (unsigned long)inactive_tr.attempt, (unsigned long)inactive_tr.img_crc32);
    }
    printf("\r\n");
    
    /* 1. 两个都无效 → 进入 DFU 模式 */
    if (!active.valid && !inactive.valid) {
        printf("[Boot] No valid image found\r\n");
        return ROLLBACK_DFU_MODE;
    }
    
    /* 2. 检查 active 的 trailer 状态 */
    if (has_active_tr && active.valid) {
        /* 验证 CRC 绑定 */
        if (active_tr.img_crc32 == active.hdr->img_crc32) {
            switch (active_tr.state) {
                case TR_STATE_PENDING:
                    /* 正在试运行的镜像 */
                    if (active_tr.attempt >= MAX_ATTEMPTS) {
                        /* 超过最大尝试次数，标记为 REJECTED 并回滚 */
                        printf("[Boot] PENDING attempt=%lu >= MAX_ATTEMPTS=%u, will REJECT and rollback\r\n",
                               (unsigned long)active_tr.attempt, MAX_ATTEMPTS);
                        trailer_write_rejected(active_slot, active.hdr->img_crc32);
                        return ROLLBACK_SWAP_TO_OLD;
                    }
                    /* 递增 attempt 继续尝试 */
                    printf("[Boot] PENDING attempt=%lu, incrementing and continue\r\n", (unsigned long)active_tr.attempt);
                    trailer_increment_attempt(active_slot, &active_tr);
                    return ROLLBACK_CONTINUE_PENDING;
                    
                case TR_STATE_CONFIRMED:
                    /* 已确认，正常启动 */
                    printf("[Boot] Active image is CONFIRMED\r\n");
                    break;
                    
                case TR_STATE_REJECTED:
                    /* 已拒绝，需要回滚 */
                    printf("[Boot] Active image is REJECTED, need rollback\r\n");
                    if (inactive.valid) {
                        return ROLLBACK_SWAP_TO_OLD;
                    }
                    /* inactive 无效，只能继续用当前的 (虽然被 rejected) */
                    printf("[Boot] WARNING: No valid inactive image, forced to use rejected image\r\n");
                    break;
                    
                default:
                    /* 未知状态，当作无 trailer */
                    break;
            }
        } else {
            printf("[Boot] Trailer CRC mismatch (trailer=0x%08lX, image=0x%08lX), ignoring trailer\r\n",
                   (unsigned long)active_tr.img_crc32, (unsigned long)active.hdr->img_crc32);
        }
    }
    
    /* 3. 检查是否需要切换到 inactive (更高版本) */
    if (inactive.valid) {
        int need_swap = 0;
        
        if (!active.valid) {
            /* active 无效，必须切换到 inactive */
            need_swap = 1;
            printf("[Boot] Active invalid, will swap to inactive\r\n");
        } else if (Boot_SemverCompare(inactive.hdr->ver, active.hdr->ver) > 0) {
            /* inactive 版本更高 */
            /* 检查 inactive 是否已被 REJECTED */
            if (has_inactive_tr && 
                inactive_tr.img_crc32 == inactive.hdr->img_crc32 &&
                inactive_tr.state == TR_STATE_REJECTED) {
                printf("[Boot] Inactive has higher version but is REJECTED, staying with active\r\n");
            } else {
                need_swap = 1;
                printf("[Boot] Inactive version is higher (%d.%d.%d > %d.%d.%d)\r\n",
                       inactive.hdr->ver.major, inactive.hdr->ver.minor, inactive.hdr->ver.patch,
                       active.hdr->ver.major, active.hdr->ver.minor, active.hdr->ver.patch);
            }
        }
        
        if (need_swap) {
            /* 对 inactive 写 PENDING(attempt=1) */
            printf("[Boot] Writing PENDING to inactive slot\r\n");
            trailer_write_pending(inactive_slot, inactive.hdr->img_crc32);
            return ROLLBACK_SWAP_TO_NEW;
        }
    }
    
    /* 4. 无需任何操作，正常启动 active */
    if (!active.valid) {
        return ROLLBACK_DFU_MODE;
    }
    
    return ROLLBACK_NONE;
}

/**
 * @brief  执行回滚决策动作
 */
void Boot_ExecuteRollbackAction(rollback_action_t action)
{
    switch (action) {
        case ROLLBACK_NONE:
        case ROLLBACK_CONTINUE_PENDING:
            /* 正常启动或继续尝试 PENDING */
            printf("[Boot] Jumping to active slot...\r\n");
            g_JumpInit = BOOT_MAGIC;
            __DSB();
            NVIC_SystemReset();
            break;
            
        case ROLLBACK_SWAP_TO_NEW:
        case ROLLBACK_SWAP_TO_OLD:
            /* 需要执行 Bank Swap */
            printf("[Boot] Executing Bank Swap...\r\n");
            {
                uint8_t current_swap = Boot_GetSwapState();
                Boot_SetSwapBank(current_swap ? 0 : 1);
            }
            /* 不会返回，Boot_SetSwapBank 会触发复位 */
            break;
            
        case ROLLBACK_DFU_MODE:
            /* 进入 DFU 模式 */
            printf("[Boot] Entering DFU mode...\r\n");
            while (1) {
                HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
                HAL_Delay(100);
            }
            /* 不会返回 */
    }
    
    /* 不应到达 */
    while (1) {}
}

/**
 * @brief  选择最佳镜像并跳转 (兼容旧接口，内部调用回滚状态机)
 */
void Boot_SelectAndJump(void)
{
    printf("[Boot] === Rollback State Machine ===\r\n");
    printf("[Boot] Swap state: %d\r\n", Boot_GetSwapState());
    
    rollback_action_t action = Boot_RollbackDecision();
    Boot_ExecuteRollbackAction(action);
    
    /* 不应到达 */
    while (1) {}
}
