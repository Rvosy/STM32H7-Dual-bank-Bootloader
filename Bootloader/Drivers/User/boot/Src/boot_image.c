/**
  ******************************************************************************
  * @file           : boot_image.c
  * @brief          : 镜像验证模块
  * @description    : 提供镜像头校验、向量表校验、CRC校验等功能
  ******************************************************************************
  */

#include "boot_image.h"
#include "crc.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * 外部变量
 *============================================================================*/

extern CRC_HandleTypeDef hcrc;

/*============================================================================
 * 私有函数
 *============================================================================*/

/*============================================================================
 * 公共函数实现
 *============================================================================*/

/**
 * @brief  获取指定 Slot 的镜像头指针
 */
const image_hdr_t* Boot_GetImageHeader(uint32_t slot_base)
{
    return (const image_hdr_t*)slot_base;
}

/**
 * @brief  检查镜像头的 Magic 和版本是否有效
 */
int Boot_CheckMagic(const image_hdr_t* hdr)
{
    return (hdr->magic == IMG_HDR_MAGIC) && (hdr->hdr_version == IMG_HDR_VER);
}

/**
 * @brief  检查向量表是否有效
 */
int Boot_CheckVector(uint32_t app_entry)
{
    uint32_t msp   = *(volatile uint32_t*)app_entry;
    uint32_t reset = *(volatile uint32_t*)(app_entry + 4);

    /* MSP 必须指向有效的 RAM 区域 */
    int msp_ok = ((msp & 0x2FF00000u) == 0x20000000u) ||   /* DTCM */
                 ((msp & 0x2FF00000u) == 0x24000000u);     /* AXI SRAM */

    /* Reset Handler 必须在 Flash 范围内 */
    int reset_ok = (reset >= 0x08000000u) && (reset < 0x08200000u);

    return msp_ok && reset_ok;
}

/**
 * @brief  计算镜像 CRC32
 */
uint32_t Boot_CalcImageCRC(uint32_t base, uint32_t hdr_size, uint32_t img_size)
{
    const uint8_t *p = (const uint8_t *)(base + hdr_size);

    __HAL_CRC_DR_RESET(&hcrc);  /* 复位 CRC 到初始值 */

    uint32_t words = img_size / 4;
    uint32_t tail  = img_size % 4;

    /* 分块计算，每次 512 words = 2KB，避免看门狗超时 */
    const uint32_t CHUNK = 512;
    while (words) {
        uint32_t n = (words > CHUNK) ? CHUNK : words;
        HAL_CRC_Accumulate(&hcrc, (uint32_t *)p, n);
        p += n * 4;
        words -= n;
        /* 可在此处喂狗 IWDG->KR = 0xAAAA; */
    }

    /* 处理尾部非 4 字节对齐的数据 */
    if (tail) {
        uint32_t last = 0xFFFFFFFFu;  /* 0xFF padding */
        memcpy(&last, p, tail);
        HAL_CRC_Accumulate(&hcrc, &last, 1);
    }

    return hcrc.Instance->DR;
}

/**
 * @brief  校验镜像 CRC
 */
int Boot_CheckCRC(uint32_t slot_base, const image_hdr_t* hdr)
{
    /* img_size 为 0 或过大认为无效 */
    if (hdr->img_size == 0 || hdr->img_size > (1024 * 1024 - HDR_SIZE)) {
        printf("[CRC] 0x%08X: invalid size %u\r\n", slot_base, hdr->img_size);
        return 0;
    }
    
    /* 校验前 invalidate DCache，确保读取的是 Flash 实际内容 */
    SCB_InvalidateDCache_by_Addr((uint32_t *)(slot_base + HDR_SIZE), hdr->img_size);
    
    uint32_t calc_crc = Boot_CalcImageCRC(slot_base, HDR_SIZE, hdr->img_size);
    
    if (calc_crc != hdr->img_crc32) {
        printf("[CRC] 0x%08X: FAIL (calc=0x%08X, expect=0x%08X)\r\n", 
               slot_base, calc_crc, hdr->img_crc32);
        return 0;
    }
    
    printf("[CRC] 0x%08X: OK (0x%08X)\r\n", slot_base, calc_crc);
    return 1;
}

/**
 * @brief  语义化版本比较
 */
int Boot_SemverCompare(semver_t a, semver_t b)
{
    if (a.major != b.major) return (a.major > b.major) ? 1 : -1;
    if (a.minor != b.minor) return (a.minor > b.minor) ? 1 : -1;
    if (a.patch != b.patch) return (a.patch > b.patch) ? 1 : -1;
    return 0;  /* build 号不参与比较 */
}

/**
 * @brief  检查指定 Slot 的镜像，返回镜像信息
 */
image_t Boot_InspectImage(uint32_t slot_base)
{
    image_t img = {
        .slot_base = slot_base,
        .app_entry = slot_base + HDR_SIZE,
        .hdr       = Boot_GetImageHeader(slot_base),
        .valid     = 0
    };
    
    /* 校验顺序: Magic -> Vector -> CRC (越往后越耗时) */
    if (!Boot_CheckMagic(img.hdr)) {
        return img;  /* Magic 无效，跳过后续校验 */
    }
    
    if (!Boot_CheckVector(img.app_entry)) {
        return img;  /* 向量表无效 */
    }
    
    if (!Boot_CheckCRC(slot_base, img.hdr)) {
        return img;  /* CRC 校验失败 */
    }
    
    img.valid = 1;
    return img;
}
