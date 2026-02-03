# STM32H7 Dual Bank Bootloader

基于 STM32H7 系列 Dual Bank 架构的 Bank Swap Bootloader，支持双 App 区域切换，实现安全可靠的 OTA 升级，具备完整的回滚机制。

## ✨ 特性

- **Dual Bank 架构**：利用 STM32H7 硬件 Bank Swap 功能，零拷贝切换固件
- **双 Slot 冗余**：Slot A/B 双镜像，支持故障回滚
- **镜像完整性校验**：Magic + Vector Table + CRC32 三重验证
- **语义化版本管理**：自动选择更高版本的固件启动
- **无缝升级**：新固件写入备用 Bank，验证通过后 Bank Swap 切换
- **自动回滚**：新镜像试运行失败后自动回滚到旧版本 (MCUboot 风格)

## 📦 内存布局

| Bank | 区域 | 地址范围 | 大小 | 说明 |
|------|------|----------|------|------|
| Bank1 | Bootloader | `0x08000000 - 0x0801FFFF` | 128KB | Boot 副本 1 |
| Bank1 | **App 区** | `0x08020000 - 0x080DFFFF` | 768KB | App 镜像 |
| Bank1 | **Trailer** | `0x080E0000 - 0x080FFFFF` | 128KB | 回滚状态记录 |
| Bank2 | Bootloader | `0x08100000 - 0x0811FFFF` | 128KB | Boot 副本 2 |
| Bank2 | **App 区** | `0x08120000 - 0x081DFFFF` | 768KB | App 镜像 |
| Bank2 | **Trailer** | `0x081E0000 - 0x081FFFFF` | 128KB | 回滚状态记录 |

> Bank Swap 后，Bank1/Bank2 的地址映射互换。由于两个 Bank 都有相同的 Bootloader，无论哪个 Bank 在前，Bootloader 都能正常运行并跳转到 `0x08020000` (当前 Bank 的 App 区)

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

## 计划表
- [x] 基础 Bootloader 功能实现
- [x] 镜像头格式定义与校验
- [x] 版本比较与选择逻辑
- [x] Bank Swap 切换实现
- [x] 回滚机制 (Trailer 扇区方案)
- [ ] DFU 模式实现
- [ ] 支持加密固件

## 📄 License

GPL-3.0 License
