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
#include "crc.h"
#include "dma.h"
#include "memorymap.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "bl_memmap.h"
#include "bl_log.h"
#include "boot_core.h"
#include "boot_image.h"
#include "boot_swap.h"
#include "key.h"
#include "lwrb.h"
#include "iap_upgrade.h"
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
/*
 * 跨软复位保持的变量 (zero_init)
 * 放置在 DTCM RAM 固定地址，软复位后不会被清零
 */
uint32_t g_JumpInit __attribute__((at(0x20000000), zero_init));          /* 跳转标志 */
uint32_t g_iwdg_reset_count __attribute__((at(0x20000004), zero_init));  /* 连续 IWDG 复位计数 */
__attribute__((aligned(32))) static uint8_t dma_rx_buf[256];
static uint8_t rb_buf[2048];
lwrb_t uart_rb;
static uint16_t old_pos = 0;
/* UART DMA → lwrb 溢出统计 (volatile 允许在中断里累加) */
static volatile uint32_t s_uart_overflow_count = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
static inline void dcache_invalidate(void* addr, uint32_t len)
{
    /* STM32H7 开启 D-Cache 后，DMA 写入的 RX 缓冲区需要先失效缓存，
       否则 CPU 可能继续读到旧缓存数据，导致串口流解析错位。 */
    uint32_t a = (uint32_t)addr;
    uint32_t start = a & ~31U;
    uint32_t end = (a + len + 31U) & ~31U;
    SCB_InvalidateDCache_by_Addr((uint32_t*)start, end - start);
}
void UartDmaRx_ResetPos(void)
{
    /* 升级前会清空环形缓冲，但 DMA 仍在环形模式持续搬运。
       这里把 old_pos 同步到 DMA 当前写指针，避免把旧数据再搬一次。 */
    uint16_t pos = (uint16_t)(sizeof(dma_rx_buf) - __HAL_DMA_GET_COUNTER(huart1.hdmarx));
    old_pos = pos;
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* 如果上一次请求进入系统 ROM Bootloader，这里在任何外设初始化前直接跳转，
     避免当前 Bootloader 的外设状态干扰 ROM DFU。 */
  if(g_JumpInit == BL_DFU_MAGIC){
    g_JumpInit = 0;
    Boot_JumpToBootloader();
  }
  /* IWDG 连续复位表示 App 可能启动即崩溃，累计到阈值后强制留在 Bootloader。 */
  if(__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST)!=RESET){
      /* IWDG 复位：累加计数器，阈值前清 g_JumpInit 走正常决策（attempt++），
       * 达到阈值说明 App 反复跑飞，强制进 DFU 避免死循环 */
      __HAL_RCC_CLEAR_RESET_FLAGS();
      g_iwdg_reset_count++;
      if (g_iwdg_reset_count >= IWDG_LOCKOUT_THRESHOLD) {
          g_iwdg_reset_count = 0;         /* 消费信号，避免下次再进 */
          g_JumpInit = BL_IWDG_LOCKOUT_MAGIC;  /* 在 Boot_SelectAndJump 中被识别 */
      } else {
          g_JumpInit = 0;
      }
  }
  /* 软复位后检测：如果已选好镜像，直接跳转（此时外设未初始化，状态干净）*/
  if (Boot_ShouldJump()) Boot_JumpToApp();
  
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
  MX_CRC_Init();
  MX_TIM5_Init();
  /* USER CODE BEGIN 2 */

  BL_LOG("===================================================================================================================================================================================\r\n");
  BL_LOG("                                                                                                                                                                                   \r\n");
  BL_LOG("                                                                                                                                   dddddddd                                        \r\n");
  BL_LOG("BBBBBBBBBBBBBBBBB                                              tttt          lllllll                                               d::::::d                                        \r\n");
  BL_LOG("B::::::::::::::::B                                          ttt:::t          l:::::l                                               d::::::d                                        \r\n");
  BL_LOG("B::::::BBBBBB:::::B                                         t:::::t          l:::::l                                               d::::::d                                        \r\n");
  BL_LOG("BB:::::B     B:::::B                                        t:::::t          l:::::l                                               d:::::d                                         \r\n");
  BL_LOG("  B::::B     B:::::B   ooooooooooo      ooooooooooo   ttttttt:::::ttttttt     l::::l    ooooooooooo     aaaaaaaaaaaaa      ddddddddd:::::d     eeeeeeeeeeee    rrrrr   rrrrrrrrr   \r\n");
  BL_LOG("  B::::B     B:::::B oo:::::::::::oo  oo:::::::::::oo t:::::::::::::::::t     l::::l  oo:::::::::::oo   a::::::::::::a   dd::::::::::::::d   ee::::::::::::ee  r::::rrr:::::::::r  \r\n");
  BL_LOG("  B::::BBBBBB:::::B o:::::::::::::::oo:::::::::::::::ot:::::::::::::::::t     l::::l o:::::::::::::::o  aaaaaaaaa:::::a d::::::::::::::::d  e::::::eeeee:::::eer:::::::::::::::::r \r\n");
  BL_LOG("  B:::::::::::::BB  o:::::ooooo:::::oo:::::ooooo:::::otttttt:::::::tttttt     l::::l o:::::ooooo:::::o           a::::ad:::::::ddddd:::::d e::::::e     e:::::err::::::rrrrr::::::r\r\n");
  BL_LOG("  B::::BBBBBB:::::B o::::o     o::::oo::::o     o::::o      t:::::t           l::::l o::::o     o::::o    aaaaaaa:::::ad::::::d    d:::::d e:::::::eeeee::::::e r:::::r     r:::::r\r\n");
  BL_LOG("  B::::B     B:::::Bo::::o     o::::oo::::o     o::::o      t:::::t           l::::l o::::o     o::::o  aa::::::::::::ad:::::d     d:::::d e:::::::::::::::::e  r:::::r     rrrrrrr\r\n");
  BL_LOG("  B::::B     B:::::Bo::::o     o::::oo::::o     o::::o      t:::::t           l::::l o::::o     o::::oa::::aaaa::::::ad:::::d     d:::::d e::::::eeeeeeeeeee   r:::::r             \r\n");
  BL_LOG("  B::::B     B:::::Bo::::o     o::::oo::::o     o::::o      t:::::t    tttttt l::::l o::::o     o::::oa::::a    a:::::ad:::::d     d:::::d e:::::::e            r:::::r            \r\n");
  BL_LOG("BB:::::BBBBBB::::::Bo:::::ooooo:::::oo:::::ooooo:::::o      t::::::tttt:::::tl::::::lo:::::ooooo:::::oa::::a    a:::::ad::::::ddddd::::::dde::::::::e           r:::::r            \r\n");
  BL_LOG("B:::::::::::::::::B o:::::::::::::::oo:::::::::::::::o      tt::::::::::::::tl::::::lo:::::::::::::::oa:::::aaaa::::::a d:::::::::::::::::d e::::::::eeeeeeee   r:::::r            \r\n");
  BL_LOG("B::::::::::::::::B   oo:::::::::::oo  oo:::::::::::oo         tt:::::::::::ttl::::::l oo:::::::::::oo  a::::::::::aa:::a d:::::::::ddd::::d  ee:::::::::::::e   r:::::r            \r\n");
  BL_LOG("BBBBBBBBBBBBBBBBB      ooooooooooo      ooooooooooo             ttttttttttt  llllllll   ooooooooooo     aaaaaaaaaa  aaaa  ddddddddd   ddddd    eeeeeeeeeeeeee   rrrrrrr            \r\n");
  BL_LOG("                                                                                                                                                                                   \r\n");
  BL_LOG("===================================================================================================================================================================================\r\n");
  /* 先启动串口 DMA 和按键，再进入镜像选择流程。
     这样即使没有可启动镜像，DFU 菜单也能立即收按键和串口升级命令。 */
  HAL_TIM_Base_Start_IT(&htim5); 
  lwrb_init(&uart_rb, rb_buf, sizeof(rb_buf));
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, dma_rx_buf, sizeof(dma_rx_buf));
  Key_Init();
  Boot_SelectAndJump();  // 选择镜像并跳转，不会返回


  
  
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint8_t ch_byte;
    /* 主循环只保留一个最小串口入口：收到 'U' 后走 YMODEM 升级。
       正常启动路径已在 Boot_SelectAndJump() 内完成，不会回到这里。 */
    if (lwrb_read(&uart_rb, &ch_byte, 1) == 1) {
        if (ch_byte == 'U') {
          /* 升级前上报溢出计数，帮助定位波特率/缓冲问题 */
          if (s_uart_overflow_count > 0) {
              BL_LOG("[UART] Pre-upgrade overflow: %lu bytes dropped\r\n",
                     (unsigned long)s_uart_overflow_count);
              s_uart_overflow_count = 0;
          }
          /* 擦除 inactive slot */
          if (IAP_EraseSlot() != 0) {
              BL_LOG("Failed to erase slot!\r\n");
          }
          lwrb_reset(&uart_rb);
          UartDmaRx_ResetPos();
          int result = IAP_UpgradeViaYmodem(&uart_rb, 2000);
          /* 升级结束后再报告一次溢出（YMODEM 期间的） */
          if (s_uart_overflow_count > 0) {
              BL_LOG("[UART] In-upgrade overflow: %lu bytes dropped\r\n",
                     (unsigned long)s_uart_overflow_count);
              s_uart_overflow_count = 0;
          }
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

    /* 不直接相信 HAL 传入的 Size，而是重新读取 DMA 剩余计数。
       这样无论回调由 IDLE/半传输/满传输哪个事件触发，都能按“DMA 当前写到哪”
       这一唯一事实把增量数据搬进环形缓冲。 */
    uint16_t pos = (uint16_t)(sizeof(dma_rx_buf) - __HAL_DMA_GET_COUNTER(huart->hdmarx));
    dcache_invalidate(dma_rx_buf, sizeof(dma_rx_buf));
    if (pos != old_pos) {
        /* lwrb_write 返回实际写入字节数；若小于请求值，说明环形缓冲满，累加溢出计数 */
        size_t req, wrote;
        if (pos > old_pos) {
            /* DMA 写指针尚未回卷，直接搬运 old_pos..pos 区间。 */
            req   = pos - old_pos;
            wrote = lwrb_write(&uart_rb, &dma_rx_buf[old_pos], req);
            if (wrote < req) s_uart_overflow_count += (uint32_t)(req - wrote);
        } else {
            /* DMA 已回卷，先搬运 old_pos..buf_end，再搬运 0..pos。 */
            req   = sizeof(dma_rx_buf) - old_pos;
            wrote = lwrb_write(&uart_rb, &dma_rx_buf[old_pos], req);
            if (wrote < req) s_uart_overflow_count += (uint32_t)(req - wrote);
            if (pos > 0) {
                req   = pos;
                wrote = lwrb_write(&uart_rb, &dma_rx_buf[0], req);
                if (wrote < req) s_uart_overflow_count += (uint32_t)(req - wrote);
            }
        }
        old_pos = pos;
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
