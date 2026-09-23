/*
 * mc6845.c — MC6845 CRTC emulation for BBC Micro
 *
 * Licence: zlib
 * Copyright (c) 2018 Andre Weissflog
 * Adaptation (c) 2026 esp-beep project / Circle Bare-metal port
 */

#include <string.h>
#include "mc6845.h"

static const uint8_t s_mask[18] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x1F, 0x7F, 0x7F,
    0xF3, 0x1F, 0x7F, 0x1F, 0x3F, 0xFF, 0x3F, 0xFF,
    0x3F, 0xFF};

static const uint8_t s_rw[3][18] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 2, 2},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2, 2},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2, 2},
};

static inline uint8_t _hsync_width(const mc6845_t *c)
{
    uint8_t w = c->sync_widths & 0x0F;
    if (c->type == MC6845_TYPE_MC6845 && w == 0)
        w = 16;
    return w;
}

static inline uint8_t _vsync_width(const mc6845_t *c)
{
    uint8_t w;
    if (c->type == MC6845_TYPE_UM6845)
    {
        w = (c->sync_widths >> 4) & 0x0F;
        if (w == 0)
            w = 16;
    }
    else
    {
        w = 16;
    }
    return w;
}

void mc6845_init(mc6845_t *c, mc6845_type_t type)
{
    memset(c, 0, sizeof(*c));
    c->type = type;
    c->reg[0x1F] = 0xFF;
    mc6845_reset(c);
}

void mc6845_reset(mc6845_t *c)
{
    c->h_ctr = 0;
    c->hsync_ctr = 0;
    c->v_ctr = 0;
    c->r_ctr = 0;
    c->vsync_ctr = 0;
    c->vadj_ctr = 0;
    c->ma = 0;
    c->ma_row_start = 0;
    c->ma_store = 0;
    c->hs = false;
    c->vs = false;
    c->h_de = false;
    c->v_de = false;
    c->in_vadj = false;
    c->odd_field = false;
    c->cursor_on = false;
    c->cursor_line_ff = false;
    c->cursor_blink_ctr = 0;
    c->cursor_blink_state = true;
    c->frame_count = 0;
    c->lightpen_latched = false;
}

void mc6845_write(mc6845_t *c, uint8_t addr, uint8_t data)
{
    if (!(addr & 1))
    {
        c->sel = data & 0x1F;
    }
    else
    {
        int i = c->sel & 0x1F;
        if (i < 18 && (s_rw[c->type][i] & 1))
        {
            c->reg[i] = data & s_mask[i];
        }
    }
}

uint8_t mc6845_read(mc6845_t *c, uint8_t addr)
{
    if (!(addr & 1))
    {
        if (c->type != MC6845_TYPE_MC6845)
        {
            return c->v_de ? 0 : (1 << 5);
        }
        return 0;
    }
    else
    {
        int i = c->sel & 0x1F;
        if (i < 18 && (s_rw[c->type][i] & 2))
        {
            return c->reg[i] & s_mask[i];
        }
        return 0;
    }
}

mc6845_output_t mc6845_tick(mc6845_t *c)
{
    /* 1. Stel de output samen voor de HUIDIGE klokcyclus */
    mc6845_output_t out;
    out.ma = c->ma & 0x3FFF;
    out.ra = c->r_ctr & 0x1F;
    out.display_enable = c->h_de && c->v_de;
    out.hsync = c->hs;
    out.vsync = c->vs;

    /* Cursor evaluatie op huidige positie */
    c->cursor_on = false;
    uint16_t cursor_addr = mc6845_get_cursor_addr(c);
    if (out.display_enable && c->ma == cursor_addr)
    {
        if (c->r_ctr == 0)
            c->cursor_line_ff = false;
        if (c->r_ctr == (c->cursor_start & 0x1F))
            c->cursor_line_ff = true;

        uint8_t cursor_mode = (c->cursor_start >> 5) & 0x03;
        bool cursor_visible = (cursor_mode == 0) ||
                              (cursor_mode >= 2 && c->cursor_blink_state);
        c->cursor_on = c->cursor_line_ff && cursor_visible;

        if (c->r_ctr == (c->cursor_end & 0x1F))
            c->cursor_line_ff = false;
    }
    out.cursor = c->cursor_on;

    /* 2. Werk de tellers bij voor de VOLGENDE cyclus (klok-overgang) */
    if (c->h_ctr == c->h_total)
    {
        c->h_ctr = 0;

        uint8_t max_scan = (!c->in_vadj) ? c->max_scanline_addr : ((c->v_total_adjust > 0) ? (c->v_total_adjust - 1) : 0);
        bool need_adj = (c->v_total_adjust != 0) || c->odd_field;
        bool frame_end = (c->r_ctr == max_scan) &&
                         (c->in_vadj || (!need_adj && c->v_ctr == c->v_total));

        if (frame_end)
        {
            c->r_ctr = 0;
            c->v_ctr = 0;
            c->in_vadj = false;
            c->ma_row_start = mc6845_get_start_addr(c);
            c->frame_count++;

            c->cursor_blink_ctr++;
            uint8_t blink_mode = (c->cursor_start >> 5) & 0x03;
            uint8_t blink_rate = (blink_mode == 3) ? 16 : 8;
            if (c->cursor_blink_ctr >= blink_rate)
            {
                c->cursor_blink_ctr = 0;
                c->cursor_blink_state = !c->cursor_blink_state;
            }
        }
        else if (!c->in_vadj && c->r_ctr == max_scan)
        {
            c->r_ctr = 0;
            c->ma_row_start = (uint16_t)((c->ma_row_start + c->h_displayed) & 0x3FFF);
            c->v_ctr++;

            if (c->v_ctr == c->v_total && need_adj)
            {
                c->in_vadj = true;
            }
        }
        else
        {
            c->r_ctr++;
        }

        c->ma = c->ma_row_start;
    }
    else
    {
        c->h_ctr++;
        c->ma = (c->ma + 1) & 0x3FFF;
    }

    /* Update display enable voor de volgende tick */
    c->h_de = (c->h_ctr < c->h_displayed);
    c->v_de = (c->v_ctr < c->v_displayed);

    /* HSYNC evaluatie */
    if (c->hs)
    {
        if (c->hsync_ctr >= _hsync_width(c))
        {
            c->hs = false;
            c->hsync_ctr = 0;
        }
        else
        {
            c->hsync_ctr++;
        }
    }
    else if (c->h_ctr == c->h_sync_pos)
    {
        c->hs = true;
        c->hsync_ctr = 1;
    }

    /* VSYNC evaluatie */
    if (c->h_ctr == c->h_sync_pos)
    {
        if (!c->vs && c->v_ctr == c->v_sync_pos && c->r_ctr == 0)
        {
            c->vs = true;
            c->vsync_ctr = 0;
            if (c->vsync_cb)
            {
                c->vsync_cb(c->vsync_ctx, true);
            }
        }
        else if (c->vs)
        {
            c->vsync_ctr++;
            if (c->vsync_ctr == _vsync_width(c))
            {
                c->vs = false;
                if (c->vsync_cb)
                {
                    c->vsync_cb(c->vsync_ctx, false);
                }
            }
        }
    }

    return out;
}

void mc6845_light_pen_strobe(mc6845_t *c)
{
    c->lightpen_addr = c->ma;
    c->lightpen_latched = true;
    c->reg[MC6845_R16_LIGHTPENHI] = (c->ma >> 8) & 0x3F;
    c->reg[MC6845_R17_LIGHTPENLO] = c->ma & 0xFF;
}

void mc6845_set_vsync_callback(mc6845_t *c, void (*cb)(void *ctx, bool state), void *ctx)
{
    c->vsync_cb = cb;
    c->vsync_ctx = ctx;
}

void mc6845_toggle_cursor_blink(mc6845_t *c)
{
    uint8_t cursor_mode = (c->cursor_start >> 5) & 0x03;
    if (cursor_mode >= 2)
        c->cursor_blink_state = !c->cursor_blink_state;
}