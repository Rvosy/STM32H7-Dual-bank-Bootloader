#ifndef __BOOT_IMAGE_H
#define __BOOT_IMAGE_H

#include <stdint.h>
#include "image_header.h"

/*============================================================================
 * 说明
 *============================================================================*/
/*
 * boot_image 模块只负责对任意给定的 slot_base 进行镜像验证，
 * 不关心 A/B Slot、不关心 Bank Swap 状态。
 * 
 * Slot 地址映射由 boot_slots 模块负责。
 */

/*============================================================================
 * 数据类型定义
 *============================================================================*/

/* 镜像信息结构体 */
typedef struct {
    uint32_t slot_base;         /* Slot 基地址 */
    uint32_t app_entry;         /* App 入口地址 (slot_base + HDR_SIZE) */
    const image_hdr_t* hdr;     /* 镜像头指针 */
    int valid;                  /* 镜像是否有效 */
} image_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  获取指定 Slot 的镜像头指针
 * @param  slot_base: Slot 基地址
 * @retval 镜像头指针
 */
const image_hdr_t* Boot_GetImageHeader(uint32_t slot_base);

/**
 * @brief  检查镜像头的 Magic 和版本是否有效
 * @param  hdr: 镜像头指针
 * @retval 1=有效, 0=无效
 */
int Boot_CheckMagic(const image_hdr_t* hdr);

/**
 * @brief  检查向量表是否有效 (MSP 和 Reset Handler 地址合法性)
 * @param  app_entry: App 入口地址
 * @retval 1=有效, 0=无效
 */
int Boot_CheckVector(uint32_t app_entry);

/**
 * @brief  计算镜像 CRC32
 * @param  base: Slot 基地址 (镜像头起始)
 * @param  hdr_size: 镜像头大小
 * @param  img_size: 镜像体大小 (不含镜像头)
 * @retval CRC32 计算结果
 */
uint32_t Boot_CalcImageCRC(uint32_t base, uint32_t hdr_size, uint32_t img_size);

/**
 * @brief  校验镜像 CRC
 * @param  slot_base: Slot 基地址
 * @param  hdr: 镜像头指针
 * @retval 1=校验通过, 0=校验失败
 */
int Boot_CheckCRC(uint32_t slot_base, const image_hdr_t* hdr);

/**
 * @brief  语义化版本比较
 * @param  a: 版本 A
 * @param  b: 版本 B
 * @retval a > b 返回 1, a < b 返回 -1, a == b 返回 0
 */
int Boot_SemverCompare(semver_t a, semver_t b);

/**
 * @brief  检查指定 Slot 的镜像，返回镜像信息
 * @param  slot_base: Slot 基地址
 * @retval 镜像信息结构体
 */
image_t Boot_InspectImage(uint32_t slot_base);

#endif /* __BOOT_IMAGE_H */
