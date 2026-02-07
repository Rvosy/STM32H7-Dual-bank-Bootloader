#include "key.h"
#include <stdio.h>
#include "main.h"
#include "tim.h"
#include "multi_button.h"


static Button btn0, btn1, btn2, btn3;
extern uint32_t cnt;
static void single_click_handler(Button* btn)
{
    printf("Single Click\r\n");
    if(btn == &btn0) printf("Button 0: Single Click\r\n");
    if(btn == &btn1) printf("Button 1: Single Click\r\n");
    if(btn == &btn2) printf("Button 2: Single Click\r\n");
    if(btn == &btn3) printf("Button 3: Single Click\r\n");
}


static uint8_t read_button_gpio(uint8_t button_id)
{
    switch (button_id) {
        case 1:
            return HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin);
        case 2:
            return HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin);
        case 3:
            return HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin);
        case 4:
            return HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin);
        default:
            return 0;
    }
}


void Key_Init(void)
{
    button_init(&btn0, read_button_gpio, 0, 1);
    button_init(&btn1, read_button_gpio, 0, 2);
    button_init(&btn2, read_button_gpio, 0, 3);
    button_init(&btn3, read_button_gpio, 0, 4);
    button_attach(&btn0, BTN_SINGLE_CLICK, single_click_handler);
    button_attach(&btn1, BTN_SINGLE_CLICK, single_click_handler);
    button_attach(&btn2, BTN_SINGLE_CLICK, single_click_handler);
    button_attach(&btn3, BTN_SINGLE_CLICK, single_click_handler);
    button_start(&btn0);
    button_start(&btn1);
    button_start(&btn2);
    button_start(&btn3);
}

//定时器5定时中断函数
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM5) {
        //cnt++;
        //HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        button_ticks();
    }
}
