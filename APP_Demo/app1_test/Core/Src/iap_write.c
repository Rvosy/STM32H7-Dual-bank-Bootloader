/**
  ******************************************************************************
  * @file           : iap_write.c
  * @brief          : IAP (In-Application Programming) 模块
  * @description    : 提供非活动 Bank 的擦除和写入功能，用于固件升级
  ******************************************************************************
  */

#include "iap_write.h"
#include "stm32h7xx_hal.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * 内部常量
 *============================================================================*/

/* Slot 布局 (与 Bootloader 保持一致) */
#define BOOTLOADER_SIZE       0x00020000u   /* Bootloader 占用 128KB */
#define SLOT_TOTAL_SIZE       0x000E0000u   /* Slot 总大小 896KB (1MB - 128KB) */
#define TRAILER_SIZE          0x00020000u   /* Trailer 占用 128KB (最后一个扇区) */
#define APP_SLOT_SIZE         (SLOT_TOTAL_SIZE - TRAILER_SIZE)  /* App 可用 768KB */

/* 逻辑地址 */
#define LOGICAL_SLOT_INACTIVE_BASE  (FLASH_BANK2_BASE + BOOTLOADER_SIZE)  /* 0x08120000 */

/* 每个 Slot 的 App 区域扇区数 (768KB / 128KB = 6 个扇区) */
#define APP_SECTOR_COUNT      6u

/* 包含 Trailer 的总扇区数 (896KB / 128KB = 7 个扇区) */
#define SLOT_SECTOR_COUNT     7u

/*============================================================================
 * 静态缓冲区 (32B 对齐，用于 Flash 写入)
 *============================================================================*/

static uint8_t s_flash_write_buf[32] __attribute__((aligned(32)));

/*============================================================================
 * 内部函数
 *============================================================================*/

/**
 * @brief  获取地址对应的 Flash Bank 编号
 */
static uint32_t get_flash_bank(uint32_t addr)
{
    return (addr >= FLASH_BANK2_BASE) ? FLASH_BANK_2 : FLASH_BANK_1;
}

/**
 * @brief  获取地址对应的 Flash Sector 编号 (0-7)
 * @note   STM32H7 每个 Bank 有 8 个 128KB 扇区
 */
static uint32_t get_flash_sector(uint32_t addr)
{
    uint32_t bank_base = (addr >= FLASH_BANK2_BASE) ? FLASH_BANK2_BASE : FLASH_BANK1_BASE;
    uint32_t offset = addr - bank_base;
    return offset / IAP_SECTOR_SIZE;
}

/**
 * @brief  擦除单个 Flash 扇区
 * @param  addr: 扇区内任意地址
 * @retval HAL_OK=成功
 */
static HAL_StatusTypeDef erase_sector_at(uint32_t addr)
{
    FLASH_EraseInitTypeDef erase_cfg = {0};
    uint32_t sector_error = 0;
    HAL_StatusTypeDef status;
    
    erase_cfg.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_cfg.Banks        = get_flash_bank(addr);
    erase_cfg.Sector       = get_flash_sector(addr);
    erase_cfg.NbSectors    = 1;
    erase_cfg.VoltageRange = FLASH_VOLTAGE_RANGE_3;  /* 2.7V - 3.6V */
    
    /* 禁用中断，清理 D-Cache */
    __disable_irq();
    SCB_CleanDCache();
    
    HAL_FLASH_Unlock();
    
    status = HAL_FLASHEx_Erase(&erase_cfg, &sector_error);
    
    HAL_FLASH_Lock();
    
    /* 清理并使 D-Cache 无效，恢复中断 */
    SCB_CleanInvalidateDCache();
    __enable_irq();
    
    if (status != HAL_OK) {
        printf("[IAP] Erase failed: bank=%lu, sector=%lu, error=0x%08lX\r\n",
               (unsigned long)erase_cfg.Banks, (unsigned long)erase_cfg.Sector,
               (unsigned long)sector_error);
    }
    
    return status;
}

/**
 * @brief  写入 32B flash word
 * @param  addr: 目标地址 (必须 32B 对齐)
 * @param  data: 源数据 (32B)
 * @retval HAL_OK=成功
 */
static HAL_StatusTypeDef write_flash_word(uint32_t addr, const uint8_t* data)
{
    HAL_StatusTypeDef status;
    
    /* 复制到对齐缓冲区 */
    memcpy(s_flash_write_buf, data, IAP_FLASH_WORD_SIZE);
    
    /* 禁用中断，清理 D-Cache */
    __disable_irq();
    SCB_CleanDCache();
    
    HAL_FLASH_Unlock();
    
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr, (uint32_t)s_flash_write_buf);
    
    HAL_FLASH_Lock();
    
    /* 清理并使 D-Cache 无效，恢复中断 */
    SCB_CleanInvalidateDCache();
    __enable_irq();
    
    return status;
}

/*============================================================================
 * 公共函数实现 - 地址查询
 *============================================================================*/

/**
 * @brief  获取非活动 Slot 的基地址
 */
uint32_t IAP_GetInactiveSlotBase(void)
{
    /* 非活动 Slot 始终在逻辑地址 0x08120000 */
    return LOGICAL_SLOT_INACTIVE_BASE;
}

/**
 * @brief  获取非活动 Slot 的 App 区域大小 (不含 trailer)
 */
uint32_t IAP_GetInactiveSlotSize(void)
{
    return APP_SLOT_SIZE;  /* 768KB */
}

/*============================================================================
 * 公共函数实现 - 擦除操作
 *============================================================================*/

/**
 * @brief  内部函数：擦除非活动 Slot 的指定扇区 (含 Trailer)
 */
static int IAP_EraseSectorRaw(uint32_t sector_index)
{
    /* 检查扇区索引有效性 (0-6，含 trailer) */
    if (sector_index >= SLOT_SECTOR_COUNT) {
        printf("[IAP] Invalid sector index: %lu (max=%u)\r\n", 
               (unsigned long)sector_index, SLOT_SECTOR_COUNT - 1);
        return -1;
    }
    
    /* 计算扇区地址 */
    uint32_t sector_addr = LOGICAL_SLOT_INACTIVE_BASE + (sector_index * IAP_SECTOR_SIZE);
    
    printf("[IAP] Erasing sector %lu at 0x%08lX...\r\n", 
           (unsigned long)sector_index, (unsigned long)sector_addr);
    
    if (erase_sector_at(sector_addr) != HAL_OK) {
        return -2;
    }
    
    printf("[IAP] Sector %lu erased OK\r\n", (unsigned long)sector_index);
    return 0;
}

/**
 * @brief  擦除非活动 Slot 的指定扇区 (仅 App 区域，不含 Trailer)
 */
int IAP_EraseSector(uint32_t sector_index)
{
    /* 检查扇区索引有效性 (0-5，不含 trailer) */
    if (sector_index >= APP_SECTOR_COUNT) {
        printf("[IAP] Invalid sector index: %lu (max=%u)\r\n", 
               (unsigned long)sector_index, APP_SECTOR_COUNT - 1);
        return -1;
    }
    
    return IAP_EraseSectorRaw(sector_index);
}

/**
 * @brief  擦除非活动 Slot 的全部区域 (包含 App + Trailer)
 */
int IAP_EraseSlot(void)
{
    printf("[IAP] Erasing inactive slot (0x%08lX, %lu sectors, including trailer)...\r\n",
           (unsigned long)LOGICAL_SLOT_INACTIVE_BASE, (unsigned long)SLOT_SECTOR_COUNT);
    
    for (uint32_t i = 0; i < SLOT_SECTOR_COUNT; i++) {
        if (IAP_EraseSectorRaw(i) != 0) {
            printf("[IAP] Slot erase failed at sector %lu\r\n", (unsigned long)i);
            return -1;
        }
    }
    
    printf("[IAP] Slot erase complete\r\n");
    return 0;
}

/**
 * @brief  擦除指定地址范围
 */
int IAP_EraseRange(uint32_t start_addr, uint32_t size)
{
    uint32_t slot_base = LOGICAL_SLOT_INACTIVE_BASE;
    uint32_t slot_end  = slot_base + APP_SLOT_SIZE;
    
    /* 检查地址范围有效性 */
    if (start_addr < slot_base || start_addr >= slot_end) {
        printf("[IAP] Start address 0x%08lX out of range\r\n", (unsigned long)start_addr);
        return -1;
    }
    
    if (start_addr + size > slot_end) {
        printf("[IAP] Range exceeds slot boundary\r\n");
        return -1;
    }
    
    /* 计算需要擦除的扇区范围 */
    uint32_t first_sector = (start_addr - slot_base) / IAP_SECTOR_SIZE;
    uint32_t last_sector  = (start_addr + size - 1 - slot_base) / IAP_SECTOR_SIZE;
    
    printf("[IAP] Erasing range 0x%08lX - 0x%08lX (sectors %lu-%lu)...\r\n",
           (unsigned long)start_addr, (unsigned long)(start_addr + size - 1),
           (unsigned long)first_sector, (unsigned long)last_sector);
    
    for (uint32_t i = first_sector; i <= last_sector; i++) {
        if (IAP_EraseSector(i) != 0) {
            return -2;
        }
    }
    
    return 0;
}

/*============================================================================
 * 公共函数实现 - 写入操作
 *============================================================================*/

/**
 * @brief  开始 IAP 写入会话
 */
int IAP_Begin(iap_writer_t* w, uint32_t dst_base, uint32_t dst_size)
{
    if (!w) return -1;
    
    uint32_t slot_base = LOGICAL_SLOT_INACTIVE_BASE;
    uint32_t slot_end  = slot_base + APP_SLOT_SIZE;
    
    /* 检查目标地址有效性 */
    if (dst_base < slot_base || dst_base >= slot_end) {
        //printf("[IAP] Invalid base address 0x%08lX\r\n", (unsigned long)dst_base);
        return -2;
    }
    
    if (dst_base + dst_size > slot_end) {
        //printf("[IAP] Size exceeds slot boundary\r\n");
        return -3;
    }
    
    /* 初始化写入器 */
    w->base  = dst_base;
    w->limit = dst_base + dst_size;
    w->addr  = dst_base;
    w->fill  = 0;
    memset(w->buf32, 0xFF, sizeof(w->buf32));  /* 填充 0xFF */
    
    // printf("[IAP] Write session started: 0x%08lX - 0x%08lX\r\n",
    //        (unsigned long)w->base, (unsigned long)w->limit);
    
    return 0;
}

/**
 * @brief  写入数据
 */
int IAP_Write(iap_writer_t* w, const uint8_t* data, uint32_t len)
{
    if (!w || !data) return -1;
    
    while (len > 0) {
        /* 检查是否超出边界 */
        if (w->addr >= w->limit && w->fill == 0) {
            printf("[IAP] Write overflow\r\n");
            return -2;
        }
        
        /* 填充缓冲区 */
        uint32_t space = IAP_FLASH_WORD_SIZE - w->fill;
        uint32_t copy_len = (len < space) ? len : space;
        
        memcpy(&w->buf32[w->fill], data, copy_len);
        w->fill += copy_len;
        data    += copy_len;
        len     -= copy_len;
        
        /* 缓冲区满，写入 Flash */
        if (w->fill == IAP_FLASH_WORD_SIZE) {
            if (write_flash_word(w->addr, w->buf32) != HAL_OK) {
                printf("[IAP] Write failed at 0x%08lX\r\n", (unsigned long)w->addr);
                return -3;
            }
            
            w->addr += IAP_FLASH_WORD_SIZE;
            w->fill = 0;
            memset(w->buf32, 0xFF, sizeof(w->buf32));  /* 重置为 0xFF */
        }
    }
    
    return 0;
}

/**
 * @brief  结束 IAP 写入会话
 */
int IAP_End(iap_writer_t* w)
{
    if (!w) return -1;
    
    /* 如果缓冲区有剩余数据，补齐 0xFF 后写入 */
    if (w->fill > 0) {
        /* buf32 已经预填充 0xFF，直接写入 */
        if (write_flash_word(w->addr, w->buf32) != HAL_OK) {
            printf("[IAP] Final write failed at 0x%08lX\r\n", (unsigned long)w->addr);
            return -2;
        }
        
        w->addr += IAP_FLASH_WORD_SIZE;
        w->fill = 0;
    }
    
    printf("[IAP] Write session complete: %lu bytes written\r\n",
           (unsigned long)(w->addr - w->base));
    
    return 0;
}
