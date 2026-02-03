// image_header.h
#include <stdint.h>

#define IMG_HDR_MAGIC  (0xA5A55A5Au)
#define IMG_HDR_VER    (1u)

#define HDR_SIZE       (0x200u)

// 语义版本：MAJOR.MINOR.PATCH + BUILD(可选)
typedef struct __attribute__((packed, aligned(4))) {
    uint16_t major;     // 用 16 位避免你后续版本号>255时截断
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;  // 对齐/预留
    uint32_t build;     // 仅做构建号标记（不属于 SemVer 核心，但工程里常用）
} semver_t;

typedef struct __attribute__((packed, aligned(4))) {
    uint32_t magic;
    uint16_t hdr_version;
    uint16_t flags;     // 预留：confirmed / rollback 等
    semver_t  ver;      // 语义版本
    uint32_t img_size;  // 建议：不含 header
    uint32_t img_crc32; // 建议：不含 header
} image_hdr_t;

extern const image_hdr_t g_image_header;
