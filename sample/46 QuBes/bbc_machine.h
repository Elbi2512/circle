#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "bbc_sysvia.h"
#include "bbc_uservia.h"
#include "bbc_video.h"
#include "wd1770.h"

struct vrEmu6502_s;
typedef struct vrEmu6502_s VrEmu6502;

#ifdef __cplusplus
extern "C" {
#endif

#define BBC_RAM_SIZE        (32 * 1024)   // 32 KB standaard BBC Model B RAM
#define BBC_ROM_BANK_SIZE   (16 * 1024)   // 16 KB per sideways bank
#define BBC_NUM_BANKS       16            // Bank 0 t/m 15
#define BBC_OS_SIZE         (16 * 1024)   // 16 KB OS 1.20 ROM

// Disk callback types (identiek aan je declaraties in beeb.cpp)
typedef int (*bbc_disk_read_fn)(void *user_ctx, uint8_t drive, uint8_t track, uint8_t sector,
                                uint8_t side, uint8_t density, uint8_t *buf, uint16_t *len);
typedef int (*bbc_disk_write_fn)(void *user_ctx, uint8_t drive, uint8_t track, uint8_t sector,
                                 uint8_t side, uint8_t density, bool deleted, const uint8_t *buf, uint16_t len);
typedef void (*bbc_disk_seek_fn)(void *user_ctx, uint8_t drive, uint8_t track);

typedef struct {
    void               *user_ctx;
    bbc_disk_read_fn    read_sector;
    bbc_disk_write_fn   write_sector;
    bbc_disk_seek_fn    seek;
    bool                mounted;
} bbc_drive_t;

// Geheugenstructuur met Sideways RAM
typedef struct {
    uint8_t main_ram[BBC_RAM_SIZE];                     // 0x0000 - 0x7FFF
    uint8_t sideways_banks[BBC_NUM_BANKS][BBC_ROM_BANK_SIZE]; // 0x8000 - 0xBFFF (Bank 0-15)
    bool    bank_is_ram[BBC_NUM_BANKS];                 // True = Schrijfbaar (Sideways RAM)
    uint8_t current_bank;                               // Huidige bank ($FE30 latch)
    uint8_t os_rom[BBC_OS_SIZE];                        // 0xC000 - 0xFFFF
} bbc_memory_t;

// Centrale Machine structuur
typedef struct bbc_machine {
    bbc_memory_t    mem;
    wd1770_t        fdc;
    bbc_drive_t     drives[2];
    uint8_t         fdc_latch;
    bool            fdc_drq_state;
    bbc_sysvia_t    sysvia;
    bbc_uservia_t   uservia;
    bbc_video_t     video;
    uint8_t         keyboard_matrix[16];
    
    // 6502 CPU registers
    uint16_t        pc;
    uint8_t         a, x, y, sp, status;
    bool            nmi_pending;
    bool            irq_pending;
    uint64_t        total_cycles;
    int32_t         crtc_acc;
    uint32_t        io_cycles_pending;
    uint32_t        io_accesses;
    VrEmu6502      *cpu;

    // Debug: last write into the Mode 7 screen memory region (0x7C00-0x7FFF)
    uint16_t        dbg_mode7_last_addr;
    uint8_t         dbg_mode7_last_val;
    uint16_t        dbg_mode7_last_pc;
    uint8_t         dbg_mode7_last_x;
    uint8_t         dbg_mode7_last_y;
    uint32_t        dbg_mode7_write_count;
    uint8_t         dbg_mode7_zp_d8;
    uint8_t         dbg_mode7_zp_d9;
    uint8_t         dbg_mode7_zp_f0;
    uint8_t         dbg_mode7_zp_88;
    uint8_t         dbg_mode7_zp_de;
    uint8_t         dbg_mode7_zp_df;
    uint32_t        dbg_irq_count;
} bbc_machine_t;

// Machine API voor beeb.cpp
void bbc_machine_init(bbc_machine_t *m, const uint8_t *os_rom, uint32_t os_size,
                      const uint8_t *basic_rom, uint32_t basic_size);
void bbc_machine_load_sideways_rom(bbc_machine_t *m, const uint8_t *rom_data, uint32_t size, uint8_t bank);
void bbc_machine_enable_sideways_ram(bbc_machine_t *m, uint8_t bank, bool enable);
void bbc_machine_mount_disk(bbc_machine_t *m, uint8_t drive,
                            bbc_disk_read_fn r_fn, bbc_disk_write_fn w_fn,
                            bbc_disk_seek_fn s_fn, void *ctx);
void bbc_machine_reset(bbc_machine_t *m);
int  bbc_machine_step(bbc_machine_t *m);
void bbc_machine_key_event(bbc_machine_t *m, uint8_t col, uint8_t row, bool pressed);
void bbc_machine_break(bbc_machine_t *m, bool shift);

#ifdef __cplusplus
}
#endif