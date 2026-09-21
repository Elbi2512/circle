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
#define BBC_INTERNAL_H 512

#define BBC_MODE7_W 240
#define BBC_MODE7_H 250

extern void bbc_debug_log(const char *msg, unsigned val1, unsigned val2);

/* Interne frame-buffers */
static uint8_t s_bbc_screen[BBC_INTERNAL_H][BBC_INTERNAL_W];
static uint8_t s_mode7_screen[BBC_MODE7_H][BBC_MODE7_W];

static uint8_t s_current_scanline[BBC_INTERNAL_W];
static int s_raster_y = 0;
static int s_vis_col = 0;

/* Teletext Set-After state persists across all 40 columns of a scanline;
 * it is re-primed once per scanline (col == 0), not per character. */
static saa5050_line_state_t s_mode7_ls;

static inline uint32_t bbc_bitmap_ram_addr(uint16_t ma, uint8_t ra, uint32_t screen_base)
{
    /*
     * BBC Micro Model B hardware address decoding:
     * De 6845 MA lijnen lopen via de Video ULA adres-shifter.
     * In Mode 0..2 (20KB scherm) start het geheugen bij 0x3000.
     * Het adres wikkelt hardwarematig rond op bit 13/14.
     */
    uint32_t addr;
    if (ma & 0x2000)
    {
        addr = ((ma & 0x1FFF) << 3) | (ra & 7);
    }
    else
    {
        addr = (((ma & 0x1FFF) + 0x600) << 3) | (ra & 7);
    }
    addr = (addr + screen_base - BBC_SCREEN_BASE_MODE012) & 0x7FFF;
    return addr;
}

static inline uint32_t bbc_mode7_ram_addr(uint16_t ma)
{
    return ((ma & 0x0800u) << 3) | 0x3C00u | (ma & 0x03FFu);
}

static void _vsync_cb(void *ctx, bool state)
{
    bbc_video_t *video = (bbc_video_t *)ctx;

    if (state)
    {
        /* VSYNC rising edge: reset raster scanline teller voor nieuw frame */
        s_raster_y = -1; /* -1 betekent: wacht tot het actieve beeld (v_de) begint */
        saa5050_reset_frame(&video->teletext);
    }

    if (video->vsync_cb)
    {
        video->vsync_cb(video->vsync_ctx, state);
    }
}

void bbc_video_init(bbc_video_t *video, const uint8_t *system_ram, uint32_t ram_size)
{
    memset(video, 0, sizeof(*video));

    memset(s_bbc_screen, 0, sizeof(s_bbc_screen));
    memset(s_mode7_screen, 0, sizeof(s_mode7_screen));
    memset(s_current_scanline, 0, sizeof(s_current_scanline));

    s_raster_y = 0;
    s_vis_col = 0;

    video->system_ram = system_ram;
    video->ram_size = ram_size;
    video->screen_base = BBC_SCREEN_BASE_MODE012;

    mc6845_init(&video->crtc, MC6845_TYPE_MC6845);
    mc6845_set_vsync_callback(&video->crtc, _vsync_cb, video);

    bbc_video_ula_init(&video->ula);
    saa5050_init(&video->teletext, NULL);
}

void bbc_video_reset(bbc_video_t *video)
{
    memset(s_bbc_screen, 0, sizeof(s_bbc_screen));
    memset(s_mode7_screen, 0, sizeof(s_mode7_screen));
    memset(s_current_scanline, 0, sizeof(s_current_scanline));
    memset(&s_mode7_ls, 0, sizeof(s_mode7_ls));

    mc6845_reset(&video->crtc);
    bbc_video_ula_reset(&video->ula);
    saa5050_reset(&video->teletext);
    video->frames_rendered = 0;

    s_raster_y = 0;
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

void bbc_video_set_screen_base(bbc_video_t *video, uint32_t base)
{
    video->screen_base = base & 0x7FFFu;
}

/* --------------------------------------------------------------------------
 * Cyclus-exacte tick: draait synchroon mee met de 6502 CPU klok
 * -------------------------------------------------------------------------- */
void bbc_video_tick(bbc_video_t *video)
{
    const mc6845_t *crtc = &video->crtc;
    uint8_t old_h_ctr = crtc->h_ctr;
    uint8_t old_r_ctr = crtc->r_ctr;

    mc6845_output_t out = mc6845_tick(&video->crtc);

    /* Bitmap rasterlijn */
    int raster_y = -1;
    if (!video->ula.teletext_mode)
    {
        int row_height = (int)crtc->max_scanline_addr + 1;
        raster_y = ((int)crtc->v_ctr * row_height) + (int)(out.ra & 0x07);
    }

    /* Mode 7 rasterlijn */
    int raster_y_mode7 = -1;
    if (video->ula.teletext_mode)
    {
        int scan_line = (int)(out.ra & 0x1F) >> 1;
        if (scan_line > 9)
            scan_line = 9;

        raster_y_mode7 = ((int)crtc->v_ctr * 10) + scan_line;
    }

    /* Einde scanline */
    if (old_h_ctr == crtc->h_total)
    {
        /* Nieuwe character-rij begonnen (r_ctr terug naar 0): latch/reset
         * de double-height state (\x0D is Set-After per rij, niet per scanline). */
        if (video->ula.teletext_mode && crtc->r_ctr == 0 && old_r_ctr != 0)
        {
            saa5050_end_row(&video->teletext);
            saa5050_start_row(&video->teletext, (uint8_t)crtc->v_ctr);
        }

        if (video->ula.teletext_mode)
        {
            if (raster_y_mode7 >= 0 && raster_y_mode7 < BBC_MODE7_H)
            {
                memcpy(s_mode7_screen[raster_y_mode7], s_current_scanline, BBC_MODE7_W);
            }
        }
        else
        {
            if (raster_y >= 0 && raster_y < BBC_INTERNAL_H)
            {
                memcpy(s_bbc_screen[raster_y], s_current_scanline, BBC_INTERNAL_W);
            }
        }

        memset(s_current_scanline, 0, BBC_INTERNAL_W);
        s_vis_col = 0;
    }

    if (!video->system_ram || !out.display_enable)
        return;

    int col = s_vis_col++;

    /* MODE 7 ------------------------------------------------------ */
    if (video->ula.teletext_mode)
    {
        uint32_t ram_addr = bbc_mode7_ram_addr(out.ma);

        int scan_line = (int)(out.ra & 0x1F) >> 1;
        if (scan_line > 9)
            scan_line = 9;

        if (col == 0)
        {
            /* saa5050_render_char() halves this itself (0-19 -> 0-9 line_addr),
             * so prime with the raw CRTC scanline, not the pre-halved buffer row. */
            saa5050_start_scanline(&video->teletext, &s_mode7_ls, (uint8_t)(out.ra & 0x1F));
        }

        if (ram_addr < video->ram_size && col < 40)
        {
            uint8_t code = video->system_ram[ram_addr] & 0x7F;

            uint8_t pixels[SAA5050_PIXELS_PER_CHAR];
            saa5050_render_char(&video->teletext, &s_mode7_ls, code, pixels);

            const int CHAR_WIDTH = 6;
            int base_x = col * CHAR_WIDTH;

            for (int dst_px = 0; dst_px < CHAR_WIDTH; dst_px++)
            {
                int src_px = (dst_px * SAA5050_PIXELS_PER_CHAR) / CHAR_WIDTH;
                int out_x = base_x + dst_px;

                if (out_x < BBC_MODE7_W)
                    s_current_scanline[out_x] = pixels[src_px] & 7;
            }
        }
        return;
    }

    /* BITMAP MODES ------------------------------------------------ */
    uint32_t ram_addr = bbc_bitmap_ram_addr(out.ma, out.ra, video->screen_base);
    if (ram_addr < video->ram_size)
    {
        uint8_t data_byte = video->system_ram[ram_addr];
        uint8_t colours[8];
        int npx = bbc_video_ula_serialize(&video->ula, data_byte, colours, out.cursor);

        int h_pixels = video->ula.crtc_2mhz ? 4 : 8;
        int base_x = col * h_pixels;

        for (int dst_px = 0; dst_px < h_pixels; dst_px++)
        {
            int src_px = (dst_px * npx) / h_pixels;
            int out_x = base_x + dst_px;

            if (out_x < BBC_INTERNAL_W)
                s_current_scanline[out_x] = colours[src_px] & 7;
        }
    }
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

/* --------------------------------------------------------------------------
 * Render row voor Circle framebuffer (aangeroepen door Core 0)
 * -------------------------------------------------------------------------- */
void bbc_video_render_row(const bbc_video_t *video,
                          int out_y, int out_height,
                          uint8_t *out_pixels, int out_width)
{
    if (!out_pixels || out_width <= 0)
        return;

    if (video->ula.teletext_mode)
    {
        const int SRC_W = BBC_MODE7_W;
        const int SRC_H = BBC_MODE7_H;

        int src_y = (out_y * SRC_H) / out_height;
        if (src_y >= SRC_H)
            src_y = SRC_H - 1;

        for (int x = 0; x < out_width; x++)
        {
            int src_x = (x * SRC_W) / out_width;
            if (src_x >= SRC_W)
                src_x = SRC_W - 1;

            out_pixels[x] = s_mode7_screen[src_y][src_x];
        }
        return;
    }

    int src_y = (out_y * BBC_INTERNAL_H) / out_height;
    if (src_y >= BBC_INTERNAL_H)
        src_y = BBC_INTERNAL_H - 1;

    int copy_w = (out_width < BBC_INTERNAL_W) ? out_width : BBC_INTERNAL_W;
    memcpy(out_pixels, s_bbc_screen[src_y], copy_w);
}
