#include "bbc_machine.h"
#include "vrEmu6502.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
//#include <cstdint>

// Forward declarations voor I/O afhandeling
static uint8_t bbc_io_read(bbc_machine_t *m, uint16_t addr);
static void    bbc_io_write(bbc_machine_t *m, uint16_t addr, uint8_t val);

static void bbc_io_timing(bbc_machine_t *m)
{
    uint32_t bus_phase = m->total_cycles;
    if (m->cpu)
    {
        uint8_t opcode_cycles = vrEmu6502GetOpcodeCycles(m->cpu);
        uint8_t remaining = vrEmu6502GetOpcodeCycle(m->cpu);
        uint8_t elapsed = opcode_cycles >= remaining
                              ? opcode_cycles - remaining
                              : 0;
        bus_phase += elapsed;
    }
    bus_phase += m->io_accesses++;
    m->io_cycles_pending += 1 + (bus_phase & 1u);
}

uint8_t bbc_mem_read(bbc_machine_t *m, uint16_t addr)
{
    // 0x0000 - 0x7FFF: Hoofdgeheugen (32 KB RAM)
    if (addr < 0x8000)
    {
        return m->mem.main_ram[addr];
    }
    // 0x8000 - 0xBFFF: Paged ROM / Sideways RAM (16 KB geselecteerde bank)
    else if (addr < 0xC000)
    {
        uint8_t bank = m->mem.current_bank & 0x0F;
        return m->mem.sideways_banks[bank][addr - 0x8000];
    }
    // 0xC000 - 0xFBFF: OS 1.20 ROM (Deel 1)
    else if (addr < 0xFC00)
    {
        return m->mem.os_rom[addr - 0xC000];
    }
    // 0xFC00 - 0xFEFF: Hardware I/O area; unmapped reads fall through to ROM.
    else if (addr < 0xFF00)
    {
        bbc_io_timing(m);
        if (addr >= 0xFE00)
        {
            return bbc_io_read(m, addr); // SHEILA
        }
        return m->mem.os_rom[addr - 0xC000];
    }
    // 0xFF00 - 0xFFFF: OS 1.20 ROM (Vectors & reset code)
    else
    {
        return m->mem.os_rom[addr - 0xC000];
    }
}

void bbc_mem_write(bbc_machine_t *m, uint16_t addr, uint8_t val)
{
    // 0x0000 - 0x7FFF: Schrijven naar hoofd RAM
    if (addr < 0x8000)
    {
        m->mem.main_ram[addr] = val;

        /* Debug: track writes into the Mode 7 screen memory region so we can
         * see what MOS is actually storing there (e.g. fill byte on scroll). */
        if (addr >= 0x3C00 && addr < 0x4000)
        {
            m->dbg_mode7_last_addr = addr;
            m->dbg_mode7_last_val = val;
            m->dbg_mode7_last_pc = m->cpu ? vrEmu6502GetPC(m->cpu) : 0;
            m->dbg_mode7_last_x = m->cpu ? vrEmu6502GetX(m->cpu) : 0;
            m->dbg_mode7_last_y = m->cpu ? vrEmu6502GetY(m->cpu) : 0;
            m->dbg_mode7_zp_d8 = m->mem.main_ram[0x00D8];
            m->dbg_mode7_zp_d9 = m->mem.main_ram[0x00D9];
            m->dbg_mode7_zp_f0 = m->mem.main_ram[0x00F0];
            m->dbg_mode7_zp_88 = m->mem.main_ram[0x0088];
            m->dbg_mode7_zp_de = m->mem.main_ram[0x00DE];
            m->dbg_mode7_zp_df = m->mem.main_ram[0x00DF];
            m->dbg_mode7_write_count++;
        }
        else if (addr >= 0x7C00)
        {
            m->dbg_mode7_last_addr = addr;
            m->dbg_mode7_last_val = val;
            m->dbg_mode7_last_pc = m->cpu ? vrEmu6502GetPC(m->cpu) : 0;
            m->dbg_mode7_last_x = m->cpu ? vrEmu6502GetX(m->cpu) : 0;
            m->dbg_mode7_last_y = m->cpu ? vrEmu6502GetY(m->cpu) : 0;
            m->dbg_mode7_zp_d8 = m->mem.main_ram[0x00D8];
            m->dbg_mode7_zp_d9 = m->mem.main_ram[0x00D9];
            m->dbg_mode7_zp_f0 = m->mem.main_ram[0x00F0];
            m->dbg_mode7_zp_88 = m->mem.main_ram[0x0088];
            m->dbg_mode7_zp_de = m->mem.main_ram[0x00DE];
            m->dbg_mode7_zp_df = m->mem.main_ram[0x00DF];
            m->dbg_mode7_write_count++;
        }
    }
    // 0x8000 - 0xBFFF: Sideways RAM ondersteuning
    else if (addr < 0xC000)
    {
        uint8_t bank = m->mem.current_bank & 0x0F;
        if (m->mem.bank_is_ram[bank])
        {
            m->mem.sideways_banks[bank][addr - 0x8000] = val;
        }
    }
    // 0xFE00 - 0xFEFF: Hardware registers (SHEILA)
    else if (addr >= 0xFE00 && addr < 0xFF00)
    {
        bbc_io_timing(m);
        bbc_io_write(m, addr, val);
    }
}

// ---------------------------------------------------------------------------
// I/O Adresdecodering (SHEILA)
// ---------------------------------------------------------------------------
static uint8_t bbc_io_read(bbc_machine_t *m, uint16_t addr)
{
    // $FE00 - $FE01: CRTC 6845
    if ((addr & 0xFFF8) == 0xFE00)
    {
        return bbc_video_crtc_read(&m->video, addr & 1);
    }

    // $FE40 - $FE5F: System VIA
    if ((addr & 0xFFE0) == 0xFE40)
    {
        return bbc_sysvia_read(&m->sysvia, addr & 0x0F);
    }

    // $FE60 - $FE7F: User VIA
    if ((addr & 0xFFE0) == 0xFE60)
    {
        return bbc_uservia_read(&m->uservia, addr & 0x0F);
    }

    // $FE80 - $FE87: Acorn 1770 FDC interface
    if ((addr & 0xFFF8) == 0xFE80)
    {
        if (!(addr & 0x04))
            return m->fdc_latch;

        uint8_t reg = addr & 0x03;
        if (reg == 0)
        {
            uint8_t status = wd1770_read(&m->fdc, 0);
            return (status & 0x7F) | (m->fdc_drq_state ? 0 : 0x80);
        }
        if (reg == 3)
        {
            m->fdc_drq_state = false;
            return wd1770_read(&m->fdc, 3);
        }
        return wd1770_read(&m->fdc, reg);
    }

    return 0xFF;
}

static void bbc_io_write(bbc_machine_t *m, uint16_t addr, uint8_t val)
{
    // $FE00 - $FE01: CRTC 6845
    if ((addr & 0xFFF8) == 0xFE00)
    {
        bbc_video_crtc_write(&m->video, addr & 1, val);
        return;
    }

    // $FE20 - $FE23: Video ULA
    if ((addr & 0xFFFC) == 0xFE20)
    {
        bbc_video_vidproc_write(&m->video, addr & 1, val);
        return;
    }

    // $FE30 - $FE33: ROMSEL (Selecteert de actieve Sideways ROM/RAM bank)
    if ((addr & 0xFFFC) == 0xFE30)
    {
        m->mem.current_bank = val & 0x0F;
        return;
    }

    // $FE40 - $FE5F: System VIA
    if ((addr & 0xFFE0) == 0xFE40)
    {
        bbc_sysvia_write(&m->sysvia, addr & 0x0F, val);
        return;
    }

    // $FE60 - $FE7F: User VIA
    if ((addr & 0xFFE0) == 0xFE60)
    {
        bbc_uservia_write(&m->uservia, addr & 0x0F, val);
        return;
    }

    // $FE80 - $FE87: Acorn 1770 FDC Registers
    if ((addr & 0xFFF8) == 0xFE80)
    {
        if (!(addr & 0x04))
        {
            m->fdc_latch = val;
            wd1770_select(&m->fdc, (val >> 1) & 1, (val >> 2) & 1, (val >> 3) & 1);
            return;
        }

        uint8_t reg = addr & 0x03;
        if (reg == 3)
            m->fdc_drq_state = false;
        wd1770_write(&m->fdc, reg, val);
        return;
    }
}