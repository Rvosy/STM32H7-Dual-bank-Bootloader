#ifndef __BOOT_CORE_H
#define __BOOT_CORE_H

#include <stdint.h>

/*============================================================================
 * 常量定义
 *============================================================================*/

#define BOOT_MAGIC        0xB007A55Au   /* Boot 跳转标志魔数 */

/*============================================================================
 * 回滚决策结果枚举
 *============================================================================*/

typedef enum {
    ROLLBACK_NONE = 0,          /* 无需任何操作，可以正常启动 */
    ROLLBACK_SWAP_TO_NEW,       /* 需要切换到新镜像 (inactive -> active) */
    ROLLBACK_SWAP_TO_OLD,       /* 需要回滚到旧镜像 (已 REJECTED) */
    ROLLBACK_CONTINUE_PENDING,  /* 继续尝试 PENDING 镜像 (attempt++) */
    ROLLBACK_DFU_MODE,          /* 进入 DFU 模式（无可用镜像） */
} rollback_action_t;

/*============================================================================
 * 外部变量声明
 *============================================================================*/

/* 跨软复位保持的变量，放置在 DTCM RAM 固定地址 */
extern uint32_t g_JumpInit;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  选择最佳镜像并跳转
 * @note   此函数不会返回
 *         - 如果需要 Bank Swap，会触发系统复位
 *         - 如果无需 Swap，会设置跳转标志并软复位
 *         - 如果没有有效镜像，进入 DFU 模式
 */
void Boot_SelectAndJump(void);

/**
 * @brief  跳转到 App (在外设初始化之前调用，状态干净)
 * @note   此函数不会返回
 *         应在检测到 g_JumpInit == BOOT_MAGIC 后立即调用
 */
void Boot_JumpToApp(void);


void Boot_JumpToBootloader(void);

/**
 * @brief  检查是否应该立即跳转到 App
 * @retval 1=应该跳转, 0=不应该跳转
 */
int Boot_ShouldJump(void);

/**
 * @brief  执行回滚状态机决策
 * @note   读取 active/inactive 的 trailer，根据状态决定下一步动作：
 *         - 若 inactive 版本更高且有效：写 PENDING → 执行 swap
 *         - 若 active 是 PENDING：attempt++ 继续或超限回滚
 *         - 若 active 是 CONFIRMED：正常启动
 *         - 若 active 是 REJECTED：回滚到旧版本
 * @retval rollback_action_t 决策结果
 */
rollback_action_t Boot_RollbackDecision(void);

/**
 * @brief  执行回滚决策动作
 * @param  action: 回滚决策结果
 * @note   根据决策结果执行相应的动作（可能触发复位）
 */
void Boot_ExecuteRollbackAction(rollback_action_t action);


#endif /* __BOOT_CORE_H */
