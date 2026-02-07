---
title: (Debug记录)编译器的-O3优化所导致Bug
tags:
  - 单片机
  - Debug
categories: 
  - Debug记录
top_img: 'https://oss.cialloo.cn/img/84136296_p0.webp'
cover: 'https://oss.cialloo.cn/img/arona.webp'
abbrlink: 
date: 2026-02-06 04:07:31
updated:
---

最近在写 Bootloader 配套的 App 时，遇到了一个非常"灵异"的问题：**开启 `-O3` 后，程序读取 Flash 里的镜像 header 字段，结果永远是 0**。尽管 Flash 里的数据已被脚本正确回填。

这篇记录一下整个踩坑过程，以及最终确定的根因：

**常量传播（constant propagation）+ 编译器优化与"镜像后处理回填"之间的冲突**。


## 背景：镜像 Header 放到 `.app_header`，CRC 由脚本回填

我在 `image_header.c` 中定义了一个镜像头 `g_image_header`，放到 `.app_header` 段，里面包含版本号、CRC 等信息。`img_crc32` 会在编译完成后由 Python 脚本计算并回填到 bin 文件中。

**image_header.c**

```c
__attribute__((section(".app_header"), used, aligned(4)))
const image_hdr_t g_image_header = {
    .magic       = IMG_HDR_MAGIC,
    .hdr_version = IMG_HDR_VER,
    .flags       = 0xFFFFu,
    .ver         = { .major=6u, .minor=2u, .patch=1u, .reserved=0u, .build=123u },
    .img_size    = 0u,   // 后续由脚本回填
    .img_crc32   = 0u,   // 后续由脚本回填
};
```

随后在 `app_confirm.c` 里实现 `App_IsPending()` 函数：

- 首先读取 `g_image_header.img_crc32`
- 与 Bootloader 写入 trailer 的 CRC32 数据比对，同时判断是否处于 PENDING 状态

**app_confirm.c**

```c
extern const image_hdr_t g_image_header;

/**
 * @brief 检查当前镜像是否处于 PENDING 状态
 */
int App_IsPending(void)
{
    static tr_rec_t last_rec;
    uint32_t my_crc32 = g_image_header.img_crc32;

    if (trailer_read_last_app(ACTIVE_TRAILER_BASE, &last_rec) == 0) {
        if (last_rec.state == TR_STATE_PENDING &&
            last_rec.img_crc32 == my_crc32) {
            return 1;
        }
    }
    return 0;
}
```

## 现象：合并文件后，程序永远认为“不处于 Pending”

一开始程序完全正常。直到我为了优化项目结构，把 `app_confirm.c` 和 `image_header.c` 合并到同一个文件里（`image_meta.c`）：

* 程序无法确认自己处于 Pending 状态
* 排查了三个小时
* 最终发现：**通过 `g_image_header.img_crc32` 读到的 CRC 始终为 0**
* 但用工具读回校验，Flash 中 `.app_header` 的 CRC 确实已经回填正确

![](https://oss.cialloo.cn/img/PixPin_2026-02-06_04-56-25.webp)
## 解决办法

在排查了好几个小时后，决定试着改用指针直接读 Flash，结果：**马上恢复正常**。

```c
const uint32_t* header_crc_ptr = (const uint32_t*)(ACTIVE_SLOT_BASE + 24); // 0x08020024
uint32_t my_crc32 = *header_crc_ptr;
```

再把编译选项改为 `-O0`，恢复用 `g_image_header.img_crc32` 读取，也正常。

到这里就基本锁定：**这是 `-O3` 优化引发的"合法但恶心"的行为差异**。

## 根因：`const` + 初始化器可见 ⇒ 常量传播把读取折叠为 `0`

这是典型的：

> **编译器看到它是 `const`，并且初始化器是常量，就把字段当成编译期常量传播/折叠。**

### 编译器视角发生了什么

* `g_image_header` 是一个全局 `const` 对象
* `img_crc32` 的初始化值写死为 `0u`
* 在 C 语义里，**`const` 对象在运行时不会被修改**（修改它违背语言抽象机假设，相关行为可能变成未定义/不可假设）
* 因此在 `-O3`（特别是再叠加 LTO/IPA）下，编译器完全可以把：

```c
uint32_t my_crc32 = g_image_header.img_crc32;
```

优化成：

```c
uint32_t my_crc32 = 0u;
```

甚至直接删除对 Flash 的读取。

### 但我做了什么"编译器不知道的事"

我用 **后处理脚本回填** CRC 到最终镜像里。这一步发生在：

* 编译器生成目标文件之后
* 链接器链接出 ELF/bin 之后
* 编译器的"世界观"之外

所以编译器仍然坚信：`img_crc32` 永远就是 0，它的优化是"合理且合法"的。

> `__attribute__((used))` 只能防止对象被丢弃（dead-strip），**不能阻止字段被常量传播**。


## 为什么"合并文件"才触发？`extern` 的保护作用

这个点非常关键，也是最迷惑人的部分：**同样的代码，拆文件时没问题，合并后就出错**。

原因是：

* **拆文件**：在 `app_confirm.c` 里是 `extern const image_hdr_t g_image_header;`，编译器看不到 `g_image_header` 的初始化器（它在另一个编译单元），所以通常不敢把字段当编译期常量折叠。
* **合并文件**：初始化器就在同一个编译单元里完全可见，`-O3` 就会进行更激进的常量传播。

一句话总结：

> `extern` 让编译器"没法确信"，所以采用保守的优化策略；
> 合并后编译器"全都看见了"，所以采用激进的策略。
