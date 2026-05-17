#ifndef BOOT_VIDEO_H
#define BOOT_VIDEO_H

#include <stdint.h>

#define BOOT_VIDEO_MAGIC 0x56494430u

typedef struct boot_video_info {
    uint32_t magic;
    uint32_t framebuffer_phys;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} __attribute__((packed)) boot_video_info_t;

#endif
