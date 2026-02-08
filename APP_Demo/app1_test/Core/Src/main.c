/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "memorymap.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "image_meta.h"
#include "iap_upgrade.h"
#include "lwrb.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 环形缓冲区 (2KB) */
__attribute__((aligned(32))) static uint8_t dma_rx_buf[256];
static uint8_t rb_buf[2048];
static lwrb_t uart_rb;
static uint16_t old_pos = 0;
uint32_t g_JumpInit __attribute__((at(0x20000000), zero_init));  /* 跳转标志 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
void App_PrintVersion(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static inline void dcache_invalidate(void* addr, uint32_t len)
{
    uint32_t a = (uint32_t)addr;
    uint32_t start = a & ~31U;
    uint32_t end = (a + len + 31U) & ~31U;
    SCB_InvalidateDCache_by_Addr((uint32_t*)start, end - start);
}
void UartDmaRx_ResetPos(void)
{
    // 读取 DMA 当前已经写到的位置
    uint16_t pos = (uint16_t)(sizeof(dma_rx_buf) - __HAL_DMA_GET_COUNTER(huart1.hdmarx));
    old_pos = pos;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  //SCB->VTOR = 0x08020200; // 设置向量表偏移地址
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  lwrb_init(&uart_rb, rb_buf, sizeof(rb_buf));
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, dma_rx_buf, sizeof(dma_rx_buf));
  App_PrintVersion();
  App_DebugTrailer();
  if (App_IsPending()) {
    printf("App is in PENDING state.\r\n");
    printf("Confirming app...\r\n");
    if (App_ConfirmSelf() == 0) {
      printf("App confirmed successfully.\r\n");
    } else {
      printf("Failed to confirm app!\r\n");
    }
    
  } else if (App_IsConfirmed()) {
    printf("App is in CONFIRMED state.\r\n");
  } else {
    printf("App is in NEW or REJECTED state.\r\n");
  }

  //测试trailer写入
  // for(int i=0; i<4095; i++) {
  //   if(App_ConfirmSelf()==-1){
  //     printf("App confirm failed at %d\r\n", i);
  //   }
  // }
  //App_DebugTrailer();

  printf("System ready. Send 'U' to start firmware upgrade.\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    //HAL_IWDG_Refresh(&hiwdg1);
    /* 读取一个字符 (环形缓冲区中有数据时才会返回) */
    uint8_t ch_byte;
    if (lwrb_read(&uart_rb, &ch_byte, 1) == 1) {
        //printf("收到: %c\r\n", ch_byte);
        
        if (ch_byte == 'U') {
          /* 擦除 inactive slot */
          if (IAP_EraseSlot() != 0) {
              printf("Failed to erase slot!\r\n");
          }
          lwrb_reset(&uart_rb);
          UartDmaRx_ResetPos();
          int result = IAP_UpgradeViaYmodem(&uart_rb, 2000);
          if (result == 0) {
            g_JumpInit = 0;
            NVIC_SystemReset();
            }
        }
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */



/*--- UART 空闲中断回调 ---*/
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != USART1) return;
    uint16_t pos = (uint16_t)(sizeof(dma_rx_buf) - __HAL_DMA_GET_COUNTER(huart->hdmarx));
    dcache_invalidate(dma_rx_buf, sizeof(dma_rx_buf));
    if (pos != old_pos) {
        if (pos > old_pos) {
            lwrb_write(&uart_rb, &dma_rx_buf[old_pos], pos - old_pos);
        } else {
            lwrb_write(&uart_rb, &dma_rx_buf[old_pos], sizeof(dma_rx_buf) - old_pos);
            if (pos > 0) lwrb_write(&uart_rb, &dma_rx_buf[0], pos);
        }
        old_pos = pos;
    }
    
    // /* 关键：重新启动接收，使 IDLE 中断继续有效 */
    // HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_rx_buf, sizeof(dma_rx_buf));
    // __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);  /* 禁用半传输中断 */
}



//定时器中断回调
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &htim2) {
      //printf("Watchdog refreshed.\r\n");
      //HAL_IWDG_Refresh(&hiwdg1);
    }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
