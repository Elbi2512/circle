/*
 * WD1770 FDC emulation for ESP32/Circle (BBC Micro)
 *
 * Derived from B-em by Tom Walker.
 * Self-contained sector-level emulation for BBC Micro .ssd/.dsd images.
 *
 * Licence: GPL-2.0
 */

#include <string.h>
#include "wd1770.h"
extern void bbc_debug_log(const char *msg, unsigned val1, unsigned val2);

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define WD_LOGD(fmt, ...) ESP_LOGD("wd1770", fmt, ##__VA_ARGS__)
#define WD_LOGW(fmt, ...) ESP_LOGW("wd1770", fmt, ##__VA_ARGS__)
#else
#include <stdio.h>
#define WD_LOGD(fmt, ...) fprintf(stderr, "[wd] " fmt "\n", ##__VA_ARGS__)
#define WD_LOGW(fmt, ...) fprintf(stderr, "[wd] WARN: " fmt "\n", ##__VA_ARGS__)
#endif

/* Delay constants (in 2 MHz BBC clock cycles, zoals in B-em) */
#define DELAY_CMD_START 32    /* time before first command callback */
#define DELAY_SEEK_STEP 6000  /* ~3ms per step at slowest rate */
#define DELAY_SECTOR_GAP 5000 /* inter-sector gap for multi-sector ops */
#define DELAY_COMPLETE 100    /* time before completion callback */
#define DELAY_FAULT 200       /* time before fault callback */
#define DELAY_ABORT 200       /* time for force-interrupt abort */
#define DELAY_SEEK_ALLOW 800  /* allow time for Opus DDOS seek cancel */

/* INDEX pulse timing */
#define INDEX_PERIOD_CYCLES 400000
#define INDEX_PULSE_CYCLES 4000
/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void set_intrq(wd1770_t *fdc, bool state)
{
    bool was = fdc->intrq;
    fdc->intrq = state;

    /* Alleen triggeren op opgaande flank (false -> true) */
    if (state && !was && fdc->cb.irq)
        fdc->cb.irq(fdc->cb.user_ctx, state);
    else if (!state && fdc->cb.irq)
        fdc->cb.irq(fdc->cb.user_ctx, state);
}

static void set_drq(wd1770_t *fdc, bool state)
{
    fdc->drq = state;
    fdc->status = state
                      ? (fdc->status | WD1770_STATUS_DRQ)
                      : (fdc->status & ~WD1770_STATUS_DRQ);
    if (fdc->cb.drq)
        fdc->cb.drq(fdc->cb.user_ctx, state);
}

static void spinup(wd1770_t *fdc)
{
    fdc->motor_on = true;
    fdc->status |= WD1770_STATUS_MOTOR_ON;
}

static void spindown(wd1770_t *fdc) __attribute__((unused));
static void spindown(wd1770_t *fdc)
{
    fdc->motor_on = false;
    fdc->status &= ~WD1770_STATUS_MOTOR_ON;
}

static void completed(wd1770_t *fdc)
{
    fdc->status &= ~WD1770_STATUS_BUSY;
    fdc->cmd_started = false;
    fdc->buf_pos = 0;
    fdc->buf_count = 0;
    set_drq(fdc, false);
    fdc->delay_cycles = 0;
    set_intrq(fdc, true);

   // bbc_debug_log("FDC Command voltooid. Status =", (unsigned)fdc->status, 0);
}

static void fault(wd1770_t *fdc, uint8_t flags, const char *desc)
{
    (void)desc;
    set_drq(fdc, false);
    fdc->status &= ~WD1770_STATUS_BUSY;
    fdc->status |= flags;              /* Zet WD1770_STATUS_RNF (0x10) */
    fdc->cmd_started = false;
    fdc->buf_pos = 0;
    fdc->buf_count = 0;
    fdc->delay_cycles = 0;
    set_intrq(fdc, true);

    bbc_debug_log("FDC FAULT opgetreden. Status =", (unsigned)fdc->status, (unsigned)fdc->sector);
}

/* -------------------------------------------------------------------------
 * Type I (seek/step) helpers
 * ------------------------------------------------------------------------- */

static void seek_done(wd1770_t *fdc, uint8_t cmd)
{
    if ((cmd & 0x04) && fdc->cur_drive < 2)
    {
        WD_LOGD("seek done, track=%d", fdc->track);
    }
    if (fdc->cb.seek)
        fdc->cb.seek(fdc->cb.user_ctx, fdc->cur_drive, fdc->track);
    completed(fdc);
    fdc->seek_ok = false;
}

/* -------------------------------------------------------------------------
 * Type II (read/write sector) helpers
 * ------------------------------------------------------------------------- */

static void begin_read_sector(wd1770_t *fdc)
{
    fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_BUSY;
    fdc->type1_status = false;

    if (!fdc->cb.read_sector)
    {
        fault(fdc, WD1770_STATUS_RNF, "read_sector callback not set");
        return;
    }

    fdc->buf_count = 0;
    fdc->buf_pos = 0;
    int rc = fdc->cb.read_sector(fdc->cb.user_ctx,
                                 fdc->cur_drive, fdc->track, fdc->sector,
                                 fdc->cur_side, fdc->density,
                                 fdc->buf, &fdc->buf_count);

    if (rc != 0 || fdc->buf_count == 0)
    {
        fault(fdc, WD1770_STATUS_RNF, "sector not found");
        return;
    }

    /* Present byte zero; advance only when the CPU reads the data register. */
    fdc->data = fdc->buf[0];
    set_drq(fdc, true);
    fdc->delay_cycles = 0; // CPU-driven completion polling
}

static void begin_write_sector(wd1770_t *fdc)
{
    WD_LOGD("write sector drive=%d side=%d track=%d sector=%d dens=%d",
            fdc->cur_drive, fdc->cur_side, fdc->track, fdc->sector, fdc->density);

    if (fdc->write_protect)
    {
        fault(fdc, WD1770_STATUS_WRITE_PROT, "write protect");
        return;
    }
    if (!fdc->cb.write_sector)
    {
        fault(fdc, WD1770_STATUS_RNF, "write_sector callback not set");
        return;
    }

    fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_DRQ | WD1770_STATUS_BUSY;
    fdc->type1_status = false;
    fdc->buf_pos = 0;
    fdc->buf_count = 256;
    set_drq(fdc, true);
    fdc->delay_cycles = DELAY_COMPLETE;
}

static void finish_write_sector(wd1770_t *fdc, bool complete_command)
{
    bool deleted = (fdc->command & 0x01) != 0;
    int rc = fdc->cb.write_sector(fdc->cb.user_ctx,
                                  fdc->cur_drive, fdc->track, fdc->sector,
                                  fdc->cur_side, fdc->density, deleted,
                                  fdc->buf, fdc->buf_pos);
    if (rc != 0)
        fault(fdc, WD1770_STATUS_RNF, "write sector failed");
    else if (complete_command)
    {
        completed(fdc);
    }
    else
    {
        set_drq(fdc, false);
        fdc->buf_pos = 0;
        fdc->buf_count = 0;
        fdc->delay_cycles = 0;
    }
}

/* -------------------------------------------------------------------------
 * Command state machine
 * ------------------------------------------------------------------------- */

static void cmd_start(wd1770_t *fdc)
{
    uint8_t cmd = fdc->command;
    WD_LOGD("cmd_start op=%X cmd=%02X", cmd >> 4, cmd);

    switch (cmd >> 4)
    {
    case 0x0: /* Restore */
        fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_BUSY;
        fdc->type1_status = true;
        fdc->track = 0xFF;
        fdc->data = 0;
        fdc->seek_delta = (int8_t)(0 - (int)fdc->track);
        fdc->track = 0;
        fdc->delay_cycles = DELAY_SEEK_STEP;
        break;

    case 0x1: /* Seek */
        fdc->type1_status = true;
        fdc->delay_cycles = DELAY_SEEK_ALLOW;
        break;

    case 0x2: /* Step (no update) */
    case 0x3: /* Step (with update) */
        fdc->type1_status = true;
        if (cmd & 0x10)
            fdc->track += fdc->step_dir;
        fdc->delay_cycles = DELAY_SEEK_STEP;
        break;

    case 0x4: /* Step in (no update) */
    case 0x5: /* Step in (with update) */
        fdc->step_dir = 1;
        fdc->type1_status = true;
        if (cmd & 0x10)
            fdc->track++;
        fdc->delay_cycles = DELAY_SEEK_STEP;
        break;

    case 0x6: /* Step out (no update) */
    case 0x7: /* Step out (with update) */
        fdc->step_dir = -1;
        fdc->type1_status = true;
        if (cmd & 0x10 && fdc->track > 0)
            fdc->track--;
        fdc->delay_cycles = DELAY_SEEK_STEP;
        break;

    case 0x8: /* Read single sector */
    case 0x9: /* Read multiple sectors */
        fdc->in_gap = false;
        begin_read_sector(fdc);
        break;

    case 0xA: /* Write single sector */
    case 0xB: /* Write multiple sectors */
        fdc->in_gap = false;
        begin_write_sector(fdc);
        break;

    case 0xC: /* Read address */
        WD_LOGD("read address (stub) side=%d track=%d", fdc->cur_side, fdc->track);
        fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_BUSY;
        fdc->type1_status = false;
        fdc->delay_cycles = DELAY_FAULT;
        break;

    case 0xD: /* Force interrupt */
        fdc->delay_cycles = DELAY_ABORT;
        break;

    case 0xE: /* Read track */
        WD_LOGD("read track (stub) side=%d track=%d", fdc->cur_side, fdc->track);
        fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_BUSY;
        fdc->type1_status = false;
        fdc->delay_cycles = DELAY_FAULT;
        break;

    case 0xF: /* Write track */
        WD_LOGD("write track (stub) side=%d track=%d", fdc->cur_side, fdc->track);
        fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_BUSY;
        fdc->type1_status = false;
        fdc->delay_cycles = DELAY_FAULT;
        break;
    }
    fdc->cmd_started = true;
}

static void cmd_next(wd1770_t *fdc)
{
    uint8_t cmd = fdc->command;
    WD_LOGD("cmd_next op=%X cmd=%02X", cmd >> 4, cmd);

    switch (cmd >> 4)
    {
    case 0x0: /* Restore complete */
        fdc->track = 0;
        fdc->seek_delta = 0;
        seek_done(fdc, cmd);
        break;

    case 0x1: /* Seek */
        if (!fdc->seek_ok)
        {
            fdc->status &= ~WD1770_STATUS_BUSY;
            WD_LOGW("seek ignored: data register not written");
        }
        else
        {
            fdc->seek_delta = (int8_t)((int)fdc->data - (int)fdc->track);
            fdc->track = fdc->data;
            fdc->delay_cycles = DELAY_SEEK_STEP;
            fdc->cmd_started = false;
            seek_done(fdc, cmd);
        }
        break;

    case 0x2: /* Step (no update) complete */
    case 0x3: /* Step (with update) complete */
    case 0x4: /* Step in (no update) complete */
    case 0x5: /* Step in (with update) complete */
    case 0x6: /* Step out (no update) complete */
    case 0x7: /* Step out (with update) complete */
        seek_done(fdc, cmd);
        break;

    case 0x8: /* Read single sector complete */
        if (fdc->buf_pos < fdc->buf_count)
        {
            fdc->delay_cycles = DELAY_COMPLETE;
        }
        else
        {
            completed(fdc);
        }
        break;

    case 0x9: /* Read multiple sectors */
        if (fdc->status & (WD1770_STATUS_WRITE_PROT |
                           WD1770_STATUS_RNF |
                           WD1770_STATUS_CRC_ERROR))
        {
            completed(fdc);
        }
        else if (fdc->in_gap)
        {
            fdc->sector++;
            fdc->in_gap = false;
            begin_read_sector(fdc);
        }
        else
        {
            fdc->in_gap = true;
            fdc->delay_cycles = DELAY_SECTOR_GAP;
        }
        break;

    case 0xA: /* Write single sector complete */
        finish_write_sector(fdc, true);
        break;

    case 0xB: /* Write multiple sectors */
        if (fdc->status & (WD1770_STATUS_WRITE_PROT |
                           WD1770_STATUS_RNF |
                           WD1770_STATUS_CRC_ERROR))
        {
            finish_write_sector(fdc, true);
        }
        else if (fdc->in_gap)
        {
            finish_write_sector(fdc, false);
            fdc->sector++;
            fdc->in_gap = false;
            begin_write_sector(fdc);
        }
        else
        {
            fdc->in_gap = true;
            fdc->delay_cycles = DELAY_SECTOR_GAP;
        }
        break;

    case 0xC: /* Read address (stub) */
        fault(fdc, WD1770_STATUS_RNF, "read address not supported");
        break;

    case 0xD: /* Force interrupt */
        if (fdc->status & WD1770_STATUS_BUSY)
            fdc->status &= ~WD1770_STATUS_BUSY;
        else
            fdc->status = WD1770_STATUS_MOTOR_ON;
        set_intrq(fdc, true);
        fdc->seek_ok = false;
        WD_LOGD("force interrupt done");
        break;

    case 0xE: /* Read track (stub) */
    case 0xF: /* Write track (stub) */
        fault(fdc, WD1770_STATUS_RNF, "track-level op not supported");
        break;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void wd1770_init(wd1770_t *fdc, const wd1770_callbacks_t *callbacks)
{
    memset(fdc, 0, sizeof(*fdc));
    if (callbacks)
        fdc->cb = *callbacks;
    fdc->step_dir = 1;
    wd1770_reset(fdc);
}

void wd1770_reset(wd1770_t *fdc)
{
    fdc->status = 0;
    fdc->sector = 1;
    fdc->data = 0;
    fdc->command = 0;
    fdc->cmd_started = false;
    fdc->type1_status = true;
    fdc->seek_ok = false;
    fdc->in_gap = false;
    fdc->drq = false;
    fdc->intrq = false;
    fdc->delay_cycles = 0;
    fdc->buf_pos = 0;
    fdc->buf_count = 0;

    fdc->index_pulse = true;
    fdc->index_cycles = INDEX_PULSE_CYCLES;

    if (fdc->motor_on)
        fdc->status |= WD1770_STATUS_MOTOR_ON;

    WD_LOGD("reset index_pulse=%d", fdc->index_pulse);
}

uint8_t wd1770_read(wd1770_t *fdc, uint8_t reg)
{
    switch (reg & 0x03)
    {
    case 0:
    { /* Status register – reading clears INTRQ and error status */
        set_intrq(fdc, false);
        uint8_t s = fdc->status;
        if (fdc->type1_status)
        {
            if (fdc->track == 0)
                s |= WD1770_STATUS_TRACK0;
            if (fdc->motor_on)
                s |= WD1770_STATUS_SPIN_UP;
            if (fdc->index_pulse || !(fdc->status & WD1770_STATUS_BUSY))
                s |= WD1770_STATUS_INDEX;
        }
        else
        {
            /* Type 2/3 status: na uitlezen worden foutbits gewist */
            fdc->status &= ~(WD1770_STATUS_RNF | WD1770_STATUS_CRC_ERROR | WD1770_STATUS_LOST_DATA);
        }
        WD_LOGD("read status base=%02X type1=%d idx=%d motor=%d -> %02X",
                fdc->status, fdc->type1_status, fdc->index_pulse, fdc->motor_on, s);
        return s;
    }
    case 1:
        WD_LOGD("read track -> %02X", fdc->track);
        return fdc->track;
    case 2:
        WD_LOGD("read sector -> %02X", fdc->sector);
        return fdc->sector;
    case 3: { /* Data register – reading clears DRQ */
        uint8_t d = fdc->data;

        /* A late read after the final byte or after completion must not restart
         * the read-sector command or reschedule completion. */
        if (!fdc->drq || !(fdc->status & WD1770_STATUS_BUSY) || fdc->type1_status ||
            fdc->buf_pos >= fdc->buf_count)
        {
            if (fdc->buf_pos >= fdc->buf_count && (fdc->status & WD1770_STATUS_BUSY))
                set_drq(fdc, false);
            return d;
        }

        set_drq(fdc, false);

        if ((uint16_t)(fdc->buf_pos + 1) < fdc->buf_count) {
            fdc->buf_pos++;
            fdc->data = fdc->buf[fdc->buf_pos];
            fdc->delay_cycles = 128;
        } else {
            fdc->buf_pos = fdc->buf_count;
            if (fdc->delay_cycles <= 0)
                fdc->delay_cycles = DELAY_COMPLETE;
        }
        return d;
    }
    }
    return 0xFE;
}

void wd1770_write(wd1770_t *fdc, uint8_t reg, uint8_t val)
{
    switch (reg & 0x03)
    {
    case 0: /* Command register */
        /* Nieuw commando wist oude foutbits */
        fdc->status &= ~(WD1770_STATUS_RNF | WD1770_STATUS_CRC_ERROR | WD1770_STATUS_LOST_DATA);

        if ((val & 0xF0) != 0xD0)
        {
            if (fdc->status & WD1770_STATUS_BUSY)
            {
                WD_LOGW("cmd %02X rejected: device busy", val);
                return;
            }
            fdc->status |= WD1770_STATUS_BUSY;
            spinup(fdc);
        }
        set_intrq(fdc, false);
        fdc->cmd_started = false;
        fdc->command = val;
        fdc->delay_cycles = DELAY_CMD_START;
        WD_LOGD("write command %02X", val);
        break;

    case 1: /* Track register */
        if (fdc->status & WD1770_STATUS_BUSY)
        {
            WD_LOGW("track write %02X rejected: busy", val);
            return;
        }
        fdc->track = val;
        WD_LOGD("write track %02X", val);
        break;

    case 2: /* Sector register */
        if (fdc->status & WD1770_STATUS_BUSY)
        {
            WD_LOGW("sector write %02X rejected: busy", val);
            return;
        }
        fdc->sector = val;
        WD_LOGD("write sector %02X", val);
        break;

    case 3: /* Data register */
        set_drq(fdc, false);
        fdc->data = val;
        fdc->seek_ok = true;
        if ((fdc->status & WD1770_STATUS_BUSY) &&
            ((fdc->command & 0xE0) == 0xA0))
        {
            if (fdc->buf_pos < sizeof(fdc->buf))
                fdc->buf[fdc->buf_pos++] = val;
            if (fdc->buf_pos < fdc->buf_count)
                set_drq(fdc, true);
            else
                fdc->delay_cycles = DELAY_COMPLETE;
        }
        WD_LOGD("write data %02X", val);
        break;
    }
}

void wd1770_tick(wd1770_t *fdc, int32_t cycles)
{
    /* INDEX pulse generator */
    fdc->index_cycles -= cycles;
    while (fdc->index_cycles <= 0)
    {
        fdc->index_pulse = !fdc->index_pulse;
        fdc->index_cycles += fdc->index_pulse
                                 ? INDEX_PULSE_CYCLES
                                 : (INDEX_PERIOD_CYCLES - INDEX_PULSE_CYCLES);
    }

    /* Command state machine */
    if (fdc->delay_cycles <= 0)
        return;

    fdc->delay_cycles -= cycles;
    if (fdc->delay_cycles > 0)
        return;

    fdc->delay_cycles = 0;

    /* 1. Als het commando nog niet gestart is, start het eerst! */
    if (!fdc->cmd_started)
    {
        cmd_start(fdc);
        return;
    }

    /* 2. Type 2 transfer: ALLEEN actief als het commando al gestart is */
    if ((fdc->status & WD1770_STATUS_BUSY) && !fdc->type1_status)
    {
        if (fdc->buf_pos < fdc->buf_count)
        {
            set_drq(fdc, true);
        }
        else if ((fdc->command >> 4) == 0x9 ||
                 (fdc->command >> 4) == 0xB)
        {
            /* Multiple-sector commands need their inter-sector phase before
             * the next sector is fetched. */
            cmd_next(fdc);
        }
        else if ((fdc->command >> 4) == 0xA)
        {
            /* A completed write buffer must be committed through the disk
             * callback before the controller raises INTRQ. */
            finish_write_sector(fdc, true);
        }
        else
        {
            completed(fdc);
        }
        return;
    }

    /* 3. Type 1 commando's (Seek, Step, Restore) afronden */
    cmd_next(fdc);
}

void wd1770_select(wd1770_t *fdc, uint8_t drive, uint8_t side, uint8_t density)
{
    WD_LOGD("select drive=%d side=%d density=%d", drive, side, density);
    fdc->cur_drive = drive & 0x01;
    fdc->cur_side = side & 0x01;
    fdc->density = density & 0x01;
}

bool wd1770_get_drq(const wd1770_t *fdc)
{
    return fdc->drq;
}

bool wd1770_get_intrq(const wd1770_t *fdc)
{
    return fdc->intrq;
}

bool wd1770_get_motor(const wd1770_t *fdc)
{
    return fdc->motor_on;
}