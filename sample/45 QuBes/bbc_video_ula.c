/*
 * bbc_video_ula.c — Acorn Video ULA emulation for BBC Micro / Circle Bare-metal
 *
 * References: beebwiki.mdfs.net/Video_ULA, B-em video.c (GPL-2, reference only),
 *             BeebFpga vidproc.vhd (reference only).
 *
 * Key implementation notes:
 *   - The palette &FE21 write: bits 7-4 = logical colour, bits 3-0 = physical.
 *     The physical bits are stored XOR'd with 0x07 (BBC hardware quirk).
 *   - Bit interleaving for 2bpp and 4bpp is BBC-specific and different from
 *     a naive MSB-first layout.
 *
 * Licence: zlib
 * Copyright (c) 2026 esp-beep project / Circle port
 */

#include <string.h>
#include "bbc_video_ula.h"

#define ULA_LOGD(fmt, ...) /* no-op */

/* --------------------------------------------------------------------------
 * Default palette — MOS 1.20 default: logical colour N → physical colour N
 * for logical colours 0-7; 8-15 are flashing variants (stored as 0-7 with
 * flash bit set in the original ULA, here we just alias to 0-7).
 * -------------------------------------------------------------------------- */
static const uint8_t s_default_palette[16] = {
    /* Logical 0-7: direct 1:1 */
    BBC_COL_BLACK,   /* 0 */
    BBC_COL_RED,     /* 1 */
    BBC_COL_GREEN,   /* 2 */
    BBC_COL_YELLOW,  /* 3 */
    BBC_COL_BLUE,    /* 4 */
    BBC_COL_MAGENTA, /* 5 */
    BBC_COL_CYAN,    /* 6 */
    BBC_COL_WHITE,   /* 7 */
    /* Logical 8-15: same colours (flash handled via flash_state) */
    BBC_COL_BLACK,   /* 8  */
    BBC_COL_RED,     /* 9  */
    BBC_COL_GREEN,   /* 10 */
    BBC_COL_YELLOW,  /* 11 */
    BBC_COL_BLUE,    /* 12 */
    BBC_COL_MAGENTA, /* 13 */
    BBC_COL_CYAN,    /* 14 */
    BBC_COL_WHITE,   /* 15 */
};

/* --------------------------------------------------------------------------
 * Colour table: physical colour (0-7, BBB_COL_*) → RGB
 * BBC uses 3-bit RGB: bit 0=R, bit 1=G, bit 2=B
 * -------------------------------------------------------------------------- */
static const bbc_rgb_t s_colour_table[8] = {
    {0, 0, 0},       /* 0: Black   */
    {255, 0, 0},     /* 1: Red     */
    {0, 255, 0},     /* 2: Green   */
    {255, 255, 0},   /* 3: Yellow  */
    {0, 0, 255},     /* 4: Blue    */
    {255, 0, 255},   /* 5: Magenta */
    {0, 255, 255},   /* 6: Cyan    */
    {255, 255, 255}, /* 7: White   */
};

/* --------------------------------------------------------------------------
 * Rebuild lookup tables after any palette change
 * -------------------------------------------------------------------------- */
void bbc_video_ula_rebuild_tables(bbc_video_ula_t *ula)
{
    /* 1bpp (MODE 0, 3, 4, 6): MOS programs the full 4-bit palette nibble,
     * writing entries 0-7 to one colour and 8-15 to the other, so only the
     * top bit of the logical index (bit 3) is actually significant. */
    for (int byte = 0; byte < 256; byte++)
    {
        for (int px = 0; px < 8; px++)
        {
            uint8_t bit = (byte >> (7 - px)) & 1;
            uint8_t logical = bit << 3;
            ula->lut_1bpp[byte][px] = ula->palette[logical];
        }
    }

    /* 2bpp (MODE 1, 5): MOS spreads the two significant bits at nibble
     * positions 3 and 1 (bits 2 and 0 are redundant/noise in real palette
     * writes), not packed into the low 2 bits. */
    for (int byte = 0; byte < 256; byte++)
    {
        for (int px = 0; px < 4; px++)
        {
            uint8_t high = (byte >> (7 - px)) & 1; /* bits 7,6,5,4 */
            uint8_t low = (byte >> (3 - px)) & 1;  /* bits 3,2,1,0 */
            uint8_t logical = (high << 3) | (low << 1);
            ula->lut_2bpp[byte][px] = ula->palette[logical];
        }
    }

    /* 4bpp (MODE 2): Stuurt alle 4 de lijnen aan (bit3, bit2, bit1, bit0) */
    for (int byte = 0; byte < 256; byte++)
    {
        for (int px = 0; px < 2; px++)
        {
            uint8_t b3 = (byte >> (7 - px)) & 1;
            uint8_t b2 = (byte >> (5 - px)) & 1;
            uint8_t b1 = (byte >> (3 - px)) & 1;
            uint8_t b0 = (byte >> (1 - px)) & 1;
            uint8_t logical = (b3 << 3) | (b2 << 2) | (b1 << 1) | b0;
            ula->lut_4bpp[byte][px] = ula->palette[logical];
        }
    }
}
/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void bbc_video_ula_init(bbc_video_ula_t *ula)
{
    memset(ula, 0, sizeof(*ula));
    /* Install colour table */
    for (int i = 0; i < 8; i++)
        ula->colour_table[i] = s_colour_table[i];
    bbc_video_ula_reset(ula);
    ula->trace_version = 0;
}

void bbc_video_ula_reset(bbc_video_ula_t *ula)
{
    ula->control = 0;
    ula->teletext_mode = false;
    ula->bpp_mode = ULA_BPP_1;
    ula->pixels_per_byte = 8;
    ula->crtc_2mhz = false;
    ula->flash_state = false;

    /* Copy default palette */
    for (int i = 0; i < 16; i++)
        ula->palette[i] = s_default_palette[i];

    bbc_video_ula_rebuild_tables(ula);
    ULA_LOGD("reset");
    ula->trace_version = 0;
}

void bbc_video_ula_write(bbc_video_ula_t *ula, uint8_t addr, uint8_t data)
{
    if (!(addr & 1))
    {
        /*
         * &FE20 — Control register
         *
         * Bit 0: flash colour state (MOS toggles this ~1 Hz)
         * Bit 1: teletext mode select
         * Bits 3-2: chars-per-pixel-byte → bpp mode
         * Bit 4: CRTC 2 MHz (1 = 2 MHz, 0 = 1 MHz)
         * Bit 7: flash colour control
         */
        ula->control = data;
        ula->flash_state = (data & ULA_CTRL_FLASH_STATE) != 0;
        ula->teletext_mode = (data & ULA_CTRL_TELETEXT) != 0;
        ula->crtc_2mhz = (data & ULA_CTRL_CRTC_2MHZ) != 0;

        uint8_t bpp_field = (data & ULA_CTRL_BPP_MASK) >> ULA_CTRL_BPP_SHIFT;
        /* Hardware quirk: bits 3-2 encode the CRTC "pixel rate" class
         * (2/4/8/16 MHz for raw 0..3), not bpp directly, so the actual bpp
         * also depends on bit 4 (CRTC clock). Real MOS control bytes:
         *   2 MHz (80 col): MODE 0/3 raw=3 (1bpp), MODE 1 raw=2 (2bpp), MODE 2 raw=1 (4bpp)
         *   1 MHz (40 col): MODE 4/6 raw=2 (1bpp), MODE 5 raw=1 (2bpp)
         * giving bpp_mode = (crtc_2mhz ? 3 : 2) - raw_field. */
        int bpp_mode = (ula->crtc_2mhz ? 3 : 2) - (int)bpp_field;
        if (bpp_mode < 0)
            bpp_mode = 0;
        if (bpp_mode > 3)
            bpp_mode = 3;
        ula->bpp_mode = (uint8_t)bpp_mode;
        switch (ula->bpp_mode)
        {
        case ULA_BPP_1:
            ula->pixels_per_byte = 8;
            break;
        case ULA_BPP_2:
            ula->pixels_per_byte = 4;
            break;
        case ULA_BPP_4:
            ula->pixels_per_byte = 2;
            break;
        default:
            ula->pixels_per_byte = 1;
            break;
        }
        /* Rebuild tables as flash_state affects palette output */
        ula->trace_version++;
        logger_log("ULAWR", (unsigned)ula->trace_version, (unsigned)data);
        bbc_video_ula_rebuild_tables(ula);
    }
    else
    {
        /*
         * &FE21 — Palette register
         *
         * Bits 7-4: logical colour index (0-15)
         * Bits 3-0: physical colour data
         *
         * Hardware quirk: the physical colour bits are inverted (XOR 7)
         * before being stored. So physical = (data & 0x07) ^ 0x07.
         * This means writing 0 to bits 2-0 stores white, writing 7 stores black.
         */
        uint8_t logical = (data >> 4) & 0x0F;
        uint8_t physical = (data & 0x07) ^ 0x07;
        ula->palette[logical] = physical;
        ula->trace_version++;
        logger_log("ULAWR", (unsigned)ula->trace_version, (unsigned)((logical << 8) | physical));
        bbc_video_ula_rebuild_tables(ula);
        // ULA_LOGD("palette[%d] = %d (raw=%02X)", logical, physical, data);
    }
}

int bbc_video_ula_serialize(const bbc_video_ula_t *ula,
                            uint8_t data_byte,
                            uint8_t *out_colours,
                            bool cursor_active)
{
    /* --- MODE OVERRIDE --- */
    uint8_t effective_bpp = ula->bpp_mode;

    if (ula->force_mode4)
        effective_bpp = ULA_BPP_1; // Mode 4 = 1bpp
    else if (ula->force_mode5)
        effective_bpp = ULA_BPP_2; // Mode 5 = 2bpp

    int n;
    switch (effective_bpp)
    {
    case ULA_BPP_1:
        n = 8;
        for (int i = 0; i < 8; i++)
            out_colours[i] = ula->lut_1bpp[data_byte][i];
        break;

    case ULA_BPP_2:
        n = 4;
        for (int i = 0; i < 4; i++)
            out_colours[i] = ula->lut_2bpp[data_byte][i];
        break;

    case ULA_BPP_4:
        n = 2;
        for (int i = 0; i < 2; i++)
            out_colours[i] = ula->lut_4bpp[data_byte][i];
        break;

    default:
        n = 1;
        out_colours[0] = ula->palette[data_byte & 0x0F];
        break;
    }

    if (cursor_active)
    {
        for (int i = 0; i < n; i++)
            out_colours[i] ^= 7;
    }
    return n;
}

void bbc_video_ula_toggle_flash(bbc_video_ula_t *ula)
{
    ula->flash_state = !ula->flash_state;
    ula->control ^= ULA_CTRL_FLASH_STATE;
    bbc_video_ula_rebuild_tables(ula);
    ULA_LOGD("flash=%d", ula->flash_state);
}