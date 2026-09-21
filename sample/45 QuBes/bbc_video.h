/*
 * bbc_video.h — BBC Micro video subsystem integration
 *
 * Integrates: MC6845 CRTC + Video ULA + SAA5050 Teletext → framebuffer.
 *
 * Licence: zlib
 * Copyright (c) 2026 esp-beep project / Circle port
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "mc6845.h"
#include "bbc_video_ula.h"
#include "saa5050.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * Framebuffer dimensions (native BBC Micro visible area)
 * -------------------------------------------------------------------------- */
#define BBC_FB_WIDTH 640
#define BBC_FB_HEIGHT 256

    /* --------------------------------------------------------------------------
     * Framebuffer pixel formats
     * -------------------------------------------------------------------------- */
    typedef enum
    {
        BBC_FB_FORMAT_INDEX8, /* 1 byte/pixel: physical colour index 0-7       */
        BBC_FB_FORMAT_RGB332, /* 1 byte/pixel: RRRGGGBB                        */
        BBC_FB_FORMAT_RGB565, /* 2 bytes/pixel: little-endian RGB565           */
        BBC_FB_FORMAT_RGB888, /* 3 bytes/pixel: R,G,B                          */
    } bbc_fb_format_t;

    /* --------------------------------------------------------------------------
     * Output configuration
     * -------------------------------------------------------------------------- */
    typedef struct
    {
        bbc_fb_format_t format;
        uint16_t width;     /* framebuffer width in pixels             */
        uint16_t height;    /* framebuffer height in pixels            */
        void *framebuffer;  /* caller-allocated buffer                 */
        uint32_t fb_stride; /* bytes per row                           */
    } bbc_video_output_t;

/* --------------------------------------------------------------------------
 * BBC Micro screen base addresses (Model B 32 KB)
 * -------------------------------------------------------------------------- */
#define BBC_SCREEN_BASE_MODE012 0x3000u /* 20 KB: MODEs 0,1,2              */
#define BBC_SCREEN_BASE_MODE3 0x4000u   /* 16 KB: MODE 3                   */
#define BBC_SCREEN_BASE_MODE45 0x5800u  /* 10 KB: MODEs 4,5                */
#define BBC_SCREEN_BASE_MODE6 0x6000u   /*  8 KB: MODE 6                   */
#define BBC_SCREEN_BASE_MODE7 0x7C00u   /*  1 KB: MODE 7 Teletext          */

    /* --------------------------------------------------------------------------
     * Full video subsystem state — no heap allocation
     * -------------------------------------------------------------------------- */
    typedef struct
    {
        mc6845_t crtc;       /* MC6845 CRTC                             */
        bbc_video_ula_t ula; /* Video ULA                               */
        saa5050_t teletext;  /* SAA5050                                 */

        /* System RAM pointer (owned by bbc_memory, not by this struct) */
        const uint8_t *system_ram;
        uint32_t ram_size;
        uint32_t screen_base;

        /* Output configuration */
        bbc_video_output_t output;

        /* Callbacks */
        void (*vsync_cb)(void *ctx, bool state);
        void *vsync_ctx;
        void (*frame_cb)(void *ctx);
        void *frame_ctx;

        /* Statistics */
        uint32_t frames_rendered;
    } bbc_video_t;

    /* --------------------------------------------------------------------------
     * Public API
     * -------------------------------------------------------------------------- */

    void bbc_video_init(bbc_video_t *video,
                        const uint8_t *system_ram, uint32_t ram_size);
    void bbc_video_reset(bbc_video_t *video);

    /* Configure framebuffer output */
    void bbc_video_set_output(bbc_video_t *video,
                              const bbc_video_output_t *output);

    /* Connect VSYNC callback */
    void bbc_video_set_vsync_callback(bbc_video_t *video,
                                      void (*cb)(void *ctx, bool state),
                                      void *ctx);

    /* Called after each complete frame is rendered into the framebuffer */
    void bbc_video_set_frame_callback(bbc_video_t *video,
                                      void (*cb)(void *ctx),
                                      void *ctx);

    /* CPU bus access — CRTC (&FE00-&FE01) */
    void bbc_video_crtc_write(bbc_video_t *video, uint8_t addr, uint8_t data);
    uint8_t bbc_video_crtc_read(bbc_video_t *video, uint8_t addr);

    /* CPU bus access — Video ULA (&FE20-&FE21) */
    void bbc_video_vidproc_write(bbc_video_t *video, uint8_t addr, uint8_t data);

    void bbc_video_set_screen_base(bbc_video_t *video, uint32_t base);

    /* Tick — advance one CRTC clock cycle. */
    void bbc_video_tick(bbc_video_t *video);

    /* Render a single output row on demand */
    void bbc_video_render_row(const bbc_video_t *video,
                              int out_y, int out_height,
                              uint8_t *out_pixels, int out_width);

    /* Render a complete frame in one call */
    void bbc_video_render_frame(bbc_video_t *video);

    /* Flash toggle — call at ~1 Hz */
    void bbc_video_toggle_flash(bbc_video_t *video);

#ifdef __cplusplus
}
#endif