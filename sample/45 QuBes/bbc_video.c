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

#define BBC_MODE7_W 240
#define BBC_MODE7_H 250

extern void bbc_debug_log(const char *msg, unsigned val1, unsigned val2);

/* Interne frame-buffers */
static uint8_t s_bbc_screen[2][BBC_INTERNAL_H][BBC_INTERNAL_W];
static uint8_t s_mode7_screen[2][BBC_MODE7_H][BBC_MODE7_W];
static volatile uint8_t s_display_buffer = 0;
static uint8_t s_render_buffer = 1;
static uint8_t s_frame_read_buffer = 0;

static uint8_t s_current_scanline[BBC_INTERNAL_W];
static int s_raster_y = 0;
static int s_vis_col = 0;
static saa5050_line_state_t s_mode7_ls;

/*
static inline uint32_t bbc_bitmap_ram_addr(uint16_t ma, uint8_t ra, uint32_t screen_base)
{
    uint32_t screen_length = 0x2000;
    switch (screen_base & 0x7FFFu)
    {
    case BBC_SCREEN_BASE_MODE012:
        screen_length = 0x5000; // 20 KB 
        break;
    case BBC_SCREEN_BASE_MODE3:
        screen_length = 0x4000; // 16 KB 
        break;
    case BBC_SCREEN_BASE_MODE45:
        screen_length = 0x2800; // 10 KB 
        break;
    case BBC_SCREEN_BASE_MODE6:
        screen_length = 0x2000; // 8 KB 
        break;
    default:
        break;
    }

    uint32_t addr = ((((uint32_t)ma << 3) | (ra & 0x07)) & 0xFFFFu);
    if (addr & 0x8000u)
        addr -= screen_length;
    return addr & 0x7FFFu;
}
*/

static inline uint32_t bbc_bitmap_ram_addr(uint16_t ma, uint8_t ra, uint32_t screen_base)
{
    /* BeebEm exacte berekening:
     * De MOS stelt R12/R13 in op 0x0600 voor Mode 0.
     * (0x0600 << 3) levert direct 0x3000 op. */
    uint32_t caddr = (((uint32_t)ma << 3) | (ra & 0x07)) & 0xFFFFu;

    uint32_t screen_length = 0x5000;
    switch (screen_base & 0x7FFFu)
    {
    case BBC_SCREEN_BASE_MODE012: screen_length = 0x5000; break; /* 20 KB */
    case BBC_SCREEN_BASE_MODE3:   screen_length = 0x4000; break; /* 16 KB */
    case BBC_SCREEN_BASE_MODE45:  screen_length = 0x2800; break; /* 10 KB */
    case BBC_SCREEN_BASE_MODE6:   screen_length = 0x2000; break; /* 8 KB */
    default: break;
    }

    /* Wrap-around: hardware bit 15 */
    if (caddr & 0x8000u)
    {
        caddr -= screen_length;
    }

    return caddr & 0x7FFFu;
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
        asm volatile("dmb sy" ::: "memory");
        s_display_buffer = s_render_buffer;
        s_render_buffer ^= 1;

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
    memset(&s_mode7_ls, 0, sizeof(s_mode7_ls));

    s_raster_y = 0;
    s_vis_col = 0;
    s_display_buffer = 0;
    s_render_buffer = 1;

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

    mc6845_reset(&video->crtc);
    bbc_video_ula_reset(&video->ula);
    saa5050_reset(&video->teletext);
    video->frames_rendered = 0;

    s_raster_y = 0;
    s_vis_col = 0;
    s_frame_read_buffer = s_display_buffer;
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
/* --------------------------------------------------------------------------
 * Cyclus-exacte tick: draait synchroon mee met de 6502 CPU klok
 * -------------------------------------------------------------------------- 
void bbc_video_tick(bbc_video_t *video)
{
    const mc6845_t *crtc = &video->crtc;
    uint8_t old_h_ctr = crtc->h_ctr;
    uint8_t old_r_ctr = crtc->r_ctr;
    uint8_t old_v_ctr = crtc->v_ctr;

    mc6845_output_t out = mc6845_tick(&video->crtc);

    // 1. Bereken de rasterlijn van de scanline die we NU vullen 
    int row_height = (int)crtc->max_scanline_addr + 1;
    int prev_raster_y = ((int)old_v_ctr * row_height) + (int)(old_r_ctr & 0x07);
    int prev_raster_mode7 = ((int)old_v_ctr * 10) + ((int)(old_r_ctr & 0x1F) >> 1);

    // 2. Einde scanline: flush naar het renderframebuffer 
    if (old_h_ctr == crtc->h_total)
    {
        if (video->ula.teletext_mode && crtc->r_ctr == 0 && old_r_ctr != 0)
        {
            saa5050_end_row(&video->teletext);
            saa5050_start_row(&video->teletext, (uint8_t)crtc->v_ctr);
        }

        if (video->ula.teletext_mode)
        {
            if (prev_raster_mode7 >= 0 && prev_raster_mode7 < BBC_MODE7_H)
            {
                memcpy(s_mode7_screen[s_render_buffer][prev_raster_mode7], s_current_scanline, BBC_MODE7_W);
            }
        }
        else
        {
            if (prev_raster_y >= 0 && prev_raster_y < BBC_INTERNAL_H)
            {
                memcpy(s_bbc_screen[s_render_buffer][prev_raster_y], s_current_scanline, BBC_INTERNAL_W);
            }
        }

        memset(s_current_scanline, 0, BBC_INTERNAL_W);
        s_vis_col = 0;
    }

    if (!video->system_ram)
        return;

    if (!video->ula.teletext_mode && s_bitmap_trace_count < 128)
    {
        uint32_t trace_addr = bbc_bitmap_ram_addr(out.ma, out.ra, video->screen_base);
        unsigned trace_byte = trace_addr < video->ram_size ? video->system_ram[trace_addr] : 0xFF;
        bbc_debug_log("VIDEO MA/RA/H/DE", ((unsigned)out.ma << 16) |
                                            ((unsigned)out.ra << 8) |
                                            (unsigned)(cur_h & 0xFF),
                      ((unsigned)(out.display_enable ? 1 : 0) << 31) |
                      ((trace_addr & 0x7FFFu) << 8) | trace_byte);
        s_bitmap_trace_count++;
    }

    if (!out.display_enable)
        return;

    int col = s_vis_col++;

    // MODE 7 ------------------------------------------------------ 
    if (video->ula.teletext_mode)
    {
        uint32_t ram_addr = bbc_mode7_ram_addr(out.ma);
        uint32_t cursor_addr = bbc_mode7_ram_addr(mc6845_get_cursor_addr(&video->crtc));

        // Cursor evaluatie conform 6845 R10 & R11 
        uint8_t cursor_mode = (video->crtc.cursor_start >> 5) & 0x03;
        bool cursor_visible = (cursor_mode == 0) || (cursor_mode >= 2 && video->crtc.cursor_blink_state);
        bool cursor_in_range = (video->crtc.r_ctr >= (video->crtc.cursor_start & 0x1F)) &&
                               (video->crtc.r_ctr <= (video->crtc.cursor_end & 0x1F));

        bool cursor_active = (ram_addr == cursor_addr) && cursor_visible && cursor_in_range;

        if (ram_addr < video->ram_size && col < 40)
        {
            uint8_t code = video->system_ram[ram_addr] & 0x7F;
            if (col == 0)
                saa5050_start_scanline(&video->teletext, &s_mode7_ls, (uint8_t)(out.ra & 0x1F));

            uint8_t pixels[SAA5050_PIXELS_PER_CHAR];
            saa5050_render_char(&video->teletext, &s_mode7_ls, code, pixels);

            const int CHAR_WIDTH = 6;
            int base_x = col * CHAR_WIDTH;

            for (int dst_px = 0; dst_px < CHAR_WIDTH; dst_px++)
            {
                int src_px = (dst_px * SAA5050_PIXELS_PER_CHAR) / CHAR_WIDTH;
                int out_x = base_x + dst_px;

                if (out_x < BBC_MODE7_W)
                {
                    uint8_t pixel = pixels[src_px] & 7;
                    s_current_scanline[out_x] = cursor_active ? (pixel ^ 7) : pixel;
                }
            }
        }
        return;
    }

    // BITMAP MODES ------------------------------------------------
    uint32_t ram_addr = bbc_bitmap_ram_addr(out.ma, out.ra, video->screen_base);
    if (s_bitmap_trace_count < 128)
    {
        unsigned byte_value = (ram_addr < video->ram_size) ? video->system_ram[ram_addr] : 0xFF;
        bbc_debug_log("VIDEO MA/RA/H/C", ((unsigned)out.ma << 16) |
                                           ((unsigned)out.ra << 8) |
                                           (unsigned)(cur_h & 0xFF),
                      ((unsigned)ram_addr << 8) | byte_value);
        s_bitmap_trace_count++;
    }
    if (ram_addr < video->ram_size)
    {
        uint8_t data_byte = video->system_ram[ram_addr];
        if (s_bitmap_trace_count < 128)
        {
            bbc_debug_log("VIDEO MA/RA/H/C", ((unsigned)out.ma << 16) |
                                               ((unsigned)out.ra << 8) |
                                               (unsigned)(cur_h & 0xFF),
                          ((unsigned)ram_addr << 8) | data_byte);
            s_bitmap_trace_count++;
        }
        uint8_t colours[8];
        int npx = bbc_video_ula_serialize(&video->ula, data_byte, colours, out.cursor);

        bool is_80_col = (video->ula.control & 0x10) != 0;
                if (!video->system_ram)
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
*/

void bbc_video_tick(bbc_video_t *video)
{
    const mc6845_t *crtc = &video->crtc;

    /* Bewaar de actieve CRTC tellers VÓÓR de ophoging in mc6845_tick */
    uint8_t cur_h = crtc->h_ctr;
    uint8_t cur_r = crtc->r_ctr;
    uint8_t cur_v = crtc->v_ctr;

    mc6845_output_t out = mc6845_tick(&video->crtc);

    /* 1. Flush de voltooide scanline zodra we aan het einde van de regel zijn */
    if (cur_h == crtc->h_total)
    {
        int row_height = (int)crtc->max_scanline_addr + 1;
        int target_y = ((int)cur_v * row_height) + (int)cur_r;
        int target_mode7 = ((int)cur_v * 10) + ((int)(cur_r & 0x1F) >> 1);

        if (video->ula.teletext_mode)
        {
            if (crtc->r_ctr == 0 && cur_r != 0)
            {
                saa5050_end_row(&video->teletext);
                saa5050_start_row(&video->teletext, (uint8_t)crtc->v_ctr);
            }

            if (target_mode7 >= 0 && target_mode7 < BBC_MODE7_H)
            {
                memcpy(s_mode7_screen[s_render_buffer][target_mode7], s_current_scanline, BBC_MODE7_W);
            }
        }
        else
        {
            if (target_y >= 0 && target_y < BBC_INTERNAL_H)
            {
                memcpy(s_bbc_screen[s_render_buffer][target_y], s_current_scanline, BBC_INTERNAL_W);
            }
        }

        memset(s_current_scanline, 0, BBC_INTERNAL_W);
        s_vis_col = 0;
    }

    if (!video->system_ram)
        return;

    if (!out.display_enable)
        return;

    /* mc6845_tick() emits MA/RA for the current clock, then advances the
     * counters. Keep the horizontal column paired with that same output. */
    int col = cur_h;

    /* MODE 7 ------------------------------------------------------ */
    if (video->ula.teletext_mode)
    {
        uint32_t ram_addr = bbc_mode7_ram_addr(out.ma);
        uint32_t cursor_addr = bbc_mode7_ram_addr(mc6845_get_cursor_addr(&video->crtc));

        uint8_t cursor_mode = (video->crtc.cursor_start >> 5) & 0x03;
        bool cursor_visible = (cursor_mode == 0) || (cursor_mode >= 2 && video->crtc.cursor_blink_state);
        bool cursor_in_range = (cur_r >= (video->crtc.cursor_start & 0x1F)) &&
                               (cur_r <= (video->crtc.cursor_end & 0x1F));
        bool cursor_active = (ram_addr == cursor_addr) && cursor_visible && cursor_in_range;

        if (ram_addr < video->ram_size && col < 40)
        {
            uint8_t code = video->system_ram[ram_addr] & 0x7F;
            if (col == 0)
                saa5050_start_scanline(&video->teletext, &s_mode7_ls, (uint8_t)(out.ra & 0x1F));

            uint8_t pixels[SAA5050_PIXELS_PER_CHAR];
            saa5050_render_char(&video->teletext, &s_mode7_ls, code, pixels);

            const int CHAR_WIDTH = 6;
            int base_x = col * CHAR_WIDTH;

            for (int dst_px = 0; dst_px < CHAR_WIDTH; dst_px++)
            {
                int src_px = (dst_px * SAA5050_PIXELS_PER_CHAR) / CHAR_WIDTH;
                int out_x = base_x + dst_px;

                if (out_x < BBC_MODE7_W)
                {
                    uint8_t pixel = pixels[src_px] & 7;
                    s_current_scanline[out_x] = cursor_active ? (pixel ^ 7) : pixel;
                }
            }
        }
        return;
    }

    /* BITMAP MODES (MODE 0, 1, 2, 3, 4, 5, 6) -------------------- */

    /* MODE 3/6 use extra scanlines (R9 > 7) as blank spacing between text
     * rows; the address math wraps ra&7, so without this the spacer
     * scanlines would re-read (and show a ghost of) the row's own top
     * scanlines instead of being blank. */
    if (cur_r & 8)
    {
        bool is_80_col_gap = (video->ula.control & 0x10) != 0;
        int h_pixels_gap = is_80_col_gap ? 8 : 16;
        int base_x_gap = col * h_pixels_gap;
        for (int dst_px = 0; dst_px < h_pixels_gap; dst_px++)
        {
            int out_x = base_x_gap + dst_px;
            if (out_x < BBC_INTERNAL_W)
                s_current_scanline[out_x] = 0;
        }
        return;
    }

    uint32_t ram_addr = bbc_bitmap_ram_addr(out.ma, out.ra, video->screen_base);

    if (ram_addr < video->ram_size)
    {
        uint8_t data_byte = video->system_ram[ram_addr];
        uint8_t colours[8];
        int npx = bbc_video_ula_serialize(&video->ula, data_byte, colours, out.cursor);

        /* In Mode 0 en Mode 3 is ULA bit 4 hoog (2 MHz CRTC = 80 koloms = 8 pixels per byte) */
        bool is_80_col = (video->ula.control & 0x10) != 0;
        int h_pixels = is_80_col ? 8 : 16;
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

void bbc_video_begin_render(const bbc_video_t *video)
{
    (void)video;
    s_frame_read_buffer = s_display_buffer;
    asm volatile("dmb sy" ::: "memory");
}

/* --------------------------------------------------------------------------
 * Render row voor Circle framebuffer (aangeroepen door Core 0)
 * -------------------------------------------------------------------------- 
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

            out_pixels[x] = s_mode7_screen[s_display_buffer][src_y][src_x];
        }
        return;
    }

    int src_y = (out_y * BBC_INTERNAL_H) / out_height;
    if (src_y >= BBC_INTERNAL_H)
        src_y = BBC_INTERNAL_H - 1;

    int copy_w = (out_width < BBC_INTERNAL_W) ? out_width : BBC_INTERNAL_W;
    memcpy(out_pixels, s_bbc_screen[s_display_buffer][src_y], copy_w);
}
    */
   
void bbc_video_render_row(const bbc_video_t *video,
                          int out_y, int out_height,
                          uint8_t *out_pixels, int out_width)
{
    if (!out_pixels || out_width <= 0 || out_height <= 0)
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

            out_pixels[x] = s_mode7_screen[s_frame_read_buffer][src_y][src_x] & 7;
        }
        return;
    }

    /* BITMAP MODES (MODE 0 t/m 6): levert 8-bit indexen af in m_RowBuf */
    int src_y = (out_y * BBC_INTERNAL_H) / out_height;
    if (src_y >= BBC_INTERNAL_H)
        src_y = BBC_INTERNAL_H - 1;

    int copy_w = (out_width < BBC_INTERNAL_W) ? out_width : BBC_INTERNAL_W;
    memcpy(out_pixels, s_bbc_screen[s_frame_read_buffer][src_y], copy_w);
}