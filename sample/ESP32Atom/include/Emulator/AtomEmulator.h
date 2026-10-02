#pragma once

#include <stddef.h>
#include <stdint.h>
#include "PlatformTypes.h"
#include "Pia8255.h"
#include "Via6522.h"
#include "AtomVideo.h"
#include "AtomMC.h"
#include "Debugger.h"

static constexpr size_t ROM_MEM_SIZE = 0xC000;
static constexpr size_t ROM_SIZE_ATOM = 0x1000;
static constexpr size_t RAM_ROM_SIZE = 0x20000;
static constexpr size_t ROM_OFS_RAMROM = 0x0C000;
static constexpr size_t ROM_OFS_UTILITY = 0x0000;
static constexpr size_t ROM_OFS_ABASIC = 0x1000;
static constexpr size_t ROM_OFS_AFLOAT = 0x2000;
static constexpr size_t ROM_OFS_DOSROM = 0x3000;
static constexpr size_t ROM_OFS_AKERNEL = 0x4000;
static constexpr size_t ROM_OFS_RR_UTILITY = ROM_OFS_RAMROM;
static constexpr size_t ROM_OFS_RR_ABASIC1 = ROM_OFS_RAMROM + 0x10000;
static constexpr size_t ROM_OFS_RR_AFLOAT1 = ROM_OFS_RAMROM + 0x11000;
static constexpr size_t ROM_OFS_RR_DOSROM1 = ROM_OFS_RAMROM + 0x12000;
static constexpr size_t ROM_OFS_RR_AKERNEL1 = ROM_OFS_RAMROM + 0x13000;
static constexpr size_t ROM_OFS_RR_ABASIC0 = ROM_OFS_RAMROM + 0x14000;
static constexpr size_t ROM_OFS_RR_AFLOAT0 = ROM_OFS_RAMROM + 0x15000;
static constexpr size_t ROM_OFS_RR_DOSROM0 = ROM_OFS_RAMROM + 0x16000;
static constexpr size_t ROM_OFS_RR_AKERNEL0 = ROM_OFS_RAMROM + 0x17000;
static constexpr EmulatorByte RAMROM_FLAG_EXTRAM = 0x01;
static constexpr EmulatorByte RAMROM_FLAG_DISKROM = 0x04;
static constexpr EmulatorByte RAMROM_FLAG_BLKA_RAM = 0x02;

class CAtomEmulator
{
public:
    static constexpr size_t RamSize = 0x10000;
    static constexpr size_t RomMemorySize = 0xC000;
    static constexpr size_t RamRomSize = 0x20000;

    CAtomEmulator();
    ~CAtomEmulator();
    bool Initialize(unsigned coreId = 0);
    void Reset();
    void Exec6502(int cycleBudget, int cyclesPerLine = 1);
    void PollTime(int cycles);
    void Update(int cycles = 0);
    EmulatorByte ReadMem(EmulatorWord address);
    void WriteMem(EmulatorWord address, EmulatorByte value);
    bool LoadROM(const char *path, int size, int offset);
    void KeyDown(EmulatorByte key) { m_Pia.KeyDown(key); }
    void KeyUp(EmulatorByte key) { m_Pia.KeyUp(key); }
    void ClearKeyboard() { m_Pia.ClearKeyboard(); }
    void SetPAL(bool pal) { m_pal = pal; }
    EmulatorByte GetGfxMode() const { return m_nGfxMode; }
    EmulatorByte GetCss() const { return m_nCss; }
    EmulatorByte *GetRAM() { return m_pRam; }
    const EmulatorByte *GetVRAM() const { return m_Video.GetVRAM(); }
    CPia8255 &GetPIA() { return m_Pia; }
    CVia6522 &GetVIA() { return m_Via; }
    CAtomVideo &GetVideo() { return m_Video; }
    CAtomMC &GetMMC() { return m_Mmc; }
    CAtomDebugger &GetDebugger() { return m_Debugger; }
    unsigned GetCoreID() const { return m_nCoreId; }
    bool IsFieldSyncActive() const;
    bool HasVRAMChanged() const { return m_bVRAMChanged; }
    void ResetVRAMChanged() { m_bVRAMChanged = false; }
    void ForceVRAMChanged() { m_bVRAMChanged = true; }
    void SetShiftState(bool pressed) { m_Pia.SetShift(pressed); }
    void SetCtrlState(bool pressed) { m_Pia.SetCtrl(pressed); }
    void SetReptState(bool pressed) { m_Pia.SetRept(pressed); }

private:
    void InitKeymap();
    void InitMem();
    bool LoadROMs();
    void SetDosRomPtr();
    void SetRRPtrs();
    void ResetROM();
    int RamEnabled(EmulatorWord address);
    EmulatorWord GetSW();

    unsigned m_nCoreId;
    bool m_bInitialized;
    bool m_bTurbo;
    bool m_pal;
    EmulatorByte *m_pRam;
    EmulatorByte *m_pRom;
    EmulatorByte *m_pUtilityPtr;
    EmulatorByte *m_pABasicPtr;
    EmulatorByte *m_pAFloatPtr;
    EmulatorByte *m_pDosRomPtr;
    EmulatorByte *m_pAKernelPtr;
    EmulatorByte m_a, m_x, m_y, m_s;
    EmulatorWord m_pc;
    struct ProcessorStatus { uint8_t n, v, d, i, z, c; } m_p;
    int m_skipint, m_nmi, m_nmilock, m_interrupt, m_timetolive, m_oldnmi;
    int m_fskipcount, m_skipint2, m_oldnmi2, m_tapeon, m_totcyc, m_tapecyc;
    int m_cycles, m_output, m_ins, m_lns;
    EmulatorByte m_RR_bankreg, m_RR_enables, m_RR_jumpers, m_ramrom_enable, m_main_ramflag;
    EmulatorByte m_nGfxMode, m_nCss, m_nRtcIndex;
    int m_debugon, m_snow;
    EmulatorWord m_vid_top;
    int m_sndatomsid;
    bool m_bVRAMChanged, m_bFullRedrawNeeded;
    EmulatorByte m_fetcheddat[32];
    EmulatorByte m_fetchc[0x10000], m_readc[0x10000], m_writec[0x10000];
    EmulatorByte m_LastTextVRAM[512];
    CPia8255 m_Pia;
    CVia6522 m_Via;
    CAtomMC m_Mmc;
    CAtomVideo m_Video;
    CAtomDebugger m_Debugger;
    EmulatorByte m_aKeyLookup[128], m_aKeyState[128];
};
