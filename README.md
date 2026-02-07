# STM32H7 Dual Bank Bootloader

基于 STM32H7 系列 Dual Bank 架构的 Bank Swap Bootloader，支持双 App 区域切换，实现安全可靠的 OTA 升级，具备完整的回滚机制和 YMODEM 串口升级功能。

## ✨ 特性

- **Dual Bank 架构**：利用 STM32H7 硬件 Bank Swap 功能，零拷贝切换固件
- **双 Slot 冗余**：Slot A/B 双镜像，支持故障回滚
- **镜像完整性校验**：Magic + Vector Table + CRC32 三重验证
- **语义化版本管理**：自动选择更高版本的固件启动
- **无缝升级**：新固件写入备用 Bank，验证通过后 Bank Swap 切换
- **自动回滚**：新镜像试运行失败后自动回滚到旧版本 (MCUboot 风格)
- **YMODEM 升级**：支持通过串口使用 YMODEM 协议进行固件升级
- **环形缓冲区**：集成轻量级环形缓冲区库 (lwrb)，高效处理串口数据
- **按键交互**：支持多按键检测，可触发 DFU 模式等功能

## 📁 项目结构

```
STM32 Bootloader/
├── Bootloader/           # Bootloader 主程序
│   ├── Core/
│   │   ├── Inc/          # 头文件
│   │   │   ├── boot_core.h         # Boot 核心逻辑
│   │   │   ├── boot_image.h        # 镜像校验
│   │   │   ├── boot_slots.h        # Slot 管理
│   │   │   ├── boot_swap.h         # Bank Swap
│   │   │   ├── trailer.h           # Trailer 状态管理
│   │   │   ├── image_header.h      # 镜像头定义
│   │   │   ├── iap_upgrade.h       # IAP 升级 (YMODEM)
│   │   │   ├── iap_write.h         # Flash 写入
│   │   │   ├── ymodem.h            # YMODEM 协议
│   │   │   ├── lwrb.h              # 环形缓冲区
│   │   │   └── multi_button.h      # 多按键库
│   │   └── Src/           # 源文件
│   │       ├── boot_core.c         # Boot 核心逻辑
│   │       ├── boot_image.c        # 镜像校验
│   │       ├── boot_slots.c        # Slot 管理
│   │       ├── boot_swap.c         # Bank Swap
│   │       ├── trailer.c           # Trailer 状态管理
│   │       ├── iap_upgrade.c       # IAP 升级 (YMODEM)
│   │       ├── iap_write.c         # Flash 写入
│   │       ├── ymodem.c            # YMODEM 协议
│   │       ├── lwrb.c              # 环形缓冲区
│   │       └── multi_button.c      # 多按键库
│   ├── MDK-ARM/           # Keil 工程
│   ├── EIDE/              # EIDE (VS Code) 工程
│   └── Bootloader.ioc     # STM32CubeMX 配置
│
├── APP_Demo/             # App 示例程序
│   ├── app1_test/        # App1 示例
│   │   └── Core/
│   │       ├── app_confirm.c/h    # App 确认 API
│   │       └── image_meta.c/h     # 镜像元数据
│   └── app2_test/        # App2 示例
│
├── Tools/                # 工具脚本
│   ├── fill_hdr_crc.py   # 镜像头 CRC 填充脚本
│   ├── stm32h7x_dual_boot.cfg         # OpenOCD 双 Bank 配置
│   └── stm32h7x_dual_bank_app_norun.cfg
│
├── .eide/                # EIDE 配置
├── Debug记录.md          # 开发调试记录
├── README.md             # 本文档
└── LICENSE               # GPL-3.0 协议
```

## 📦 内存布局

| Bank | 区域 | 地址范围 | 大小 | 说明 |
|------|------|----------|------|------|
| Bank1 | Bootloader | `0x08000000 - 0x0801FFFF` | 128KB | Boot 副本 1 |
| Bank1 | **App 区** | `0x08020000 - 0x080DFFFF` | 768KB | App 镜像 |
| Bank1 | **Trailer** | `0x080E0000 - 0x080FFFFF` | 128KB | 回滚状态记录 |
| Bank2 | Bootloader | `0x08100000 - 0x0811FFFF` | 128KB | Boot 副本 2 |
| Bank2 | **App 区** | `0x08120000 - 0x081DFFFF` | 768KB | App 镜像 |
| Bank2 | **Trailer** | `0x081E0000 - 0x081FFFFF` | 128KB | 回滚状态记录 |

> **说明**：Bank Swap 后，Bank1/Bank2 的地址映射互换。由于两个 Bank 都有相同的 Bootloader，无论哪个 Bank 在前，Bootloader 都能正常运行并跳转到 `0x08020000` (当前 Bank 的 App 区)

## 🔧 镜像头格式

每个 App 镜像前需包含 512 字节 (`0x200`) 的镜像头：

```c
typedef struct {
    uint32_t magic;       // 固定 0xA5A55A5A
    uint16_t hdr_version; // Header 版本号
    uint16_t flags;       // 预留标志位
    semver_t ver;         // 语义版本 (major.minor.patch)
    uint32_t img_size;    // 镜像大小 (不含 header)
    uint32_t img_crc32;   // 镜像 CRC32 (不含 header)
} image_hdr_t;
```

## 🚀 启动流程

```
┌──────────────┐
│   复位启动   │
└──────┬───────┘
       ▼
┌──────────────┐
│ 检查跳转标志 │──── 有效 ───▶ 直接跳转 App
└──────┬───────┘
       │ 无效
       ▼
┌──────────────────┐
│ 读取 Trailer 状态 │
│ 校验 Active/      │
│ Inactive Slot     │
└──────┬───────────┘
       ▼
┌──────────────────┐     A/B 都无效
│  回滚状态机决策  │────────────────▶ 进入 DFU 模式
└──────┬───────────┘
       │
       ▼
  ┌────┴────┐
  │ 决策结果 │
  └────┬────┘
       │
       ├─ PENDING 且 attempt < MAX ─▶ attempt++ → 跳转 App
       │
       ├─ PENDING 且 attempt >= MAX ─▶ REJECTED → Bank Swap 回滚
       │
       ├─ CONFIRMED ─────────────────▶ 跳转 App
       │
       ├─ REJECTED ──────────────────▶ Bank Swap 回滚
       │
       └─ Inactive 版本更高 ─────────▶ 写 PENDING → Bank Swap
```

## 🔄 回滚机制 (Trailer 扇区)

参考 MCUboot 的 test/confirm/revert 思路，实现完整的回滚状态机。

### Trailer 记录格式 (32B)

```c
typedef struct {
    uint32_t magic;       // 固定 0x544C5252 ('TLRR')
    uint32_t seq;         // 序列号，递增
    uint32_t state;       // 状态：NEW/PENDING/CONFIRMED/REJECTED
    uint32_t attempt;     // 尝试次数 1..N
    uint32_t img_crc32;   // 绑定的镜像 CRC32
    uint32_t rsv[3];      // 保留，padding to 32B
} tr_rec_t;
```

### 状态定义

| 状态 | 值 | 说明 |
|------|-----|------|
| `TR_STATE_NEW` | `0xAAAA0001` | 新镜像，尚未尝试启动 |
| `TR_STATE_PENDING` | `0xAAAA0002` | 待确认，正在试运行 |
| `TR_STATE_CONFIRMED` | `0xAAAA0003` | 已确认，App 自检通过 |
| `TR_STATE_REJECTED` | `0xAAAA0004` | 已拒绝，需要回滚 |

### 工程级判定规则

为避免"莫名其妙反复 swap"的问题，实现了两条工程级判定规则：

#### 规则 1：可升级条件检查

只有满足以下**全部条件**时，才会把 inactive 标记为 PENDING 并执行升级：

| # | 条件 | 说明 |
|---|------|------|
| 1 | `inactive.valid == true` | 镜像有效 (magic + vector + CRC 校验通过) |
| 2 | `inactive.version > active.version` | 版本更高 (或 active 无效) |
| 3 | `inactive.trailer.state != REJECTED` | 该镜像未被标记为 REJECTED |
| 4 | `inactive.trailer.crc == inactive.hdr.crc` | Trailer CRC 与镜像 CRC 绑定一致 (防止误判) |

#### 规则 2：PENDING 计数触发点

每次进入 Boot，若发现 **active 仍处于 PENDING 且未 CONFIRMED**：

```
Boot 启动
   ↓
检测到 active.trailer.state == PENDING
   ↓
   ├── attempt >= MAX_ATTEMPTS?
   │       ↓ 是
   │   写入 REJECTED → Bank Swap 回滚到旧版本
   │
   └── attempt < MAX_ATTEMPTS
           ↓
       attempt++ 写入新记录 → 继续启动 App
```

**关键语义**：这与 MCUboot/Mynewt 的行为一致：
- 新镜像首次启动时 `attempt = 1`
- 若 App 崩溃或未调用 `App_ConfirmSelf()`，下次重启 `attempt++`
- 超过 `MAX_ATTEMPTS`（默认 3 次）后自动回滚到旧版本

### 回滚流程

```
新镜像写入 Inactive Slot
         ↓
Boot 检测到 Inactive 版本更高
         ↓
写入 PENDING(attempt=1) → Bank Swap → 复位
         ↓
新镜像启动（作为 Active）
         ↓
App 自检 ─── 成功 ───→ App_ConfirmSelf() 写入 CONFIRMED
         │
         └── 失败/崩溃 ───→ Boot 检测到 PENDING
                                  ↓
                      attempt++ < MAX_ATTEMPTS ───→ 继续尝试
                                  ↓
                      attempt >= MAX_ATTEMPTS
                                  ↓
                      写入 REJECTED → Bank Swap 回滚
```

### App 侧确认 API

```c
// App 自检通过后调用，标记为 CONFIRMED
int App_ConfirmSelf(void);

// 检查当前镜像是否处于 PENDING 状态
int App_IsPending(void);

// 检查当前镜像是否已 CONFIRMED
int App_IsConfirmed(void);
```

**使用示例：**

```c
int main(void) {
    HAL_Init();
    SystemClock_Config();
    
    // 执行 App 自检...
    if (SelfTest_Pass()) {
        // 自检通过，确认镜像
        App_ConfirmSelf();
    }
    
    // 正常运行...
    while (1) {
        // ...
    }
}
```

## 🛠️ 开发环境

- **MCU**: STM32H743 (2MB Flash, Dual Bank)
- **IDE**: Keil MDK-ARM / EIDE (VS Code)
- **HAL**: STM32Cube HAL

## 📝 App 开发注意事项

1. App 链接地址需设置为 `0x08020200` (Slot 基址 + Header 大小)
2. 编译后使用工具在 bin 文件前添加镜像头

## 📡 YMODEM 串口升级

Bootloader 支持通过 YMODEM 协议进行串口固件升级，无需外部编程器。

### 升级流程

```
1. 上电进入 Bootloader
   ↓
2. 检测到 YMODEM 协议 (或通过按键触发)
   ↓
3. 接收镜像数据 → 写入 Inactive Slot
   ↓
4. 校验镜像完整性
   ↓
5. 写入 PENDING 状态
   ↓
6. Bank Swap → 复位
   ↓
7. 启动新固件 (进入 PENDING 状态)
```

### 使用方法

1. **触发升级模式**
   - 通过按键触发（根据配置）
   - 或检测到串口 YMODEM 协议帧

2. **发送固件**
   ```bash
   # 使用 sz 命令 (Linux/Mac)
   sz -b your_app.bin < /dev/ttyUSB0

   # 或使用支持 YMODEM 的终端工具
   # 如 SecureCRT、Tera Term 等
   ```

3. **固件验证**
   - 升级完成后，Bootloader 会校验镜像 CRC
   - 验证通过后执行 Bank Swap

### YMODEM 协议特性

- **可靠传输**：支持校验和/ CRC16 校验
- **断点续传**：支持从断点继续传输
- **自动重传**：数据包错误时自动重传
- **进度显示**：实时显示传输进度

### 相关文件

| 文件 | 说明 |
|------|------|
| [ymodem.c](Bootloader/Core/Src/ymodem.c) | YMODEM 协议实现 |
| [ymodem.h](Bootloader/Core/Inc/ymodem.h) | YMODEM 协议接口 |
| [iap_upgrade.c](Bootloader/Core/Src/iap_upgrade.c) | IAP 升级整合 |
| [iap_write.c](Bootloader/Core/Src/iap_write.c) | Flash 写入封装 |

## 🔨 构建工具

### fill_hdr_crc.py

用于填充镜像头中的 `img_size` 和 `img_crc32` 字段。CRC32 算法与 STM32 硬件 CRC 外设一致 (多项式 `0x04C11DB7`，初始值 `0xFFFFFFFF`，无反转)。

**使用方法：**

```bash
py -3 ".\Tools\fill_hdr_crc.py" <input.bin> --out <output.bin> [options]
```

**参数说明：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `in_bin` | (必填) | 输入 bin 文件 (需包含镜像头) |
| `--out` | 原地覆盖 | 输出 bin 文件路径 |
| `--hdr-size` | `0x200` | 镜像头大小 |
| `--img-size-off` | `20` | `img_size` 字段在头中的偏移 |
| `--crc-off` | `24` | `img_crc32` 字段在头中的偏移 |
| `--pad` | `0xFF` | 尾部对齐填充字节 |

**Keil 后处理配置：**

在 Keil Options → User → After Build 中添加：

```
py -3 ".\Tools\fill_hdr_crc.py" ".\Output\@L.bin" --out ".\Output\@L_patched.bin" --hdr-size 0x200 --img-size-off 20 --crc-off 24
```

**Keil 常用宏：**

| 宏 | 说明 | 示例 |
|-----|------|------|
| `@L` | 工程名 (linker output name) | `Bootloader` |
| `@P` | 工程文件路径 (不含扩展名) | `D:\Project\Bootloader` |
| `@H` | 工程文件所在目录 | `D:\Project\` |
| `@F` | 工程文件名 (不含路径和扩展名) | `Bootloader` |
| `@J` | 工程文件名 (含扩展名) | `Bootloader.uvprojx` |
| `#L` | 链接器输出文件路径 (不含扩展名) | `.\Objects\Bootloader` |
| `#H` | 链接器输出目录 | `.\Objects\` |

## 🔌 OpenOCD 配置

### 双 Bank 镜像编程配置

`[Tools/stm32h7x_dual_boot.cfg](Tools/stm32h7x_dual_boot.cfg)`

该配置实现了当向 `0x08000000` 写入 Bootloader 时，自动镜像写入到 `0x08100000` (Bank2)。

**使用方法：**

```bash
# 使用自定义配置编程
openocd -f interface/stlink.cfg -f Tools/stm32h7x_dual_boot.cfg -c "program Bootloader.bin 0x08000000 verify; reset run; exit"
```

**配置说明：**

```tcl
# 启用 dual bank 并加载标准 target 脚本
set DUAL_BANK 1
source [find target/stm32h7x.cfg]

# 劫持 program 命令，实现镜像写入
rename program _openocd_program

proc program {filename args} {
    # 解析参数...
    # 原生 program 写入到指定地址
    _openocd_program $filename {*}$fwd

    # 如果目标地址是 0x08000000，镜像写入 Bank2
    if {$addr eq "0x08000000"} {
        _openocd_program $filename 0x08100000 verify
    }
}
```

## ⚠️ 开发注意事项

### 编译器优化问题

**问题**：开启 `-O3` 优化后，读取 Flash 中的镜像头字段（如 `img_crc32`）时结果总是初始值（0）。

**根因**：编译器看到 `const` 对象的初始化器是常量（如 `0u`），进行常量传播优化，将读取操作折叠为直接使用该常量值。

**解决方法**：

1. **使用指针直接读取**：
   ```c
   const uint32_t* header_crc_ptr = (const uint32_t*)(ACTIVE_SLOT_BASE + 24);
   uint32_t my_crc32 = *header_crc_ptr;
   ```

2. **使用 `extern` 声明**：
   ```c
   // 在另一个编译单元中定义
   extern const image_hdr_t g_image_header;
   ```

3. **降低优化级别**：使用 `-O0` 或 `-O1`

**详细记录**：参见 [Debug记录.md](Debug记录.md)

### 其他注意事项

1. **镜像头布局**：必须使用 `aligned(4)` 属性确保 4 字节对齐
2. **Flash 对齐**：Flash 写入必须按 256 字节（Bank1）或 128 字节（Bank2）对齐
3. **Trailer 写入**：使用 32 字节对齐的缓冲区，避免写入 Flash 时对齐问题
4. **中断向量表**：App 的中断向量表起始地址必须为 `0x08020200`（Slot 基址 + Header 大小）

## ❓ 常见问题 (FAQ)

### Q1: Bootloader 为什么有两个副本？

**A**: 由于 Bank Swap 后，Bank1/Bank2 的地址映射会互换。为了让无论哪个 Bank 在前，Bootloader 都能正常运行并跳转到 `0x08020000`（当前 Bank 的 App 区），需要在两个 Bank 的相同偏移位置都放置 Bootloader。

### Q2: 如何手动触发进入 DFU 模式？

**A**: 可以通过以下方式触发：
- 检测到按键长按（需根据具体按键配置）
- 串口接收到特定命令
- 两个 Slot 的镜像都无效时自动进入


## 📋 计划表

### 已完成
- [x] 基础 Bootloader 功能实现
- [x] 镜像头格式定义与校验
- [x] 版本比较与选择逻辑
- [x] Bank Swap 切换实现
- [x] 回滚机制 (Trailer 扇区方案)
- [x] YMODEM 串口升级功能
- [x] 环形缓冲区集成
- [x] 多按键交互支持
- [x] App 确认 API 实现
- [x] OpenOCD 双 Bank 配置

### 待完成
- [ ] DFU 模式完整实现
- [ ] 支持加密固件
- [ ] 支持 LZMA/LZ4 压缩固件
- [ ] Web OTA 升级界面
- [ ] 签名验证 (RSA/ECDSA)
- [ ] 多设备批量升级工具

## 📄 License

GPL-3.0 License
