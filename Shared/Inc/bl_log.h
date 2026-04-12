/**
  ******************************************************************************
  * @file           : bl_log.h
  * @brief          : Bootloader 条件编译日志宏
  * @description    : 通过 BL_DEBUG 宏控制调试输出
  *                   生产构建移除 BL_DEBUG 即可关闭所有调试 printf
  ******************************************************************************
  */

#ifndef BL_LOG_H
#define BL_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BL_DEBUG
  #include <stdio.h>
  #define BL_LOG(fmt, ...)    printf(fmt, ##__VA_ARGS__)
#else
  #define BL_LOG(fmt, ...)    ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* BL_LOG_H */
