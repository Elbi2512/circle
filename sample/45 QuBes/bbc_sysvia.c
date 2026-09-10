/*
 * bbc_sysvia.c — BBC Micro System VIA (IC3) for Circle Bare-metal
 *
 * Implements the BBC Micro-specific behaviour layered on top of the generic
 * m6522_t emulation. Logic derived from B-em sysvia.c by Tom Walker (GPL-2).
 *
 * Licence: GPL-2.0
 * Copyright (c) 2026 esp-beep project / Circle port
 */

#include <string.h>
#include "bbc_sysvia.h"

#define SV_LOGD(fmt, ...) /* no-op */
#define SV_LOGW(fmt, ...) /* no-op */

/* -------------------------------------------------------------------------
 * Internal: update CA2 ("any key pressed" signal) on System VIA.
 * ------------------------------------------------------------------------- */
#define BBC_KB_ROWS_MAX  8
#define BBC_KB_COLS_MAX 10

static void sysvia_update_ca2(bbc_sysvia_t *sv)
{
    if (!sv->cb.keyboard_read) return;

    bool any = false;
    uint8_t col = sv->via.pa.outr & 0x0F;
    bool autoscan = !!(sv->latch & (1u << BBC_LATCH_KB_AUTOSCAN));

    if (autoscan) {
        /* Scan all columns, rows 1-7 */
        for (uint8_t c = 0; c < BBC_KB_COLS_MAX && !any; c++)
            for (uint8_t r = 1; r < BBC_KB_ROWS_MAX && !any; r++)
                if (sv->cb.keyboard_read(sv->cb.user_ctx, r, c))
                    any = true;
    } else {
        /* Scan all rows in the currently selected column */
        if (col < BBC_KB_COLS_MAX)
            for (uint8_t r = 1; r < BBC_KB_ROWS_MAX && !any; r++)
                if (sv->cb.keyboard_read(sv->cb.user_ctx, r, col))
                    any = true;
    }

    /* CA2 goes HIGH when a key is detected, LOW when none */
    m6522_set_ca2(&sv->via, any);
}

/* -------------------------------------------------------------------------
 * Internal: update addressable latch IC32 from Port B output
 * ------------------------------------------------------------------------- */
static void sysvia_update_latch(bbc_sysvia_t *sv, uint8_t portb_val)
{
    uint8_t bit_addr  = portb_val & 0x07;       /* PB0-PB2 */
    bool    bit_value = (portb_val >> 3) & 0x01; /* PB3     */

    uint8_t old_latch = sv->latch;

    if (bit_value)
        sv->latch |=  (1u << bit_addr);
    else
        sv->latch &= ~(1u << bit_addr);

    if (sv->latch != old_latch) {
        SV_LOGD("IC32 = %02X", sv->latch);
        if (sv->cb.latch_changed)
            sv->cb.latch_changed(sv->cb.user_ctx, sv->latch);

        /* IC32 bit 2 = tape motor relay (active HIGH) */
        bool motor_old = !!(old_latch  & (1u << BBC_LATCH_TAPE_MOTOR));
        bool motor_new = !!(sv->latch  & (1u << BBC_LATCH_TAPE_MOTOR));
        if (motor_old != motor_new && sv->cb.motor_changed)
            sv->cb.motor_changed(sv->cb.user_ctx, motor_new);
    }
}

/* -------------------------------------------------------------------------
 * Internal: check whether sound chip write should fire
 * ------------------------------------------------------------------------- */
static void sysvia_check_sound(bbc_sysvia_t *sv, uint8_t old_latch)
{
    bool was_enabled = !(old_latch    & (1u << BBC_LATCH_SOUND_WE));
    bool now_enabled = !(sv->latch    & (1u << BBC_LATCH_SOUND_WE));

    if (!was_enabled && now_enabled) {
        /* Falling edge of WE — data on Port A goes to SN76489 */
        uint8_t snd_data = sv->via.pa.outr;
        SV_LOGD("sound write %02X", snd_data);
        if (sv->cb.sound_write)
            sv->cb.sound_write(sv->cb.user_ctx, snd_data);
    }
}

/* -------------------------------------------------------------------------
 * m6522 generic callbacks (wired up in bbc_sysvia_init)
 * ------------------------------------------------------------------------- */

static void sysvia_port_out(void *user_ctx, uint8_t port, uint8_t val, uint8_t ddr)
{
    bbc_sysvia_t *sv = (bbc_sysvia_t *)user_ctx;
    (void)ddr;

    if (port == 1) {
        /* Port B → addressable latch IC32 */
        uint8_t old_latch = sv->latch;
        sysvia_update_latch(sv, val);
        sysvia_check_sound(sv, old_latch);
        /* IC32 change may affect keyboard autoscan → update CA2 */
        sysvia_update_ca2(sv);
    } else {
        /* Port A (slow data bus) — keyboard row/col select + SN76489 data. */
        SV_LOGD("port A out %02X ddr=%02X", val, ddr);
        if (!(sv->latch & (1u << BBC_LATCH_SOUND_WE))) {
            if (sv->cb.sound_write)
                sv->cb.sound_write(sv->cb.user_ctx, val);
        }
        /* Port A change (new row/col) → update CA2 */
        sysvia_update_ca2(sv);
    }
}

static uint8_t sysvia_port_in(void *user_ctx, uint8_t port)
{
    bbc_sysvia_t *sv = (bbc_sysvia_t *)user_ctx;

    if (port == 0) {
        uint8_t pa_out = sv->via.pa.outr;
        uint8_t row = (pa_out >> 4) & 0x07;
        uint8_t col = pa_out & 0x0F;

        uint8_t result = pa_out & 0x7Fu;  /* clear bit 7 by default */

        /* Only drive the keyboard output when KBD_WE is LOW (active) */
        if (!(sv->latch & (1u << BBC_LATCH_KB_AUTOSCAN))) {
            if (sv->cb.keyboard_read &&
                sv->cb.keyboard_read(sv->cb.user_ctx, row, col)) {
                /* Key is pressed — drive bit 7 HIGH */
                result |= 0x80u;
            }
        }

        SV_LOGD("kbd read row=%d col=%d -> bit7=%d", row, col, (result >> 7) & 1);
        return result;
    }

    /* Port B */
    uint8_t val = 0xFF;
    /* PB4 = joystick fire 0 (active low) */
    if (sv->joy_fire0) val &= ~0x10;
    /* PB5 = joystick fire 1 (active low) */
    if (sv->joy_fire1) val &= ~0x20;
    return val;
}

static void sysvia_irq(void *user_ctx, bool state)
{
    bbc_sysvia_t *sv = (bbc_sysvia_t *)user_ctx;
    SV_LOGD("IRQ %s", state ? "assert" : "clear");
    if (sv->cb.irq)
        sv->cb.irq(sv->cb.user_ctx, state);
}

static void sysvia_control_out(void *user_ctx, uint8_t line, bool state)
{
    (void)user_ctx; (void)line; (void)state;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void bbc_sysvia_init(bbc_sysvia_t *sv, const bbc_sysvia_callbacks_t *callbacks)
{
    memset(sv, 0, sizeof(*sv));
    if (callbacks)
        sv->cb = *callbacks;

    m6522_callbacks_t via_cb = {
        .port_out    = sysvia_port_out,
        .port_in     = sysvia_port_in,
        .irq         = sysvia_irq,
        .control_out = sysvia_control_out,
        .user_ctx    = sv,
    };
    m6522_init(&sv->via, &via_cb);
    bbc_sysvia_reset(sv);
}

void bbc_sysvia_reset(bbc_sysvia_t *sv)
{
    m6522_reset(&sv->via);
    sv->latch     = 0;
    sv->joy_fire0 = false;
    sv->joy_fire1 = false;
    SV_LOGD("reset");
}

uint8_t bbc_sysvia_read(bbc_sysvia_t *sv, uint8_t reg)
{
    return m6522_read(&sv->via, reg);
}

void bbc_sysvia_write(bbc_sysvia_t *sv, uint8_t reg, uint8_t val)
{
    m6522_write(&sv->via, reg, val);
}

void bbc_sysvia_tick(bbc_sysvia_t *sv, int32_t cycles)
{
    m6522_tick(&sv->via, cycles);
}

void bbc_sysvia_vsync(bbc_sysvia_t *sv, bool state)
{
    m6522_set_ca1(&sv->via, state);
}

void bbc_sysvia_adc_eoc(bbc_sysvia_t *sv, bool state)
{
    m6522_set_cb1(&sv->via, state);
}

void bbc_sysvia_set_joystick(bbc_sysvia_t *sv, bool fire0, bool fire1)
{
    sv->joy_fire0 = fire0;
    sv->joy_fire1 = fire1;
}

uint8_t bbc_sysvia_get_latch(const bbc_sysvia_t *sv)
{
    return sv->latch;
}

void bbc_sysvia_keyboard_updated(bbc_sysvia_t *sv)
{
    sysvia_update_ca2(sv);
}