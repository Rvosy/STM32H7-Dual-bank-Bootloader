#ifndef __KEY_H
#define __KEY_H
#include "stm32h7xx_hal.h"

/* 按键事件标志 (位掩码) */
#define KEY_FLAG_NONE   0x00
#define KEY_FLAG_KEY0   0x01
#define KEY_FLAG_KEY1   0x02
#define KEY_FLAG_KEY2   0x04
#define KEY_FLAG_KEY3   0x08

void Key_Init(void);
uint8_t Key_GetFlag(void);     /* 获取并清除按键事件标志 */

#endif /* __KEY_H */

