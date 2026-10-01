/*
 * bbc_machine.c — BBC Micro Model B top-level integration
 *
 * Licence: zlib
 * Copyright (c) 2026 esp-beep project
 */

#include "bbc_machine.h"
#include "bbc_tape.h"
#include <string.h>
#include <stdio.h>
extern void bbc_debug_log(const char *msg, unsigned val1, unsigned val2);

/* ======================================================================
 * Forward declarations for static callback functions
 * ====================================================================== */

/* sysvia callbacks */
static void sv_sound_write(void *ctx, uint8_t data);
static bool sv_keyboard_read(void *ctx, uint8_t row, uint8_t col);
static void sv_latch_changed(void *ctx, uint8_t latch_bits);
static void sv_irq(void *ctx, bool state);

/* uservia callbacks */
static void uv_port_out(void *ctx, uint8_t port, uint8_t val, uint8_t ddr);
static uint8_t uv_port_in(void *ctx, uint8_t port);
static void uv_irq(void *ctx, bool state);

/* wd1770 callbacks */
static void fdc_irq(void *ctx, bool state);
static void fdc_drq(void *ctx, bool state);

/* tape callbacks and IO handlers */
static void tape_irq(void *ctx, bool state);
static void sv_motor_changed(void *ctx, bool on);
static uint8_t io_acia_read(uint16_t addr, void *ctx);
static void io_acia_write(uint16_t addr, uint8_t val, void *ctx);
static void io_serial_ula_write(uint16_t addr, uint8_t val, void *ctx);

/* wd1770 disk I/O wrappers (forward to m->disk_* callbacks) */
static int fdc_read_sector(void *ctx, uint8_t drive, uint8_t track, uint8_t sector, uint8_t side, uint8_t density, uint8_t *buf, uint16_t *len);
static int fdc_write_sector(void *ctx, uint8_t drive, uint8_t track, uint8_t sector, uint8_t side, uint8_t density, bool deleted, const uint8_t *buf, uint16_t len);
static void fdc_seek(void *ctx, uint8_t drive, uint8_t track);

/* video vsync callback */
static void video_vsync_cb(void *ctx, bool state);

/* bbc_memory I/O callbacks (per Sheila address range) */
static uint8_t io_crtc_read(uint16_t addr, void *ctx);
static void io_crtc_write(uint16_t addr, uint8_t val, void *ctx);
static uint8_t io_vidproc_read(uint16_t addr, void *ctx);
static void io_vidproc_write(uint16_t addr, uint8_t val, void *ctx);
static uint8_t io_sysvia_read(uint16_t addr, void *ctx);
static void io_sysvia_write(uint16_t addr, uint8_t val, void *ctx);
static uint8_t io_uservia_read(uint16_t addr, void *ctx);
static void io_uservia_write(uint16_t addr, uint8_t val, void *ctx);
static uint8_t io_fdc_read(uint16_t addr, void *ctx);
static void io_fdc_write(uint16_t addr, uint8_t val, void *ctx);
static uint8_t io_romsel_read(uint16_t addr, void *ctx);
static void io_romsel_write(uint16_t addr, uint8_t val, void *ctx);

/* ======================================================================
 * IRQ helpers
 * ====================================================================== */

static void update_irq(bbc_machine_t *m)
{
    if (m->irq.sysvia || m->irq.uservia || m->irq.acia)
    {
        bbc_cpu_irq(m->cpu);
    }
    else
    {
        bbc_cpu_clear_irq(m->cpu);
    }
}

/* ======================================================================
 * bbc_machine_init
 * ====================================================================== */

void bbc_machine_init(bbc_machine_t *m,
                      const uint8_t *os_rom, uint32_t os_size,
                      const uint8_t *basic_rom, uint32_t basic_size)
{
    memset(m, 0, sizeof(*m));

    /* Memory */
    m->mem = bbc_memory_create();

    if (os_rom && os_size)
        bbc_memory_load_rom(m->mem, os_rom, os_size, 0xC000);
    if (basic_rom && basic_size)
        bbc_memory_load_rom(m->mem, basic_rom, basic_size, 0x8000);

    /* CPU */
    m->cpu = bbc_cpu_create(
        bbc_memory_read,
        bbc_memory_write,
        m->mem);

    /* System VIA */
    {
        bbc_sysvia_callbacks_t cb = {
            .sound_write = sv_sound_write,
            .keyboard_read = sv_keyboard_read,
            .latch_changed = sv_latch_changed,
            .irq = sv_irq,
            .motor_changed = sv_motor_changed,
            .user_ctx = m,
        };
        bbc_sysvia_init(&m->sysvia, &cb);
    }

    /* User VIA */
    {
        bbc_uservia_callbacks_t cb = {
            .port_out = uv_port_out,
            .port_in = uv_port_in,
            .irq = uv_irq,
            .user_ctx = m,
        };
        bbc_uservia_init(&m->uservia, &cb);
    }

    /* WD1770 FDC */
    {
        wd1770_callbacks_t cb = {
            .read_sector = fdc_read_sector,
            .write_sector = fdc_write_sector,
            .seek = fdc_seek,
            .irq = fdc_irq,
            .drq = fdc_drq,
            .user_ctx = m,
        };
        wd1770_init(&m->fdc, &cb);
    }

    /* Tape */
    bbc_tape_init(&m->tape);
    bbc_tape_set_irq_cb(&m->tape, tape_irq, m);

    /* PSG */
    sn76489_init(&m->psg, 22050);

    /* Video */
    {
        uint8_t *ram = bbc_memory_get_ram(m->mem);
        bbc_video_init(&m->video, ram, 32768);
        bbc_video_set_vsync_callback(&m->video, video_vsync_cb, m);
    }

    /* I/O mappings */
    bbc_memory_set_range_callbacks(m->mem, 0xFE00, 2, io_crtc_read, io_crtc_write, m);
    bbc_memory_set_range_callbacks(m->mem, 0xFE20, 2, io_vidproc_read, io_vidproc_write, m);
    bbc_memory_set_range_callbacks(m->mem, 0xFE40, 16, io_sysvia_read, io_sysvia_write, m);
    bbc_memory_set_range_callbacks(m->mem, 0xFE60, 16, io_uservia_read, io_uservia_write, m);
    bbc_memory_set_range_callbacks(m->mem, 0xFE80, 8, io_fdc_read, io_fdc_write, m);
    bbc_memory_set_read_callback(m->mem, 0xFE30, io_romsel_read, m);
    bbc_memory_set_write_callback(m->mem, 0xFE30, io_romsel_write, m);
    bbc_memory_set_range_callbacks(m->mem, 0xFE08, 2, io_acia_read, io_acia_write, m);
    bbc_memory_set_write_callback(m->mem, 0xFE10, io_serial_ula_write, m);
}

void bbc_machine_load_sideways_rom(bbc_machine_t *m,
                                   const uint8_t *rom_data, uint32_t rom_size,
                                   uint8_t slot)
{
    bbc_memory_load_sideways_rom(m->mem, rom_data, rom_size, slot);
}

void bbc_machine_reset(bbc_machine_t *m)
{
    bbc_sysvia_reset(&m->sysvia);
    bbc_uservia_reset(&m->uservia);
    wd1770_reset(&m->fdc);
    sn76489_reset(&m->psg);
    bbc_video_reset(&m->video);

    m->irq.sysvia = false;
    m->irq.uservia = false;
    m->irq.acia = false;
    /* Start the 1 MHz VIA clock half a CPU cycle ahead of the 1 MHz CRTC
     * phase; this matches the board-level clock phase at raster IRQ edges. */
    m->cycle_acc = 1;

    if (m->fb_output)
    {
        bbc_video_set_output(&m->video, m->fb_output);
    }
    if (m->on_frame)
    {
        bbc_video_set_frame_callback(&m->video, m->on_frame, m->on_frame_ctx);
    }

    bbc_cpu_reset(m->cpu);
}

void bbc_machine_break(bbc_machine_t *m, bool shift_held)
{
    uint8_t *ram = bbc_memory_get_ram(m->mem);
    if (ram)
    {
        ram[0x0258] = shift_held ? 0x02 : 0x00;
    }

    bbc_sysvia_reset(&m->sysvia);
    bbc_uservia_reset(&m->uservia);
    wd1770_reset(&m->fdc);
    sn76489_reset(&m->psg);
    bbc_video_reset(&m->video);

    m->irq.sysvia = false;
    m->irq.uservia = false;
    m->irq.acia = false;
    m->cycle_acc = 1;
    m->crtc_acc = 0;

    if (m->fb_output)
    {
        bbc_video_set_output(&m->video, m->fb_output);
    }
    if (m->on_frame)
    {
        bbc_video_set_frame_callback(&m->video, m->on_frame, m->on_frame_ctx);
    }

    bbc_cpu_reset(m->cpu);
}

int bbc_machine_step(bbc_machine_t *m)
{
    int cycles = bbc_cpu_step(m->cpu);
    if (cycles <= 0)
        cycles = 1;

    /* Do not nest a DRQ NMI inside the DFS NMI handler.  The 6502 I flag is
     * set for the handler and cleared by RTI; deliver the pending request
     * only after that return. */
    if (m->fdc_drq_nmi_pending && !(bbc_cpu_get_p(m->cpu) & 0x04))
    {
        m->fdc_drq_nmi_pending = false;
        bbc_cpu_nmi(m->cpu);
    }

    /* The CPU runs at 2 MHz; both VIAs are clocked at 1 MHz. */
    m->cycle_acc += cycles;
    while (m->cycle_acc >= 2)
    {
        bbc_sysvia_tick(&m->sysvia, 1);
        bbc_uservia_tick(&m->uservia, 1);
        m->cycle_acc -= 2;
    }
    wd1770_tick(&m->fdc, cycles);
    bbc_tape_tick(&m->tape, cycles);

    int crtc_divider = m->video.ula.crtc_2mhz ? 1 : 2;

    m->crtc_acc += cycles;
    while (m->crtc_acc >= crtc_divider)
    {
        bbc_video_tick(&m->video);
        m->crtc_acc -= crtc_divider;
    }

    return cycles;
}

void bbc_machine_mount_disk(bbc_machine_t *m, uint8_t drive,
                            int (*read_sector)(void *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t *, uint16_t *),
                            int (*write_sector)(void *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, bool, const uint8_t *, uint16_t),
                            void (*seek)(void *, uint8_t, uint8_t),
                            void *disk_ctx)
{
    (void)drive;
    m->disk_read_sector = read_sector;
    m->disk_write_sector = write_sector;
    m->disk_seek = seek;
    m->disk_ctx = disk_ctx;
}

void bbc_machine_key_event(bbc_machine_t *m, uint8_t row, uint8_t col, bool pressed)
{
    if (row < BBC_KB_ROWS && col < BBC_KB_COLS)
    {
        m->keyboard.pressed[row][col] = pressed;
        bbc_sysvia_keyboard_updated(&m->sysvia);
    }
}

void bbc_machine_set_video_output(bbc_machine_t *m, bbc_video_output_t *out)
{
    m->fb_output = out;
    if (out)
    {
        bbc_video_set_output(&m->video, out);
    }
}

void bbc_machine_set_frame_callback(bbc_machine_t *m,
                                    void (*cb)(void *ctx), void *ctx)
{
    m->on_frame = cb;
    m->on_frame_ctx = ctx;
    bbc_video_set_frame_callback(&m->video, cb, ctx);
}

/* System VIA callbacks */
static void sv_latch_changed(void *ctx, uint8_t latch_bits)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    static const uint32_t screen_bases[4] = { 0x4000, 0x6000, 0x3000, 0x5800 };
    bbc_video_set_screen_base(&m->video, screen_bases[(latch_bits >> 4) & 3]);
    if (!(latch_bits & (1u << BBC_LATCH_SOUND_WE)))
    {
        uint8_t pa_data = m6522_get_port_a(&m->sysvia.via);
        sn76489_write(&m->psg, pa_data);
    }
}

static void sv_sound_write(void *ctx, uint8_t data)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    sn76489_write(&m->psg, data);
}

static bool sv_keyboard_read(void *ctx, uint8_t row, uint8_t col)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    if (row < BBC_KB_ROWS && col < BBC_KB_COLS)
    {
        return m->keyboard.pressed[row][col];
    }
    return false;
}

static void sv_irq(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    m->irq.sysvia = state;
    update_irq(m);
}

/* User VIA callbacks */
static void uv_port_out(void *ctx, uint8_t port, uint8_t val, uint8_t ddr)
{
    (void)ctx;
    (void)port;
    (void)val;
    (void)ddr;
}

static uint8_t uv_port_in(void *ctx, uint8_t port)
{
    (void)ctx;
    (void)port;
    return 0xFF;
}

static void uv_irq(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    m->irq.uservia = state;
    update_irq(m);
}

/* Disk wrappers */
static int fdc_read_sector(void *ctx,
                           uint8_t drive, uint8_t track, uint8_t sector,
                           uint8_t side, uint8_t density,
                           uint8_t *buf, uint16_t *len)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    if (!m->disk_read_sector)
        return -1;

    return m->disk_read_sector(m->disk_ctx, drive, track, sector, side, density, buf, len);
}

static int fdc_write_sector(void *ctx,
                            uint8_t drive, uint8_t track, uint8_t sector,
                            uint8_t side, uint8_t density, bool deleted,
                            const uint8_t *buf, uint16_t len)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    if (!m->disk_write_sector)
        return -1;
    return m->disk_write_sector(m->disk_ctx, drive, track, sector, side, density, deleted, buf, len);
}

static void fdc_seek(void *ctx, uint8_t drive, uint8_t track)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    if (m->disk_seek)
        m->disk_seek(m->disk_ctx, drive, track);
}

/* Video VSYNC callback */
static void video_vsync_cb(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    bbc_sysvia_vsync(&m->sysvia, state);
}

/* CRT and ULA callbacks */
static uint8_t io_crtc_read(uint16_t addr, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    return bbc_video_crtc_read(&m->video, (uint8_t)(addr & 1));
}
static void io_crtc_write(uint16_t addr, uint8_t val, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    bbc_video_crtc_write(&m->video, (uint8_t)(addr & 1), val);
}

static uint8_t io_vidproc_read(uint16_t addr, void *ctx)
{
    (void)addr;
    (void)ctx;
    return 0xFF;
}
static void io_vidproc_write(uint16_t addr, uint8_t val, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    bbc_video_vidproc_write(&m->video, (uint8_t)(addr & 1), val);
}

static uint8_t io_sysvia_read(uint16_t addr, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    return bbc_sysvia_read(&m->sysvia, (uint8_t)(addr & 0x0F));
}
static void io_sysvia_write(uint16_t addr, uint8_t val, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    bbc_sysvia_write(&m->sysvia, (uint8_t)(addr & 0x0F), val);
}

static uint8_t io_uservia_read(uint16_t addr, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    return bbc_uservia_read(&m->uservia, (uint8_t)(addr & 0x0F));
}
static void io_uservia_write(uint16_t addr, uint8_t val, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    bbc_uservia_write(&m->uservia, (uint8_t)(addr & 0x0F), val);
}

static uint8_t io_romsel_read(uint16_t addr, void *ctx)
{
    (void)addr;
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    return bbc_memory_get_romsel(m->mem);
}

static void io_romsel_write(uint16_t addr, uint8_t val, void *ctx)
{
    (void)addr;
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    bbc_memory_set_romsel(m->mem, val & 0x0F);
}

static uint8_t io_acia_read(uint16_t addr, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    return bbc_tape_read(&m->tape, (uint8_t)(addr & 1));
}

static void io_acia_write(uint16_t addr, uint8_t val, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    bbc_tape_write(&m->tape, (uint8_t)(addr & 1), val);
}

static void io_serial_ula_write(uint16_t addr, uint8_t val, void *ctx)
{
    (void)addr;
    (void)ctx;
    (void)val;
}

static void tape_irq(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    m->irq.acia = state;
    update_irq(m);
}

static void sv_motor_changed(void *ctx, bool on)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    bbc_tape_set_motor(&m->tape, on);
}

int bbc_machine_mount_tape(bbc_machine_t *m, const char *uef_path)
{
    if (!uef_path)
    {
        bbc_tape_free(&m->tape);
        return 0;
    }
    return bbc_tape_load_uef(&m->tape, uef_path);
}

/* ======================================================================
 * WD1770 callbacks
 * ====================================================================== */

static void fdc_irq(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    m->fdc.intrq = state;

    if (state)
    {
        /* A completed command supersedes any DRQ edge left from its final
         * byte; do not deliver that stale NMI after the INTRQ handler. */
        m->fdc_drq_nmi_pending = false;
        bbc_cpu_nmi(m->cpu);
    }
}

static void fdc_drq(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;

    if (state && !m->fdc_drq_state)
    {
        m->fdc_drq_nmi_pending = true;
    }

    m->fdc_drq_state = state;
}

static uint8_t io_fdc_read(uint16_t addr, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;

    if (!(addr & 0x04))
    {
        return 0xFF;
    }

    uint8_t reg = addr & 0x03;

    if (reg == 0) // &FE84: Acorn 1770 Status / DRQ Latch
    {
        uint8_t raw = wd1770_read(&m->fdc, 0);

        /* The BBC interface exposes DRQ as an active-low bit 7 latch. */
        return (raw & 0x7F) | (m->fdc_drq_state ? 0 : 0x80);
    }
    else if (reg == 3) // &FE87: Data register
    {
        /* Real BBC hardware only clears the external DRQ latch when the CPU is
         * actually reading a valid byte. A late or spurious read after transfer
         * completion must not restart or advance the transfer state. */
        if (!m->fdc_drq_state || !(m->fdc.status & WD1770_STATUS_BUSY) ||
            m->fdc.type1_status || m->fdc.buf_pos >= m->fdc.buf_count)
        {
            return m->fdc.data;
        }

        m->fdc_drq_state = false;
        return wd1770_read(&m->fdc, 3);
    }

    return wd1770_read(&m->fdc, reg);
}

static void io_fdc_write(uint16_t addr, uint8_t val, void *ctx)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;

    /* &FE80–&FE83: Control Latch (Drive, Side, Density) */
    if (!(addr & 0x04))
    {
        m->fdc_latch = val;
        uint8_t drive = (val >> 1) & 1;
        uint8_t side = (val >> 2) & 1;
        uint8_t density = (val >> 3) & 1;
        wd1770_select(&m->fdc, drive, side, density);
        return;
    }

    /* &FE84–&FE87: WD1770 chipregisters (reg 0..3) */
    uint8_t reg = addr & 0x03;

    if (reg == 0) // &FE84: Command register
    {
        m->fdc.intrq = false;
        m->fdc_drq_state = false;
        m->fdc_drq_nmi_pending = false;
        wd1770_write(&m->fdc, 0, val);
    }
    else if (reg == 1) // &FE85: Track register
    {
        wd1770_write(&m->fdc, 1, val);
    }
    else if (reg == 2) // &FE86: Sector register
    {
        // bbc_debug_log("FDC-W &FE86 Sector register ingesteld op =", (unsigned)val, 0);
        wd1770_write(&m->fdc, 2, val);
    }
    else if (reg == 3) // &FE87: Data register
    {
        m->fdc_drq_state = false;
        wd1770_write(&m->fdc, 3, val);
    }
}