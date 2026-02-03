#ifndef IAP_WRITE_H
#define IAP_WRITE_H

#include <stdint.h>

/*============================================================================
 * STM32H7 Flash 常量
 *============================================================================*/

#define IAP_SECTOR_SIZE       0x20000u    /* 128KB per sector */
#define IAP_FLASH_WORD_SIZE   32u         /* 256-bit = 32 bytes per flash word */

/*============================================================================
 * 数据结构
 *============================================================================*/

typedef struct {
  uint32_t base;          /* 目标区域起始地址 */
  uint32_t limit;         /* 目标区域结束地址 (base + size) */
  uint32_t addr;          /* 当前写入地址 */
  uint8_t  buf32[32];     /* 32B 对齐缓冲区 (flash word) */
  uint32_t fill;          /* buf32 中已填充的字节数 */
} iap_writer_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  获取非活动 Slot 的基地址 (用于 IAP 写入目标)
 * @note   根据当前 swap 状态返回 0x08120000 (逻辑地址)
 * @retval 非活动 Slot 基地址
 */
uint32_t IAP_GetInactiveSlotBase(void);

/**
 * @brief  获取非活动 Slot 的大小 (不含 trailer)
 * @retval Slot 可写入大小 (字节)
 */
uint32_t IAP_GetInactiveSlotSize(void);

/**
 * @brief  擦除非活动 Slot 的指定扇区
 * @param  sector_index: 扇区索引 (0-5，相对于 Slot 起始，不含 Bootloader 区域)
 *         - Sector 0: 0x08120000 - 0x0813FFFF (128KB)
 *         - Sector 1: 0x08140000 - 0x0815FFFF (128KB)
 *         - ...
 *         - Sector 5: 0x081C0000 - 0x081DFFFF (128KB)
 *         - Sector 6: Trailer (不应擦除)
 * @retval 0=成功, <0=失败
 */
int IAP_EraseSector(uint32_t sector_index);

/**
 * @brief  擦除非活动 Slot 的全部 App 区域 (不含 trailer)
 * @note   擦除 6 个扇区 = 768KB
 * @retval 0=成功, <0=失败
 */
int IAP_EraseSlot(void);

/**
 * @brief  擦除指定地址范围 (自动计算需要擦除的扇区)
 * @param  start_addr: 起始地址 (必须在非活动 Slot 范围内)
 * @param  size: 要擦除的大小
 * @retval 0=成功, <0=失败
 */
int IAP_EraseRange(uint32_t start_addr, uint32_t size);

/**
 * @brief  开始 IAP 写入会话
 * @param  w: 写入器实例
 * @param  dst_base: 目标起始地址
 * @param  dst_size: 目标区域大小
 * @retval 0=成功, <0=失败
 */
int IAP_Begin(iap_writer_t* w, uint32_t dst_base, uint32_t dst_size);

/**
 * @brief  写入数据 (自动处理 32B 对齐)
 * @param  w: 写入器实例
 * @param  data: 源数据
 * @param  len: 数据长度
 * @retval 0=成功, <0=失败
 */
int IAP_Write(iap_writer_t* w, const uint8_t* data, uint32_t len);

/**
 * @brief  结束 IAP 写入会话 (刷新剩余缓冲区)
 * @param  w: 写入器实例
 * @retval 0=成功, <0=失败
 */
int IAP_End(iap_writer_t* w);

#endif /* IAP_WRITE_H */