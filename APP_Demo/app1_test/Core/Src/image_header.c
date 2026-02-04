#include "image_header.h"

__attribute__((section(".app_header"), used, aligned(4)))
const image_hdr_t g_image_header = {
    .magic       = IMG_HDR_MAGIC,
    .hdr_version = IMG_HDR_VER,
    .flags       = 0xFFFFu,
    .ver         = { .major=1u, .minor=2u, .patch=1u, .reserved=0u, .build=123u },
    .img_size    = 0u,   // 后续可由升级/脚本回填
    .img_crc32   = 0u,
};
