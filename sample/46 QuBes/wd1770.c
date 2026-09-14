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
#define WD_LOGD(fmt, ...) ((void)0)
#define WD_LOGW(fmt, ...) ((void)0)
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

    bbc_debug_log("FDC Command voltooid. Status =", (unsigned)fdc->status, 0);
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

    /* Eerste byte klaarzetten en DRQ triggeren */
    fdc->data = fdc->buf[fdc->buf_pos++];
    set_drq(fdc, true);
    fdc->delay_cycles = 0; // GEEN automatische completion timer starten!
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

static void finish_write_sector(wd1770_t *fdc)
{
    bool deleted = (fdc->command & 0x01) != 0;
    int rc = fdc->cb.write_sector(fdc->cb.user_ctx,
                                  fdc->cur_drive, fdc->track, fdc->sector,
                                  fdc->cur_side, fdc->density, deleted,
                                  fdc->buf, fdc->buf_pos);
    if (rc != 0)
        fault(fdc, WD1770_STATUS_RNF, "write sector failed");
    else
        completed(fdc);
}

static int load_raw_track(wd1770_t *fdc)
{
    fdc->track_pos = 0;
    fdc->track_size = 0;

    for (uint8_t sector = 0; sector < 10; ++sector)
    {
        uint16_t length = 0;
        if (!fdc->cb.read_sector ||
            fdc->cb.read_sector(fdc->cb.user_ctx, fdc->cur_drive,
                                fdc->track, sector, fdc->cur_side,
                                fdc->density, fdc->track_data + fdc->track_size,
                                &length) != 0 || length != 256)
            return -1;
        fdc->track_size += length;
    }
    return 0;
}

static void begin_read_address(wd1770_t *fdc)
{
    fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_BUSY;
    fdc->type1_status = false;
    fdc->buf_pos = 0;
    fdc->buf_count = 6;
    fdc->buf[0] = fdc->track;
    fdc->buf[1] = fdc->cur_side;
    fdc->buf[2] = fdc->sector;
    fdc->buf[3] = 1;
    fdc->buf[4] = 0;
    fdc->buf[5] = 0;
    fdc->data = fdc->buf[fdc->buf_pos++];
    set_drq(fdc, true);
}

static void begin_read_track(wd1770_t *fdc)
{
    fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_BUSY;
    fdc->type1_status = false;
    if (load_raw_track(fdc) != 0)
    {
        fault(fdc, WD1770_STATUS_RNF, "track not found");
        return;
    }
    fdc->buf_pos = 0;
    fdc->buf_count = fdc->track_size;
    fdc->data = fdc->track_data[fdc->buf_pos++];
    set_drq(fdc, true);
}

static void begin_write_track(wd1770_t *fdc)
{
    if (fdc->write_protect || !fdc->cb.write_sector)
    {
        fault(fdc, fdc->write_protect ? WD1770_STATUS_WRITE_PROT : WD1770_STATUS_RNF,
              "track write unavailable");
        return;
    }
    fdc->status = WD1770_STATUS_MOTOR_ON | WD1770_STATUS_BUSY;
    fdc->type1_status = false;
    fdc->track_pos = 0;
    fdc->track_size = 2560;
    fdc->data = 0;
    set_drq(fdc, true);
}

static void finish_write_track(wd1770_t *fdc)
{
    for (uint8_t sector = 0; sector < 10; ++sector)
    {
        if (fdc->cb.write_sector(fdc->cb.user_ctx, fdc->cur_drive,
                                 fdc->track, sector, fdc->cur_side,
                                 fdc->density, false,
                                 fdc->track_data + sector * 256, 256) != 0)
        {
            fault(fdc, WD1770_STATUS_RNF, "track write failed");
            return;
        }
    }
    completed(fdc);
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
        fdc->seek_ok = false;
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
        if (cmd & 0x10)
        {
            if (fdc->track > 0)
                fdc->track--;
        }
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
        begin_read_address(fdc);
        break;

    case 0xD: /* Force interrupt */
        fdc->delay_cycles = DELAY_ABORT;
        break;

    case 0xE: /* Read track */
        begin_read_track(fdc);
        break;

    case 0xF: /* Write track */
        begin_write_track(fdc);
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
        finish_write_sector(fdc);
        break;

    case 0xB: /* Write multiple sectors */
        if (fdc->status & (WD1770_STATUS_WRITE_PROT |
                           WD1770_STATUS_RNF |
                           WD1770_STATUS_CRC_ERROR))
        {
            finish_write_sector(fdc);
        }
        else if (fdc->in_gap)
        {
            finish_write_sector(fdc);
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

    case 0xC: /* Read address */
        if (fdc->buf_pos >= fdc->buf_count)
            completed(fdc);
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

    case 0xE: /* Read track */
        if (fdc->buf_pos >= fdc->buf_count)
            completed(fdc);
        break;
    case 0xF: /* Write track */
        finish_write_track(fdc);
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
    fdc->track_pos = 0;
    fdc->track_size = 0;
    fdc->track_sector = 0;

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
        set_drq(fdc, false);
        uint8_t d = fdc->data;

        if (fdc->buf_pos < fdc->buf_count) {
            /* Volgende byte klaarzetten met floppy timing delay (128 cycli) */
            fdc->data = (fdc->command >> 4 == 0xE)
                            ? fdc->track_data[fdc->buf_pos++]
                            : fdc->buf[fdc->buf_pos++];
            fdc->delay_cycles = 128; 
        } else {
            /* Laatste byte (256): niet direct INTRQ vuren binnen de LDA instructie,
             * maar geef de 6502 64 cycli de tijd voor zijn PLA en RTI! */
            fdc->delay_cycles = DELAY_COMPLETE; // 100 cycli wachten vóór completed()
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
            (fdc->command >> 4) == 0xF)
        {
            if (fdc->track_pos < sizeof(fdc->track_data))
                fdc->track_data[fdc->track_pos++] = val;
            if (fdc->track_pos < fdc->track_size)
                set_drq(fdc, true);
            return;
        }
        if ((fdc->status & WD1770_STATUS_BUSY) &&
            ((fdc->command & 0xE0) == 0xA0))
        {
            if (fdc->buf_pos < sizeof(fdc->buf))
                fdc->buf[fdc->buf_pos++] = val;
            if (fdc->buf_pos < fdc->buf_count)
                set_drq(fdc, true);
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
        if ((fdc->command >> 4) == 0xF && fdc->track_pos >= fdc->track_size)
        {
            finish_write_track(fdc);
        }
        else if (fdc->buf_pos < fdc->buf_count)
        {
            set_drq(fdc, true);
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