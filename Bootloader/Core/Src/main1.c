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
#include "memorymap.h"
#include "usart.h"
#include "gpio.h"

#define BOOT_STATE_MAGIC 0xBEEFCAFEu

typedef struct {
    uint32_t magic;
    uint32_t pending_id;   // img_crc32 of pending image, 0 = none
    uint32_t attempts;     // attempts for pending_id
} boot_state_t;

// 仍然用你现在的固定地址方式（软复位能保留）
// 强烈建议你后续改成 .noinit 段（更标准）
boot_state_t g_bs __attribute__((at(0x20000008), zero_init)); // 注意：占用 0x2000_0008 起

static void boot_state_init_if_needed(void)
{
    if (g_bs.magic != BOOT_STATE_MAGIC) {
        g_bs.magic      = BOOT_STATE_MAGIC;
        g_bs.pending_id = 0;
        g_bs.attempts   = 0;
    }
}

static int write_image_flags(uint32_t slot_base, uint16_t new_flags)
{
    // 32B 对齐，满足 FLASHWORD 源地址对齐要求
    __attribute__((aligned(32))) uint8_t buf[32];
    memcpy(buf, (void*)slot_base, 32);

    // header 结构：magic(4) hdr_ver(2) flags(2) => flags 偏移 6
    buf[6] = (uint8_t)(new_flags & 0xFF);
    buf[7] = (uint8_t)(new_flags >> 8);

    // 只允许 1->0：确保 new_flags 只清位
    uint16_t old_flags = *(uint16_t*)(slot_base + 6);
    if ((~old_flags) & new_flags) {
        printf("[Flash] flags invalid (trying 0->1). old=0x%04X new=0x%04X\r\n", old_flags, new_flags);
        return -1;
    }

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, slot_base, (uint32_t)buf);
    HAL_FLASH_Lock();

    if (st != HAL_OK) {
        printf("[Flash] Write flags failed @0x%08X err=0x%08lX\r\n", slot_base, HAL_FLASH_GetError());
        return -1;
    }

    printf("[Flash] Flags updated: 0x%04X @ 0x%08X\r\n", new_flags, slot_base);
    return 0;
}

static int mark_pending(uint32_t slot_base, const image_hdr_t* hdr)
{
    // 例：pending 位是 BIT0，pending=0 表示 pending
    uint16_t nf = hdr->flags & ~IMG_FLAG_PENDING_MASK;
    return write_image_flags(slot_base, nf);
}

static int mark_rejected(uint32_t slot_base, const image_hdr_t* hdr)
{
    // rejected 位 BIT2，rejected=0 表示 rejected
    uint16_t nf = hdr->flags & ~IMG_FLAG_REJECT_MASK;
    return write_image_flags(slot_base, nf);
}
static uint8_t get_swap_bank_state(void)
{
    FLASH_OBProgramInitTypeDef ob = {0};
    HAL_FLASHEx_OBGetConfig(&ob);
    return (ob.USERConfig & OB_SWAP_BANK_ENABLE) ? 1 : 0;
}

static void BankSwap_Set(uint32_t enable)
{
    FLASH_OBProgramInitTypeDef ob = {0};
    __disable_irq();

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();
    HAL_FLASHEx_OBGetConfig(&ob);

    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType   = OB_USER_SWAP_BANK;
    ob.USERConfig = enable ? OB_SWAP_BANK_ENABLE : OB_SWAP_BANK_DISABLE;

    if (HAL_FLASHEx_OBProgram(&ob) != HAL_OK) while(1){}

    // 会触发复位
    (void)HAL_FLASH_OB_Launch();
    while(1){}
}

typedef struct {
    uint32_t slot_base;       // 0x08020000 or 0x08120000 (逻辑槽)
    uint32_t app_entry;       // slot_base + HDR_SIZE
    const image_hdr_t* hdr;
    int valid;
} image_t;

static int semver_cmp(semver_t a, semver_t b);

static int is_bootable(const image_t* img)
{
    if (!img->valid) return 0;
    if (IMG_IS_REJECTED(img->hdr->flags)) return 0;
    return 1;
}

static const image_t* find_pending_image(const image_t* A, const image_t* B, uint32_t pending_id)
{
    if (pending_id == 0) return NULL;
    if (is_bootable(A) && A->hdr->img_crc32 == pending_id && IMG_IS_PENDING(A->hdr->flags) && !IMG_IS_CONFIRMED(A->hdr->flags))
        return A;
    if (is_bootable(B) && B->hdr->img_crc32 == pending_id && IMG_IS_PENDING(B->hdr->flags) && !IMG_IS_CONFIRMED(B->hdr->flags))
        return B;
    return NULL;
}

static const image_t* pick_best_image(const image_t* A, const image_t* B)
{
    // 1) confirmed 优先
    int Ac = is_bootable(A) && IMG_IS_CONFIRMED(A->hdr->flags);
    int Bc = is_bootable(B) && IMG_IS_CONFIRMED(B->hdr->flags);

    if (Ac && !Bc) return A;
    if (!Ac && Bc) return B;

    // 2) 同为 confirmed 或都非 confirmed：版本高优先
    if (is_bootable(A) && is_bootable(B)) {
        return (semver_cmp(A->hdr->ver, B->hdr->ver) >= 0) ? A : B;
    }
    if (is_bootable(A)) return A;
    if (is_bootable(B)) return B;

    return NULL;
}

#define SLOT_A_BASE 0x08020000u
#define SLOT_B_BASE 0x08120000u
#define APP_A_ENTRY (SLOT_A_BASE + HDR_SIZE)

extern image_t inspect(uint32_t slot_base); // 你已有的 inspect（含 magic/vector/crc）

static void request_run_image(const image_t* target)
{
    // 目标是 A：不 swap；目标是 B：swap
    if (target->slot_base == SLOT_B_BASE) {
        uint8_t cur = get_swap_bank_state();
        BankSwap_Set(cur ? 0 : 1); // toggle -> 复位
        while(1){}
    }

    // 运行 A：走你的“软复位后跳转”路径
    g_AppEntry = APP_A_ENTRY;
    g_JumpInit = BOOT_MAGIC;
    __DSB();
    NVIC_SystemReset();
    while(1){}
}

void Boot_SelectAndJump(void)
{
    boot_state_init_if_needed();

    image_t A = inspect(SLOT_A_BASE);
    image_t B = inspect(SLOT_B_BASE);

    printf("\r\n[Boot] ========== Boot Selection ==========\r\n");
    printf("[Boot] Swap state: %d\r\n", get_swap_bank_state());
    if (A.valid) printf("[Boot] Slot A: ver=%d.%d.%d flags=0x%04X\r\n", A.hdr->ver.major, A.hdr->ver.minor, A.hdr->ver.patch, A.hdr->flags);
    else         printf("[Boot] Slot A: INVALID\r\n");
    if (B.valid) printf("[Boot] Slot B: ver=%d.%d.%d flags=0x%04X\r\n", B.hdr->ver.major, B.hdr->ver.minor, B.hdr->ver.patch, B.hdr->flags);
    else         printf("[Boot] Slot B: INVALID\r\n");

    // 0) 都不可启动 -> DFU
    if (!is_bootable(&A) && !is_bootable(&B)) {
        printf("[Boot] No bootable image -> DFU\r\n");
        while(1){}
    }

    // 1) 如果存在 pending_id，优先处理“试跑/回滚”
    const image_t* pend = find_pending_image(&A, &B, g_bs.pending_id);
    if (pend) {
        printf("[Boot] Pending image found id=0x%08lX attempts=%lu\r\n", g_bs.pending_id, g_bs.attempts);

        if (g_bs.attempts >= IMG_MAX_BOOT_ATTEMPTS) {
            printf("[Boot] Pending exceeded max (%d) -> REJECT + rollback\r\n", IMG_MAX_BOOT_ATTEMPTS);

            // 标记 REJECTED（写 Flash，不再 RAM-only）
            (void)mark_rejected(pend->slot_base, pend->hdr);

            // 清 pending 状态
            g_bs.pending_id = 0;
            g_bs.attempts   = 0;

            // 回滚：选另一个 best（此时 pend 已 rejected，pick_best 会选另一个）
            const image_t* best = pick_best_image(&A, &B);
            printf("[Boot] Rollback choose slot %c\r\n", best->slot_base==SLOT_A_BASE?'A':'B');
            request_run_image(best);
        } else {
            // 继续试跑
            g_bs.attempts++;
            printf("[Boot] Try pending again, attempts=%lu\r\n", g_bs.attempts);
            request_run_image(pend);
        }
    }

    // 2) 没有 pending：选择最佳镜像（confirmed 优先，否则版本高）
    const image_t* best = pick_best_image(&A, &B);
    if (!best) {
        printf("[Boot] No best image -> DFU\r\n");
        while(1){}
    }

    // 3) 如果 best 是 NEW：先标记为 PENDING，再进入试跑状态
    if (IMG_IS_NEW(best->hdr->flags)) {
        printf("[Boot] Best is NEW -> mark PENDING\r\n");
        (void)mark_pending(best->slot_base, best->hdr);

        // 重新读取 header（可选，但建议）
        // 这里简单起见不重读也行，只要 pending_id 用 crc32
    }

    // 4) 如果 best 是 PENDING 且未 confirmed：建立 pending_id & attempts
    if (IMG_IS_PENDING(best->hdr->flags) && !IMG_IS_CONFIRMED(best->hdr->flags)) {
        g_bs.pending_id = best->hdr->img_crc32;
        g_bs.attempts   = 1;
        printf("[Boot] Start pending id=0x%08lX attempts=1\r\n", g_bs.pending_id);
    } else {
        // confirmed 的镜像，清状态
        g_bs.pending_id = 0;
        g_bs.attempts   = 0;
    }

    printf("[Boot] Selected slot %c\r\n", best->slot_base==SLOT_A_BASE?'A':'B');
    request_run_image(best);
}










/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "image_header.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Boot Magic - 用于软复位后识别跳转状态 */
#define BOOT_MAGIC      0x12345678u

/* Flash 分区地址 */
#define SLOT_A_BASE     0x08020000u     /* Bank1 App 区起始 */
#define SLOT_B_BASE     0x08120000u     /* Bank2 App 区起始 */

#define APP_A_ENTRY     (SLOT_A_BASE + HDR_SIZE)
#define APP_B_ENTRY     (SLOT_B_BASE + HDR_SIZE)

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
uint32_t g_JumpInit __attribute__((at(0x20000000), zero_init));  /* 跳转标志 */
uint32_t g_AppEntry __attribute__((at(0x20000004), zero_init));  /* App 入口地址 */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
static void BankSwap_Set(uint32_t enable);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*============================================================================
 * 镜像验证相关函数
 *============================================================================*/

/* 获取指定 Slot 的镜像头指针 */
static inline const image_hdr_t* hdr_at(uint32_t slot_base)
{
    return (const image_hdr_t*)slot_base;
}

/* 检查镜像头的 Magic 和版本是否有效 */
static int magic_ok(const image_hdr_t* h)
{
    return (h->magic == IMG_HDR_MAGIC) && (h->hdr_version == IMG_HDR_VER);
}

/* 检查向量表是否有效 (MSP 和 Reset Handler 地址合法性) */
static int vector_ok(uint32_t app_entry)
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

/* 语义化版本比较: a > b 返回 1, a < b 返回 -1, a == b 返回 0 */
static int semver_cmp(semver_t a, semver_t b)
{
    if (a.major != b.major) return (a.major > b.major) ? 1 : -1;
    if (a.minor != b.minor) return (a.minor > b.minor) ? 1 : -1;
    if (a.patch != b.patch) return (a.patch > b.patch) ? 1 : -1;
    return 0;  /* build 号不参与比较 */
}

/* 镜像信息结构体 */
typedef struct {
    uint32_t slot_base;         /* Slot 基地址 */
    uint32_t app_entry;         /* App 入口地址 (slot_base + HDR_SIZE) */
    const image_hdr_t* hdr;     /* 镜像头指针 */
    int valid;                  /* 镜像是否有效 */
} image_t;

/* 检查指定 Slot 的镜像，返回镜像信息 */
static image_t inspect(uint32_t slot_base)
{
    image_t img = {
        .slot_base = slot_base,
        .app_entry = slot_base + HDR_SIZE,
        .hdr       = hdr_at(slot_base),
        .valid     = 0
    };
    img.valid = magic_ok(img.hdr) && vector_ok(img.app_entry);
    return img;
}

/*============================================================================
 * Bank Swap 相关函数
 *============================================================================*/

/* 获取当前 Bank Swap 状态: 0=未交换, 1=已交换 */
static uint8_t get_swap_bank_state(void)
{
    FLASH_OBProgramInitTypeDef ob = {0};
    HAL_FLASHEx_OBGetConfig(&ob);
    return (ob.USERConfig & OB_SWAP_BANK_ENABLE) ? 1 : 0;
}

/*============================================================================
 * Boot 主逻辑
 *============================================================================*/

/* 选择最佳镜像并跳转 */
void Boot_SelectAndJump(void)
{
    image_t A = inspect(SLOT_A_BASE);  // 0x08020000 - 当前 Bank 的 App
    image_t B = inspect(SLOT_B_BASE);  // 0x08120000 - 另一个 Bank 的 App

    printf("[Boot] Swap state: %d\r\n", get_swap_bank_state());
    printf("[Boot] Slot A (0x%08X): %s\r\n", SLOT_A_BASE, A.valid ? "valid" : "invalid");
    printf("[Boot] Slot B (0x%08X): %s\r\n", SLOT_B_BASE, B.valid ? "valid" : "invalid");

    if (A.valid) {
        printf("[Boot] A ver=%d.%d.%d size=%d bytes CRC32=0x%08X\r\n", 
               A.hdr->ver.major, A.hdr->ver.minor, A.hdr->ver.patch, A.hdr->img_size, A.hdr-> img_crc32);
    }
    if (B.valid) {
        printf("[Boot] B ver=%d.%d.%d size=%d bytes CRC32=0x%08X\r\n", 
               B.hdr->ver.major, B.hdr->ver.minor, B.hdr->ver.patch, B.hdr->img_size, B.hdr->img_crc32);
    }

    // 1. 两个都无效 → 进入 DFU 模式
    if (!A.valid && !B.valid) {
        printf("[Boot] No valid image, entering DFU mode...\r\n");
        while (1) {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            HAL_Delay(100);
        }
    }

    // 2. 判断是否需要 Bank Swap
    //    只有当 B 有效且版本比 A 高时才需要 Swap
    if (B.valid && (!A.valid || semver_cmp(B.hdr->ver, A.hdr->ver) > 0)) {
        printf("[Boot] Slot B is better, need Bank Swap...\r\n");
        
        uint8_t current_swap = get_swap_bank_state();
        BankSwap_Set(current_swap ? 0 : 1);  // 切换状态，会触发复位
        // 不会返回，复位后 A/B 地址映射互换
    }

    // 3. 当前 Slot A 是最佳选择（或唯一有效的），跳转到 0x08020000
    if (!A.valid) {
        // 理论上不应该到这里，但做个保护
        printf("[Boot] Error: Slot A invalid but no swap?\r\n");
        while (1) {}
    }

    printf("[Boot] Jumping to Slot A at 0x%08X...\r\n", A.app_entry);
    
    /* 设置跳转参数，然后软复位 */
    g_AppEntry = APP_A_ENTRY;
    g_JumpInit = BOOT_MAGIC;
    
    __DSB();
    NVIC_SystemReset();
    
    /* 不应到达 */
    while (1) {}
}

/* 设置 Bank Swap 状态，会触发芯片复位 */
static void BankSwap_Set(uint32_t enable)
{
    FLASH_OBProgramInitTypeDef ob = {0};

    __disable_irq();

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    /* 读取当前 Option Bytes 配置 */
    HAL_FLASHEx_OBGetConfig(&ob);

    /* 配置 Swap Bank 选项 */
    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType   = OB_USER_SWAP_BANK;
    ob.USERConfig = enable ? OB_SWAP_BANK_ENABLE : OB_SWAP_BANK_DISABLE;

    if (HAL_FLASHEx_OBProgram(&ob) != HAL_OK) {
        /* 编程失败，死循环 */
        while (1) {}
    }

    /* 触发 Option Bytes 重载，此处会产生复位 */
    if (HAL_FLASH_OB_Launch() != HAL_OK) {
        while (1) {}
    }

    /* 如果 OB_Launch 没有立即复位，手动复位 */
    g_AppEntry = APP_A_ENTRY;
    g_JumpInit = BOOT_MAGIC;
    NVIC_SystemReset();
    
    /* 永远不会执行到这里 */
    while (1) {}
}
/*============================================================================
 * 跳转函数
 *============================================================================*/

/* 跳转到 App (在外设初始化之前调用，状态干净) */
static void JumpToApp(void)
{
    uint32_t entry = g_AppEntry;
    
    /* 设置向量表偏移 */
    SCB->VTOR = entry;
    
    /* 设置主堆栈指针 */
    __set_MSP(*(__IO uint32_t *)entry);
    
    /* 跳转到 Reset Handler */
    ((void (*)(void))(*(__IO uint32_t *)(entry + 4)))();
    
    /* 不应到达这里 */
    while (1) {}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  
  /* 软复位后检测：如果已选好镜像，直接跳转（此时外设未初始化，状态干净）*/
  if (g_JumpInit == BOOT_MAGIC) {
      JumpToApp();
  }
  
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
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */ 

  printf("===================================================================================================================================================================================\r\n");
  printf("                                                                                                                                                                                   \r\n");
  printf("                                                                                                                                   dddddddd                                        \r\n");
  printf("BBBBBBBBBBBBBBBBB                                              tttt          lllllll                                               d::::::d                                        \r\n");
  printf("B::::::::::::::::B                                          ttt:::t          l:::::l                                               d::::::d                                        \r\n");
  printf("B::::::BBBBBB:::::B                                         t:::::t          l:::::l                                               d::::::d                                        \r\n");
  printf("BB:::::B     B:::::B                                        t:::::t          l:::::l                                               d:::::d                                         \r\n");
  printf("  B::::B     B:::::B   ooooooooooo      ooooooooooo   ttttttt:::::ttttttt     l::::l    ooooooooooo     aaaaaaaaaaaaa      ddddddddd:::::d     eeeeeeeeeeee    rrrrr   rrrrrrrrr   \r\n");
  printf("  B::::B     B:::::B oo:::::::::::oo  oo:::::::::::oo t:::::::::::::::::t     l::::l  oo:::::::::::oo   a::::::::::::a   dd::::::::::::::d   ee::::::::::::ee  r::::rrr:::::::::r  \r\n");
  printf("  B::::BBBBBB:::::B o:::::::::::::::oo:::::::::::::::ot:::::::::::::::::t     l::::l o:::::::::::::::o  aaaaaaaaa:::::a d::::::::::::::::d  e::::::eeeee:::::eer:::::::::::::::::r \r\n");
  printf("  B:::::::::::::BB  o:::::ooooo:::::oo:::::ooooo:::::otttttt:::::::tttttt     l::::l o:::::ooooo:::::o           a::::ad:::::::ddddd:::::d e::::::e     e:::::err::::::rrrrr::::::r\r\n");
  printf("  B::::BBBBBB:::::B o::::o     o::::oo::::o     o::::o      t:::::t           l::::l o::::o     o::::o    aaaaaaa:::::ad::::::d    d:::::d e:::::::eeeee::::::e r:::::r     r:::::r\r\n");
  printf("  B::::B     B:::::Bo::::o     o::::oo::::o     o::::o      t:::::t           l::::l o::::o     o::::o  aa::::::::::::ad:::::d     d:::::d e:::::::::::::::::e  r:::::r     rrrrrrr\r\n");
  printf("  B::::B     B:::::Bo::::o     o::::oo::::o     o::::o      t:::::t           l::::l o::::o     o::::oa::::aaaa::::::ad:::::d     d:::::d e::::::eeeeeeeeeee   r:::::r             \r\n");
  printf("  B::::B     B:::::Bo::::o     o::::oo::::o     o::::o      t:::::t    tttttt l::::l o::::o     o::::oa::::a    a:::::ad:::::d     d:::::d e:::::::e            r:::::r            \r\n");
  printf("BB:::::BBBBBB::::::Bo:::::ooooo:::::oo:::::ooooo:::::o      t::::::tttt:::::tl::::::lo:::::ooooo:::::oa::::a    a:::::ad::::::ddddd::::::dde::::::::e           r:::::r            \r\n");
  printf("B:::::::::::::::::B o:::::::::::::::oo:::::::::::::::o      tt::::::::::::::tl::::::lo:::::::::::::::oa:::::aaaa::::::a d:::::::::::::::::d e::::::::eeeeeeee   r:::::r            \r\n");
  printf("B::::::::::::::::B   oo:::::::::::oo  oo:::::::::::oo         tt:::::::::::ttl::::::l oo:::::::::::oo  a::::::::::aa:::a d:::::::::ddd::::d  ee:::::::::::::e   r:::::r            \r\n");
  printf("BBBBBBBBBBBBBBBBB      ooooooooooo      ooooooooooo             ttttttttttt  llllllll   ooooooooooo     aaaaaaaaaa  aaaa  ddddddddd   ddddd    eeeeeeeeeeeeee   rrrrrrr            \r\n");
  printf("A                                                                                                                                                                                  \r\n");
  printf("===================================================================================================================================================================================\r\n");        
  printf("                                                                                                                                                                                   \r\n");
  
  Boot_SelectAndJump();  // 选择镜像并跳转，不会返回


  
  
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    HAL_Delay(50);
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
/* JumpToApp 不再需要，跳转逻辑已移至 main() 开头 */
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
