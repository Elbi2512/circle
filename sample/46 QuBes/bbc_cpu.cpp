#include "bbc_machine.h"
#include "bbc_cpu.h"
#include "vrEmu6502.h"

extern uint8_t bbc_mem_read(bbc_machine_t *m, uint16_t addr);
extern void bbc_mem_write(bbc_machine_t *m, uint16_t addr, uint8_t value);

static bbc_machine_t *s_machine;

static uint8_t cpu_read(uint16_t addr, bool is_debug)
{
    (void)is_debug;
    return s_machine ? bbc_mem_read(s_machine, addr) : 0xFF;
}

static void cpu_write(uint16_t addr, uint8_t value)
{
    if (s_machine)
        bbc_mem_write(s_machine, addr, value);
}

void bbc_cpu_init(bbc_machine_t *m)
{
    s_machine = m;
    m->cpu = vrEmu6502New(CPU_6502, cpu_read, cpu_write);
}

void bbc_cpu_reset(bbc_machine_t *m)
{
    s_machine = m;
    if (m->cpu)
        vrEmu6502Reset(m->cpu);
}

void bbc_cpu_destroy(bbc_machine_t *m)
{
    if (m && m->cpu)
    {
        vrEmu6502Destroy(m->cpu);
        m->cpu = 0;
    }
    if (s_machine == m)
        s_machine = 0;
}

void bbc_cpu_nmi(bbc_machine_t *m)
{
    s_machine = m;
    if (m->cpu)
        *vrEmu6502Nmi(m->cpu) = IntRequested;
}

void bbc_cpu_irq(bbc_machine_t *m)
{
    s_machine = m;
    if (m->cpu)
        *vrEmu6502Int(m->cpu) = IntRequested;
}

void bbc_cpu_clear_irq(bbc_machine_t *m)
{
    s_machine = m;
    if (m->cpu)
        *vrEmu6502Int(m->cpu) = IntCleared;
}

int bbc_cpu_step(bbc_machine_t *m)
{
    s_machine = m;
    return m->cpu ? vrEmu6502InstCycle(m->cpu) : 0;
}

int bbc_machine_step(bbc_machine_t *m)
{
    if (m->nmi_pending)
    {
        m->nmi_pending = false;
        bbc_cpu_nmi(m);
    }

    if (m->irq_pending)
        bbc_cpu_irq(m);
    else
        bbc_cpu_clear_irq(m);

    m->io_cycles_pending = 0;
    m->io_accesses = 0;
    int cycles = bbc_cpu_step(m);
    cycles += (int)m->io_cycles_pending;
    m->io_cycles_pending = 0;
    bbc_sysvia_tick(&m->sysvia, cycles);
    bbc_uservia_tick(&m->uservia, cycles);
    wd1770_tick(&m->fdc, cycles);
    int crtc_divider = m->video.ula.crtc_2mhz ? 1 : 2;
    m->crtc_acc += cycles;
    while (m->crtc_acc >= crtc_divider)
    {
        bbc_video_tick(&m->video);
        m->crtc_acc -= crtc_divider;
    }
    m->total_cycles += cycles;
    return cycles;
}
