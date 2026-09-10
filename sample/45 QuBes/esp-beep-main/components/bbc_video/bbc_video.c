/*
 * bbc_video.c — BBC Micro video subsystem integration
 *
 * Integrates MC6845 CRTC + Video ULA + SAA5050 -> framebuffer.
 *
 * Licence: zlib
 * Copyright (c) 2026 esp-beep project / Circle Bare-metal port
 */

#include <string.h>
#include <stdio.h>
#include "bbc_video.h"

#define BBC_INTERNAL_W 640
#define BBC_INTERNAL_H 256

/* Interne frame-buffer die tijdens de scanlines wordt gevuld */
static uint8_t s_bbc_screen[BBC_INTERNAL_H][BBC_INTERNAL_W];
static uint8_t s_current_scanline[BBC_INTERNAL_W];
static int s_vis_col = 0;

static inline uint32_t bbc_bitmap_ram_addr(uint16_t ma, uint8_t ra)
{
    return (((uint32_t)ma << 3) | ((uint32_t)(ra & 0x07))) & 0x7FFF;
}

static void _vsync_cb(void *ctx, bool state)
{
    bbc_video_t *video = (bbc_video_t *)ctx;

    if (video->vsync_cb)
    {
        video->vsync_cb(video->vsync_ctx, state);
    }
}

void bbc_video_init(bbc_video_t *video, const uint8_t *system_ram, uint32_t ram_size)
{
    memset(video, 0, sizeof(*video));
    video->system_ram = system_ram;
    video->ram_size = ram_size;

    memset(s_bbc_screen, 0, sizeof(s_bbc_screen));
    memset(s_current_scanline, 0, sizeof(s_current_scanline));
    s_vis_col = 0;

    mc6845_init(&video->crtc, MC6845_TYPE_MC6845);
    mc6845_set_vsync_callback(&video->crtc, _vsync_cb, video);

    bbc_video_ula_init(&video->ula);
    saa5050_init(&video->teletext, NULL);
}

void bbc_video_reset(bbc_video_t *video)
{
    mc6845_reset(&video->crtc);
    bbc_video_ula_reset(&video->ula);
    saa5050_reset(&video->teletext);
    video->frames_rendered = 0;
    s_vis_col = 0;
}

void bbc_video_set_output(bbc_video_t *video, const bbc_video_output_t *output)
{
    video->output = *output;
}

void bbc_video_set_vsync_callback(bbc_video_t *video, void (*cb)(void *ctx, bool state), void *ctx)
{
    video->vsync_cb = cb;
    video->vsync_ctx = ctx;
}

void bbc_video_set_frame_callback(bbc_video_t *video, void (*cb)(void *ctx), void *ctx)
{
    video->frame_cb = cb;
    video->frame_ctx = ctx;
}

void bbc_video_crtc_write(bbc_video_t *video, uint8_t addr, uint8_t data)
{
    mc6845_write(&video->crtc, addr, data);
}

uint8_t bbc_video_crtc_read(bbc_video_t *video, uint8_t addr)
{
    return mc6845_read(&video->crtc, addr);
}

void bbc_video_vidproc_write(bbc_video_t *video, uint8_t addr, uint8_t data)
{
    bbc_video_ula_write(&video->ula, addr, data);
}

/* --------------------------------------------------------------------------
 * Cyclus-exacte tick: draait synchroon mee met de 6502 CPU klok
 * -------------------------------------------------------------------------- */
void bbc_video_tick(bbc_video_t *video)
{
    const mc6845_t *crtc = &video->crtc;
    uint8_t old_h_ctr = crtc->h_ctr;

    /* Tick de 6845 CRTC */
    mc6845_output_t out = mc6845_tick(&video->crtc);

    /* Einde van een scanline (horizontale teller bereikt h_total) */
    if (old_h_ctr == crtc->h_total)
    {
        /* Bereken absolute rasterlijn direct vanuit CRTC v_ctr en r_ctr om drift te voorkomen */
        int max_scan = (crtc->max_scanline_addr > 0) ? crtc->max_scanline_addr : 7;
        int target_y = (crtc->v_ctr * (max_scan + 1)) + crtc->r_ctr;

        if (target_y >= 0 && target_y < BBC_INTERNAL_H)
        {
            memcpy(s_bbc_screen[target_y], s_current_scanline, BBC_INTERNAL_W);
        }
        memset(s_current_scanline, 0, BBC_INTERNAL_W);
        s_vis_col = 0; /* Reset zichtbare kolomteller voor de nieuwe regel */
    }

    if (!video->system_ram)
        return;

    /* Controleer of we binnen het zichtbare beeld zitten[cite: 5] */
    if (!out.display_enable)
        return;

    int max_cols = crtc->h_displayed > 0 ? crtc->h_displayed : 80;
    int col = s_vis_col++;

    if (col >= max_cols)
    {
        return; /* Voorkom kolom-wrapping buiten de ingestelde displaybreedte */
    }

    if (video->ula.teletext_mode)
    {
        /* Mode 7 (Teletext)[cite: 5] */
        int char_row = (int)crtc->v_ctr;
        int scan_line = (int)crtc->r_ctr;
        uint32_t ram_addr = (BBC_SCREEN_BASE_MODE7 + (uint32_t)(char_row * SAA5050_COLS + col)) & 0x7FFF;
        if (ram_addr < video->ram_size && col < SAA5050_COLS)
        {
            uint8_t code = video->system_ram[ram_addr] & 0x7F;
            saa5050_line_state_t ls;
            saa5050_start_scanline(&video->teletext, &ls, (uint8_t)(scan_line * 2));
            uint8_t pixels[SAA5050_PIXELS_PER_CHAR];
            saa5050_render_char(&video->teletext, &ls, code, pixels);

            int base_x = col * 16;
            for (int i = 0; i < SAA5050_PIXELS_PER_CHAR; i++)
            {
                int fx = base_x + (i * 16 / SAA5050_PIXELS_PER_CHAR);
                if (fx < BBC_INTERNAL_W)
                {
                    s_current_scanline[fx] = pixels[i] & 7;
                }
            }
        }
    }
    else
    {
        /* Bitmap Modes 0 t/m 6[cite: 5] */
        uint32_t ram_addr = bbc_bitmap_ram_addr(out.ma, out.ra);
        if (ram_addr < video->ram_size)
        {
            uint8_t data_byte = video->system_ram[ram_addr];
            uint8_t colours[8];
            int npx = bbc_video_ula_serialize(&video->ula, data_byte, colours, out.cursor);

            int hsync = video->ula.crtc_2mhz ? 8 : 16;
            int px_width = hsync / npx;
            int base_x = col * hsync;

            for (int px = 0; px < npx; px++)
            {
                uint8_t col_idx = colours[px] & 7;
                int fx = base_x + px * px_width;
                for (int d = 0; d < px_width; d++)
                {
                    int out_x = fx + d;
                    if (out_x >= 0 && out_x < BBC_INTERNAL_W)
                    {
                        s_current_scanline[out_x] = col_idx;
                    }
                }
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Render row voor Circle framebuffer (aangeroepen door Core 0)[cite: 5]
 * -------------------------------------------------------------------------- */
void bbc_video_render_row(const bbc_video_t *video,
                          int out_y, int out_height,
                          uint8_t *out_pixels, int out_width)
{
    (void)video;
    if (!out_pixels || out_width <= 0)
        return;
    if (out_y < 0 || out_y >= out_height)
        return;

    int src_y = (out_y * BBC_INTERNAL_H) / out_height;
    if (src_y >= BBC_INTERNAL_H)
        src_y = BBC_INTERNAL_H - 1;

    memcpy(out_pixels, s_bbc_screen[src_y], (out_width < BBC_INTERNAL_W) ? out_width : BBC_INTERNAL_W);
}

void bbc_video_toggle_flash(bbc_video_t *video)
{
    bbc_video_ula_toggle_flash(&video->ula);
    saa5050_toggle_flash(&video->teletext);
}

void bbc_video_render_frame(bbc_video_t *video)
{
    video->frames_rendered++;
    if (video->frame_cb)
    {
        video->frame_cb(video->frame_ctx);
    }
}