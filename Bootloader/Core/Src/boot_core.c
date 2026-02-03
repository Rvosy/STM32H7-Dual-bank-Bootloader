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
static int check_upgrade_eligible(const image_t* inactive, const image_t* active,
                                  const tr_rec_t* inactive_tr, int has_inactive_tr);
static int check_trailer_binding(const tr_rec_t* tr, const image_t* img);

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

/**
 * @brief  检查 trailer 记录是否与镜像绑定一致
 * @param  tr: trailer 记录
 * @param  img: 镜像信息
 * @retval 1=绑定一致, 0=不一致或无效
 */
static int check_trailer_binding(const tr_rec_t* tr, const image_t* img)
{
    if (!img->valid) return 0;
    return (tr->img_crc32 == img->hdr->img_crc32);
}

/**
 * @brief  【规则1】检查 inactive 是否满足升级条件
 * @note   可升级条件组合：
 *         1. inactive 镜像 valid (magic+vector+CRC 都 OK)
 *         2. inactive 版本号 > active 版本号 (或 active 无效)
 *         3. inactive trailer 当前不是 REJECTED (针对该镜像)
 *         4. (可选) 若 inactive 已有 trailer，需要 CRC 绑定一致
 * @param  inactive: inactive slot 镜像信息
 * @param  active: active slot 镜像信息
 * @param  inactive_tr: inactive slot 的 trailer 记录
 * @param  has_inactive_tr: 是否有有效的 inactive trailer 记录
 * @retval 1=满足升级条件, 0=不满足
 */
static int check_upgrade_eligible(const image_t* inactive, const image_t* active,
                                  const tr_rec_t* inactive_tr, int has_inactive_tr)
{
    /* 条件1: inactive 镜像必须有效 */
    if (!inactive->valid) {
        return 0;
    }
    
    /* 条件2: inactive 版本必须高于 active (或 active 无效) */
    if (active->valid) {
        if (Boot_SemverCompare(inactive->hdr->ver, active->hdr->ver) <= 0) {
            /* inactive 版本不高于 active，不需要升级 */
            return 0;
        }
    }
    /* else: active 无效，inactive 有效就应该切换 */
    
    /* 条件3: 检查 inactive 是否已被 REJECTED */
    if (has_inactive_tr) {
        /* 只有当 trailer CRC 与当前镜像 CRC 一致时，才认为 trailer 有效 */
        if (check_trailer_binding(inactive_tr, inactive)) {
            if (inactive_tr->state == TR_STATE_REJECTED) {
                /* 该镜像已被标记为 REJECTED，拒绝升级 */
                printf("[Boot] Upgrade blocked: inactive image is REJECTED\r\n");
                return 0;
            }
            /* 如果已经是 CONFIRMED，也不需要重新写 PENDING */
            if (inactive_tr->state == TR_STATE_CONFIRMED) {
                printf("[Boot] Upgrade blocked: inactive image already CONFIRMED (version rollback?)\r\n");
                return 0;
            }
            /* 如果已经是 PENDING，说明正在升级过程中，不要重复写 */
            if (inactive_tr->state == TR_STATE_PENDING) {
                printf("[Boot] Upgrade in progress: inactive already PENDING\r\n");
                /* 返回 1 继续执行 swap，但不需要再写 PENDING */
                return 1;
            }
        }
        /* else: trailer CRC 不匹配，说明是旧镜像的 trailer，忽略 */
    }
    
    return 1;
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
 * @note   实现 MCUboot 风格的 test/confirm/revert 机制
 * 
 *         【规则1】只有满足"可升级条件"时才把 inactive 标为 PENDING：
 *           - inactive 镜像 valid (magic+vector+CRC 都 OK)
 *           - inactive 版本号 > active 版本号 (或 active 无效)
 *           - inactive trailer 当前不是 REJECTED (针对该镜像)
 *           - inactive 的 header CRC 与 trailer 绑定一致 (若有 trailer)
 * 
 *         【规则2】PENDING 计数触发点：
 *           - 每次进入 Boot，发现 active 仍处于 PENDING 且未 CONFIRMED
 *           - 则 attempt++ 并写入一条新记录
 *           - 若 attempt >= MAX_ATTEMPTS：写 REJECTED，然后 swap 回旧版本
 *           - 这与 MCUboot 的语义一致："test 升级若不 confirm 则下次重启 revert"
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
    
    /* 打印调试信息 */
    printf("[Boot] Active   Slot (0x%08lX): %s", (unsigned long)active_slot.base, active.valid ? "valid" : "invalid");
    if (active.valid) {
        printf(", ver=%d.%d.%d, crc=0x%08lX",
               active.hdr->ver.major, active.hdr->ver.minor, active.hdr->ver.patch,
               (unsigned long)active.hdr->img_crc32);
    }
    if (has_active_tr) {
        printf(", trailer: state=0x%08lX, attempt=%lu, crc=0x%08lX", 
               (unsigned long)active_tr.state, (unsigned long)active_tr.attempt, (unsigned long)active_tr.img_crc32);
    }
    printf("\r\n");
    
    printf("[Boot] Inactive Slot (0x%08lX): %s", (unsigned long)inactive_slot.base, inactive.valid ? "valid" : "invalid");
    if (inactive.valid) {
        printf(", ver=%d.%d.%d, crc=0x%08lX",
               inactive.hdr->ver.major, inactive.hdr->ver.minor, inactive.hdr->ver.patch,
               (unsigned long)inactive.hdr->img_crc32);
    }
    if (has_inactive_tr) {
        printf(", trailer: state=0x%08lX, attempt=%lu, crc=0x%08lX", 
               (unsigned long)inactive_tr.state, (unsigned long)inactive_tr.attempt, (unsigned long)inactive_tr.img_crc32);
    }
    printf("\r\n");
    
    /*=========================================================================
     * 阶段 1：两个都无效 → 进入 DFU 模式
     *=========================================================================*/
    if (!active.valid && !inactive.valid) {
        printf("[Boot] No valid image found\r\n");
        return ROLLBACK_DFU_MODE;
    }
    
    /*=========================================================================
     * 阶段 2：【规则2】检查 active 的 PENDING 状态
     *         每次进入 Boot，发现 active 仍处于 PENDING 且未 CONFIRMED，就 attempt++
     *=========================================================================*/
    if (has_active_tr && active.valid && check_trailer_binding(&active_tr, &active)) {
        switch (active_tr.state) {
            case TR_STATE_PENDING:
                /* 
                 * 【规则2核心逻辑】
                 * active 正在试运行，App 还没有调用 App_ConfirmSelf()
                 * 这意味着上一次启动要么崩溃了，要么 App 没来得及确认
                 */
                if (active_tr.attempt >= MAX_ATTEMPTS) {
                    /* 超过最大尝试次数，标记为 REJECTED 并回滚 */
                    printf("[Boot] PENDING attempt=%lu >= MAX_ATTEMPTS=%u\r\n",
                           (unsigned long)active_tr.attempt, MAX_ATTEMPTS);
                    printf("[Boot] Marking as REJECTED, will rollback to old version\r\n");
                    trailer_write_rejected(active_slot, active.hdr->img_crc32);
                    
                    /* 检查是否有可以回滚的旧版本 */
                    if (inactive.valid) {
                        return ROLLBACK_SWAP_TO_OLD;
                    } else {
                        /* 无可回滚版本，只能继续用被 reject 的（危险但无奈） */
                        printf("[Boot] WARNING: No valid inactive, forced to use rejected image\r\n");
                        return ROLLBACK_NONE;
                    }
                }
                
                /* 递增 attempt 计数并写入新记录，继续尝试启动 */
                printf("[Boot] PENDING attempt=%lu -> %lu, continue testing\r\n", 
                       (unsigned long)active_tr.attempt, (unsigned long)(active_tr.attempt + 1));
                trailer_increment_attempt(active_slot, &active_tr);
                return ROLLBACK_CONTINUE_PENDING;
                
            case TR_STATE_CONFIRMED:
                /* 已确认，正常启动。不再检查 inactive 是否有更高版本 */
                /* 因为用户可能故意想用这个确认过的版本 */
                printf("[Boot] Active image is CONFIRMED, booting normally\r\n");
                /* 继续执行阶段3，检查是否有更高版本的 inactive */
                break;
                
            case TR_STATE_REJECTED:
                /* active 已被拒绝（可能是之前升级失败），需要回滚 */
                printf("[Boot] Active image is REJECTED\r\n");
                if (inactive.valid) {
                    printf("[Boot] Rollback to inactive slot\r\n");
                    return ROLLBACK_SWAP_TO_OLD;
                }
                /* inactive 无效，只能继续用被 rejected 的（危险但无奈） */
                printf("[Boot] WARNING: No valid inactive, forced to use rejected image\r\n");
                break;
                
            default:
                /* 未知状态，当作无 trailer 处理 */
                printf("[Boot] Unknown trailer state 0x%08lX, ignoring\r\n", (unsigned long)active_tr.state);
                break;
        }
    } else if (has_active_tr && active.valid) {
        /* trailer CRC 与镜像 CRC 不匹配，说明 trailer 是旧镜像的 */
        printf("[Boot] Active trailer CRC mismatch (0x%08lX != 0x%08lX), ignoring trailer\r\n",
               (unsigned long)active_tr.img_crc32, (unsigned long)active.hdr->img_crc32);
    }
    
    /*=========================================================================
     * 阶段 3：【规则1】检查是否满足升级条件，决定是否切换到 inactive
     *=========================================================================*/
    if (check_upgrade_eligible(&inactive, &active, &inactive_tr, has_inactive_tr)) {
        /* 检查 inactive 是否已经是 PENDING 状态 (正在升级中，之前可能被中断) */
        int already_pending = has_inactive_tr && 
                              check_trailer_binding(&inactive_tr, &inactive) &&
                              inactive_tr.state == TR_STATE_PENDING;
        
        if (!already_pending) {
            /* 写入 PENDING(attempt=1) 到 inactive slot */
            printf("[Boot] Writing PENDING(attempt=1) to inactive slot\r\n");
            trailer_write_pending(inactive_slot, inactive.hdr->img_crc32);
        } else {
            printf("[Boot] Inactive already PENDING, continuing swap\r\n");
        }
        
        printf("[Boot] Swapping to inactive slot (version upgrade)\r\n");
        return ROLLBACK_SWAP_TO_NEW;
    }
    
    /*=========================================================================
     * 阶段 4：无需任何操作，正常启动 active
     *=========================================================================*/
    if (!active.valid) {
        /* 理论上不应该走到这里，但做保护 */
        return ROLLBACK_DFU_MODE;
    }
    
    printf("[Boot] Booting active slot\r\n");
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
