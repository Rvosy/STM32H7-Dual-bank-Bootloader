# AGENTS.md

## 项目概述

基于 STM32H743 Dual Bank 架构的 Bootloader，支持双 Slot OTA 升级、Bank Swap 零拷贝切换固件、Trailer 状态机回滚机制和 YMODEM 串口升级。

- **MCU**: STM32H743 (2MB Flash, Dual Bank)
- **IDE**: EIDE (VS Code) + Keil MDK-ARM
- **工具链**: AC5 (ARM Compiler 5)，AC6 也支持
- **调试器**: ST-Link + OpenOCD

## 编译与烧录

### 使用 EIDE (VS Code) 编译

通过 `bfd-eide-build` skill 或 `mcp__bfd-stm32__bfd_eide_build` 工具编译，在对应项目的 `EIDE/` 子目录下执行。编译产物输出至 `EIDE/build/<ProjectName>/`。

### Post-Build：填充镜像头 CRC

App 编译完成后**必须**运行此脚本，将 `img_size` 和 `img_crc32` 回填到 bin 文件：

```bash
py -3 ".\Tools\fill_hdr_crc.py" <input.bin> --out <output.bin> --hdr-size 0x200 --img-size-off 20 --crc-off 24
```

app2_test 的 EIDE 工程已在 `afterBuildTasks` 中配置了此步骤，会自动生成 `*_patched.bin`。

### 烧录

**烧录 Bootloader**（自动镜像写入 Bank2）：

```bash
openocd -f interface/stlink.cfg -f Tools/stm32h7x_dual_boot.cfg \
  -c "program Bootloader.bin 0x08000000 verify; reset run; exit"
```

**烧录 App 到 Slot A（0x08020000）**：

```bash
openocd -f interface/stlink.cfg -f .eide/stm32h7x_dual_bank_app_norun.cfg \
  -c "program app_patched.bin 0x08020000 verify; reset run; exit"
```

**烧录 App 到 Slot B（0x08120000）**：地址改为 `0x08120000`，其余相同。

`stm32h7x_dual_bank_app_norun.cfg` 在烧录前会自动擦除对应 Slot 的 Trailer 扇区，且不执行 reset run（让 Bootloader 决策后再跳转）。

也可使用 `bfd-openocd-flash` skill 或 `bfd-build-flash` skill 进行端到端编译+烧录。

## 内存布局

| 区域 | Bank1 地址 | Bank2 地址 | 大小 |
|------|-----------|-----------|------|
| Bootloader | `0x08000000` | `0x08100000` | 128KB |
| App Slot（Active/Inactive） | `0x08020000` | `0x08120000` | 896KB |
| Trailer | `0x080E0000` | `0x081E0000` | 128KB |

Bank Swap 后，Bank1/Bank2 逻辑地址互换，但 Bootloader 始终在两个 Bank 相同偏移处都有副本，始终从 `0x08020000` 跳入 App。

## 代码架构

### Bootloader 启动流程

`main.c` 入口有两个早期检查（在外设初始化之前）：
1. 若 `g_JumpInit == 0x5555AAAA`（IWDG 复位或手动触发），跳回 Bootloader 本身
2. 若 `Boot_ShouldJump()` 返回真（`g_JumpInit == BOOT_MAGIC`），直接 `Boot_JumpToApp()`

`g_JumpInit` 存放于 DTCM 固定地址 `0x20000000`（`zero_init`），软复位后不清零，用于跨复位传递跳转意图。

外设初始化完成后调用 `Boot_SelectAndJump()`，该函数不返回：
- 通过 `Boot_RollbackDecision()` 执行 Trailer 状态机决策
- 通过 `Boot_ExecuteRollbackAction()` 执行动作（可能触发 Bank Swap 复位）

### 核心模块（`Bootloader/Drivers/User/`）

| 目录 | 文件 | 职责 |
|------|------|------|
| `boot/` | `boot_core.c` | 启动决策、跳转逻辑、Rollback 状态机 |
| `boot/` | `boot_image.c` | 镜像校验（Magic + 向量表 + CRC32） |
| `boot/` | `boot_slots.c` | Active/Inactive Slot 地址计算 |
| `boot/` | `boot_swap.c` | Bank Swap 执行 |
| `boot/` | `trailer.c` | Trailer 扇区读写（追加式写入） |
| `iap/` | `iap_upgrade.c` | YMODEM 升级主流程 |
| `iap/` | `iap_write.c` | Flash 写入封装（256B 对齐） |
| `ymodem/` | `ymodem.c` | YMODEM 协议实现 |
| `lwrb/` | `lwrb.c` | 环形缓冲区（处理 DMA UART 数据） |
| `key/` | `key.c / multi_button.c` | 按键检测 |

### App 侧接口（`APP_Demo/app2_test/Core/Src/`）

- `image_header.c`：定义 `g_image_header`，链接到 `.app_header` 段（头 512B），`img_size` 和 `img_crc32` 由脚本回填
- `app_confirm.c`：实现 `App_ConfirmSelf()` / `App_IsPending()` / `App_IsConfirmed()`

App 入口地址为 `0x08020200`（Slot 基址 `0x08020000` + Header 512B）。

### Trailer 状态机

Trailer 扇区用追加写入方式记录 `tr_rec_t`（32B，`seq` 递增）。状态转换：

```
NEW → PENDING(attempt=1) → CONFIRMED（App 自检通过）
                         → REJECTED（attempt >= MAX_ATTEMPTS，Boot 写入，触发回滚）
```

升级触发条件（全部满足才执行 swap）：
1. Inactive 镜像有效（Magic + CRC 校验通过）
2. Inactive 版本 > Active 版本
3. Inactive Trailer 状态 ≠ REJECTED
4. Inactive Trailer CRC 与镜像 CRC 绑定一致

### YMODEM 升级触发

UART 接收到字符 `'U'` → 擦除 Inactive Slot → 启动 YMODEM 接收 → 写入完成后 `NVIC_SystemReset()`。

## 关键开发注意事项

### -O3 优化陷阱（已知 Bug，详见 Debug记录.md）

`image_header.c`（定义 `g_image_header`）和使用该变量的文件（如 `app_confirm.c`）**必须位于不同的编译单元**，且在使用方以 `extern` 声明。若合并到同一文件，编译器会将 `img_crc32` 的 `const` 初始化值（`0u`）常量传播，导致 `-O3` 下读取结果始终为 0，即使脚本已正确回填 Flash。

### Flash 写入对齐

STM32H7 Flash 写入以 256-bit（32B）为最小单位，写入缓冲区必须 `__attribute__((aligned(32)))`。

### OpenOCD 配置说明

- `Tools/stm32h7x_dual_boot.cfg`：烧录 Bootloader 用，劫持 `program` 命令在写 Bank1 时自动镜像到 Bank2
- `.eide/stm32h7x_dual_bank_app_norun.cfg`：烧录 App 用，自动擦除目标 Slot 的 Trailer，烧录后不 `reset run`

## 项目协作要求

- 只有用户明确要求编写文档、生成 README 或写说明文档时，才允许创建或修改文档。默认不新建 README、设计文档、使用说明或架构说明。
- 所有函数和关键逻辑都必须写清楚中文注释。
- 探索、决策、执行、审查分开进行，一次只处理一种意图。
- 非简单修改先检查是否有更清晰的方案；发现临时拼凑的实现时改进方案。

## Spec 与审批

| 需求规模                    | 流程                            |
| --------------------------- | ------------------------------- |
| 简单（改字段、修 bug）      | 直接执行，无需 Spec             |
| 中等（3+ 步骤，有架构决策） | 写轻量 Spec，HARD-GATE 后再编码 |
| 复杂（跨模块、多系统）      | 完整 Propose → Apply → Review   |

中等及以上复杂度遵守以下要求：

- 使用 plan mode，验证步骤也纳入规划；出问题立即停下重新规划。
- 没有 Spec，不准写代码。Spec 和代码冲突时，以 Spec 为准，代码视为错误；执行中发现偏差，先修 Spec，再修代码。
- Spec 按“现状分析 → 功能点 → 风险与决策”分段输出，每段等用户确认后再继续。
- HARD-GATE：Spec 完整生成后，必须等用户显式确认才能开始编码，确认前禁止修改代码。

## 调研与执行

- 描述代码现状时，每个结论必须标注文件路径和函数名。
- 充分使用 subagent，调研、探索和并行分析交给 subagent；复杂问题增加 subagent 投入。每个 subagent 只负责一件事，减少主 context 负担。

| 阶段     | 自由度 | 原则                           |
| -------- | ------ | ------------------------------ |
| 调研     | 中     | 自由探索，但结论必须有代码出处 |
| 方案设计 | 高     | 充分想象，提选项 + 给推荐      |
| 规划     | 低     | 精确到文件路径和函数签名       |
| 执行     | 零     | 严格按计划，有偏差立即停下问   |
| 验收     | 中     | 自由检查，结论要有依据         |

- 完成前必须展示可验证的证据（编译输出、测试结果或运行日志），必要时对比修改前后的行为差异。

## 任务记录

- 先将计划写入 `tasks/todo.md`，使用可勾选的任务项；完成一项立即标记，每步给出高层次说明，最后在文件末尾添加 review 小节。
- 用户每次纠正后，将模式和防止同类错误的规则写入 `tasks/lessons.md`；每次会话开始时阅读其中的相关规则。
- 对有价值的踩坑和领域发现，主动建议沉淀到项目知识库。
