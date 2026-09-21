#include "bbc_machine.h"
#include "bbc_cpu.h"
#include <string.h>

extern uint8_t bbc_mem_read(bbc_machine_t *m, uint16_t addr);
extern void    bbc_mem_write(bbc_machine_t *m, uint16_t addr, uint8_t val);

static void update_via_irq(bbc_machine_t *m)
{
    bool was = m->irq_pending;
    m->irq_pending = m6522_get_irq(&m->sysvia.via) ||
                     m6522_get_irq(&m->uservia.via);
    if (m->irq_pending && !was)
        m->dbg_irq_count++;
}

static void sv_sound_write(void *ctx, uint8_t data)
{
    (void)ctx;
    (void)data;
}

static bool sv_keyboard_read(void *ctx, uint8_t row, uint8_t col)
{
    /* Caller (VIA autoscan) passes hardware axes: row=0-7, col=0-9.
     * bbc_machine_key_event stores the opposite way (index=0-9 "row", bit=0-7
     * "col"), so the array index/bit must be swapped here to match. */
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    return col < 16 && row < 8 && (m->keyboard_matrix[col] & (1u << row));
}

static void sv_latch_changed(void *ctx, uint8_t latch_bits)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    static const uint32_t screen_bases[4] = {
        0x4000, 0x6000, 0x3000, 0x5800
    };
    bbc_video_set_screen_base(&m->video, screen_bases[(latch_bits >> 4) & 3]);
}

static void sv_irq(void *ctx, bool state)
{
    (void)state;
    update_via_irq((bbc_machine_t *)ctx);
}

static void sv_motor_changed(void *ctx, bool on)
{
    (void)ctx;
    (void)on;
}

static void uv_port_out(void *ctx, uint8_t port, uint8_t value, uint8_t ddr)
{
    (void)ctx;
    (void)port;
    (void)value;
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
    (void)state;
    update_via_irq((bbc_machine_t *)ctx);
}

static void video_vsync(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    m6522_set_ca1(&m->sysvia.via, state);
}

static int fdc_read_sector(void *ctx, uint8_t drive, uint8_t track, uint8_t sector,
                           uint8_t side, uint8_t density, uint8_t *buf, uint16_t *len)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    if (drive >= 2 || !m->drives[drive].mounted || !m->drives[drive].read_sector)
        return -1;
    return m->drives[drive].read_sector(m->drives[drive].user_ctx, drive, track,
                                        sector, side, density, buf, len);
}

static int fdc_write_sector(void *ctx, uint8_t drive, uint8_t track, uint8_t sector,
                            uint8_t side, uint8_t density, bool deleted,
                            const uint8_t *buf, uint16_t len)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    if (drive >= 2 || !m->drives[drive].mounted || !m->drives[drive].write_sector)
        return -1;
    return m->drives[drive].write_sector(m->drives[drive].user_ctx, drive, track,
                                         sector, side, density, deleted, buf, len);
}

static void fdc_seek(void *ctx, uint8_t drive, uint8_t track)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    if (drive < 2 && m->drives[drive].mounted && m->drives[drive].seek)
        m->drives[drive].seek(m->drives[drive].user_ctx, drive, track);
}

static void fdc_irq(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    m->nmi_pending = state;
}

static void fdc_drq(void *ctx, bool state)
{
    bbc_machine_t *m = (bbc_machine_t *)ctx;
    if (state && !m->fdc_drq_state)
        m->nmi_pending = true;
    m->fdc_drq_state = state;
}

void bbc_machine_init(bbc_machine_t *m, const uint8_t *os_rom, uint32_t os_size,
                      const uint8_t *basic_rom, uint32_t basic_size)
{
    memset(m, 0, sizeof(bbc_machine_t));
    memset(m->mem.sideways_banks, 0xFF, sizeof(m->mem.sideways_banks));

    bbc_sysvia_callbacks_t sysvia_callbacks = {
        sv_sound_write, sv_keyboard_read, sv_latch_changed,
        sv_irq, sv_motor_changed, m
    };
    bbc_sysvia_init(&m->sysvia, &sysvia_callbacks);

    bbc_uservia_callbacks_t uservia_callbacks = {
        uv_port_out, uv_port_in, uv_irq, m
    };
    bbc_uservia_init(&m->uservia, &uservia_callbacks);

    wd1770_callbacks_t fdc_callbacks = {
        fdc_read_sector, fdc_write_sector, fdc_seek,
        fdc_irq, fdc_drq, m
    };
    wd1770_init(&m->fdc, &fdc_callbacks);

    // 1. Laad OS 1.20 ROM in $C000 - $FFFF
    if (os_rom && os_size <= BBC_OS_SIZE)
    {
        memcpy(m->mem.os_rom, os_rom, os_size);
    }

    // 2. Laad BBC BASIC standaard in ROM Bank 15
    if (basic_rom && basic_size <= BBC_ROM_BANK_SIZE)
    {
        bbc_machine_load_sideways_rom(m, basic_rom, basic_size, 15);
    }

    // 3. Configureer Sideways RAM
    // Banken 4 t/m 7 instellen als Sideways RAM (schrijfbaar)
    for (int b = 4; b <= 7; b++)
    {
        bbc_machine_enable_sideways_ram(m, b, true);
    }

    // Standaard bank 15 actief
    m->mem.current_bank = 15;
    bbc_video_init(&m->video, m->mem.main_ram, BBC_RAM_SIZE);
    bbc_video_set_vsync_callback(&m->video, video_vsync, m);
    bbc_cpu_init(m);
}

void bbc_machine_load_sideways_rom(bbc_machine_t *m, const uint8_t *rom_data, uint32_t size, uint8_t bank)
{
    if (bank < BBC_NUM_BANKS && rom_data && size <= BBC_ROM_BANK_SIZE)
    {
        memcpy(m->mem.sideways_banks[bank], rom_data, size);
        m->mem.bank_is_ram[bank] = false; // Is ROM, dus niet schrijfbaar
    }
}

void bbc_machine_enable_sideways_ram(bbc_machine_t *m, uint8_t bank, bool enable)
{
    if (bank < BBC_NUM_BANKS)
    {
        m->mem.bank_is_ram[bank] = enable;
    }
}

void bbc_machine_mount_disk(bbc_machine_t *m, uint8_t drive,
                            bbc_disk_read_fn r_fn, bbc_disk_write_fn w_fn,
                            bbc_disk_seek_fn s_fn, void *ctx)
{
    if (drive < 2)
    {
        m->drives[drive].read_sector = r_fn;
        m->drives[drive].write_sector = w_fn;
        m->drives[drive].seek = s_fn;
        m->drives[drive].user_ctx = ctx;
        m->drives[drive].mounted = true;
    }
}

void bbc_machine_reset(bbc_machine_t *m)
{
    bbc_sysvia_reset(&m->sysvia);
    bbc_uservia_reset(&m->uservia);
    wd1770_reset(&m->fdc);
    m->fdc_drq_state = false;
    bbc_video_reset(&m->video);
    m->crtc_acc = 0;
    m->irq_pending = false;
    m->nmi_pending = false;
    bbc_cpu_reset(m);
    m->total_cycles = 0;
}

void bbc_machine_break(bbc_machine_t *m, bool shift)
{
    (void)shift;
    bbc_machine_reset(m);
}

void bbc_machine_key_event(bbc_machine_t *m, uint8_t col, uint8_t row, bool pressed)
{
    // BBC toetsenbordmatrix: 10 rijen (0..9) x 8 kolommen (0..7)
    if (row < 16 && col < 8)
    {
        if (pressed)
            m->keyboard_matrix[row] |= (1 << col);
        else
            m->keyboard_matrix[row] &= ~(1 << col);
        bbc_sysvia_keyboard_updated(&m->sysvia);
    }
}
