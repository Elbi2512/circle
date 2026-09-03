/*
        Roms.h, defines for handling roms of Atomulator.

        2012-06-01, P.Harvey-Smith.
 */

// Size of ROM memory, and rom blocks

#pragma once

#include <stdint.h>
#include <circle/types.h>
#include <stdlib.h>

#include "atomemulator.h"
#include "pia8255.h"
#include "via6522.h"
// #include "fdc8271.h"
// #include "fdc1770.h"
#include "AtomMC.h"
#include "AtomVideo.h"
#include "debugger.h"
#include "textconsole.h"

// SP7 BBC MODE PATCH

#define ROM_MEM_SIZE 0xC000

// END SP7 BBC MODE PATCH

#define ROM_SIZE_ATOM 0x1000

// ROM offsets within rom memory
#define ROM_OFS_UTILITY 0x0000
#define ROM_OFS_ABASIC 0x1000
#define ROM_OFS_AFLOAT 0x2000
#define ROM_OFS_DOSROM 0x3000
#define ROM_OFS_AKERNEL 0x4000

// As of the 2014 Hackfest, the rom for the RAM ROM board is a 128K rom laid out as follows
//
// 0x00000 - Atom #A000 Bank 0
// 0x01000 - Atom #A000 Bank 1
// 0x02000 - Atom #A000 Bank 2
// 0x03000 - Atom #A000 Bank 3
// 0x04000 - Atom #A000 Bank 4
// 0x05000 - Atom #A000 Bank 5
// 0x06000 - Atom #A000 Bank 6
// 0x07000 - Atom #A000 Bank 7
// 0x08000 - BBC #6000 Bank 0 (ExtROM1)
// 0x09000 - BBC #6000 Bank 1 (ExtROM1)
// 0x0A000 - BBC #6000 Bank 2 (ExtROM1)
// 0x0B000 - BBC #6000 Bank 3 (ExtROM1)
// 0x0C000 - BBC #6000 Bank 4 (ExtROM1)
// 0x0D000 - BBC #6000 Bank 5 (ExtROM1)
// 0x0E000 - BBC #6000 Bank 6 (ExtROM1)
// 0x0F000 - BBC #6000 Bank 7 (ExtROM1)
// 0x10000 - Atom Basic (DskRomEn=1)
// 0x11000 - Atom FP (DskRomEn=1)
// 0x12000 - Atom MMC (DskRomEn=1)
// 0x13000 - Atom Kernel (DskRomEn=1)
// 0x14000 - Atom Basic (DskRomEn=0)
// 0x15000 - Atom FP (DskRomEn=0)
// 0x16000 - unused
// 0x17000 - Atom Kernel (DskRomEn=0)
// 0x18000 - unused
// 0x19000 - BBC #7000 (ExtROM2)
// 0x1A000 - BBC Basic 1/4
// 0x1B000 - unused
// 0x1C000 - BBC Basic 2/4
// 0x1D000 - BBC Basic 3/4
// 0x1E000 - BBC Basic 4/4
// 0x1F000 - BBC MOS 3.0
//
// See: http://stardot.org.uk/forums/viewtopic.php?f=44&t=8350&p=92948#p92948

#define RAM_ROM_SIZE 0x20000

// This is the address in the Ram array that ramrom.rom is loaded to
// So all the above addresses need offseting by this amount

#define ROM_OFS_RAMROM 0x0C000

// Number of pages ROMs, same in both Atom and BBC Mode
#define RAM_ROM_ROMS 0x08

// Atom Mode
#define ROM_OFS_RR_UTILITY (ROM_OFS_RAMROM + 0x00000)

#define ROM_OFS_RR_ABASIC1 (ROM_OFS_RAMROM + 0x10000)
#define ROM_OFS_RR_AFLOAT1 (ROM_OFS_RAMROM + 0x11000)
#define ROM_OFS_RR_DOSROM1 (ROM_OFS_RAMROM + 0x12000)
#define ROM_OFS_RR_AKERNEL1 (ROM_OFS_RAMROM + 0x13000)

#define ROM_OFS_RR_ABASIC0 (ROM_OFS_RAMROM + 0x14000)
#define ROM_OFS_RR_AFLOAT0 (ROM_OFS_RAMROM + 0x15000)
#define ROM_OFS_RR_DOSROM0 (ROM_OFS_RAMROM + 0x16000)
#define ROM_OFS_RR_AKERNEL0 (ROM_OFS_RAMROM + 0x17000)

// if emuspeed is faster than this then ramrom returns fast speed.
#define RAMROM_EMU_FAST 6

// RAMROM bitmaps for 0xBFFD/0xBFFE
#define RAMROM_FLAG_EXTRAM 0x01
#define RAMROM_FLAG_BLKA_RAM 0x02
#define RAMROM_FLAG_DISKROM 0x04
#define RAMROM_FLAG_BBCMODE 0x08

// #define RR_bit_set(bit)			((RR_enables ^ RR_jumpers) & bit)

// Only enable Block A RAM if requested, and if DISKROM is not enabled
// Currently there is no GUI option to control BLKA, so the normal value of this bit is 0
// This can be toggled by setting bit 1 of ?#BFFE
// I have actually inverted it's meaning, so the default behaviour is to enable the BLKA RAM when DISROM is disabled
#define RR_BLKA_enabled() (!RR_bit_set(RAMROM_FLAG_BLKA_RAM) && RR_bit_set(RAMROM_FLAG_DISKROM))

/*

// ROM Offsets en Sizes (Stubs voor de compiler)
#ifndef ROM_SIZE_ATOM
#define ROM_SIZE_ATOM 0x1000
#define RAM_ROM_SIZE 0x20000
// #define ROM_SIZE_GDOS2015 0x20000
#define ROM_MEM_SIZE 0x10000

#define ROM_OFS_AKERNEL 0x0000
#define ROM_OFS_DOSROM  0x1000
#define ROM_OFS_AFLOAT  0x2000
#define ROM_OFS_ABASIC  0x3000
#define ROM_OFS_UTILITY 0x4000
#define ROM_OFS_RAMROM  0x5000

#define ROM_OFS_RR_ABASIC1  0xC000
#define ROM_OFS_RR_AFLOAT1  0xD000
#define ROM_OFS_RR_DOSROM1  0xE000
#define ROM_OFS_RR_AKERNEL1 0xF000
#define ROM_OFS_RR_ABASIC0 0x10000
#define ROM_OFS_RR_AFLOAT0 0x11000
#define ROM_OFS_RR_AKERNEL0 0x12000
#define ROM_OFS_RR_UTILITY 0x13000

#define RAMROM_FLAG_BBCMODE 0x01
#define RAMROM_FLAG_DISKROM 0x02
#define RAMROM_FLAG_EXTRAM 0x04

// #define WDBASE 0xBC00
// #define CTRLREG 0xBC0F
*/
// #endif

class CTextConsole;

class CKernel; // Forward declaration bovenaan indien nodig
static volatile bool g_bTrueFullscreen = false;
// Gedeelde buffer van 4 pages (4 * 256 bytes = 1024 bytes)
// We alignen dit op een page boundary voor netheid.

class CAtomEmulator
{
public:
    CAtomEmulator(void);
    ~CAtomEmulator();
    void SetConsole(CTextConsole *pConsole) { m_pConsole = pConsole; }
    void SetKernel(CKernel *pKernel);
    unsigned GetCoreID() const { return m_nCoreId; }
    void ForceVRAMChanged() { m_bVRAMChanged = true; }

    bool Initialize(unsigned nCoreId = 0);
    bool IsFieldSyncActive(void) const;
    void SetShiftState(bool bPressed) { m_Pia.SetShift(bPressed); }
    void SetCtrlState(bool bPressed) { m_Pia.SetCtrl(bPressed); }
    void SetReptState(bool bPressed) { m_Pia.SetRept(bPressed); }

    void SetPAL(bool bPAL) { m_bPAL = bPAL; }

    // Toetsenbord koppeling voor de actieve core
    void KeyDown(uint8_t scancode);
    void KeyUp(uint8_t scancode);
     
    void Reset();
    void Update();
    void Exec6502(int linenum, int cpl);
    void PollTime(int c);

    void LoadROM(const char *Name, int Size, int Offset);
    void LoadROMs();
    uint8_t *GetRam() { return m_pRam; }
    // Directe VRAM toegang voor rendering
    const uint8_t *GetVRAMPointer() const { return m_Video.GetVRAM(); }

    uint8_t ReadMem(uint16_t addr);
    void WriteMem(uint16_t addr, uint8_t val);
    //     void SetDosRomPtr()
    // {
    //     m_pDosRomPtr = &m_pRom[0xA000]; // Of jouw specifieke offset voor de DOS ROM
    // }

    // Getters voor de randapparatuur
    //  CFdc8271& GetFDC() { return m_Fdc; }
    //  CFdc1770& GetFdc1770() { return m_Fdc1770; }

    // Directe toegang tot de onderliggende componenten
    // CAtomEmulator& GetCPU()     { return m_Emulator; }
    CPia8255 &GetPIA() { return m_Pia; }
    CVia6522 &GetVIA() { return m_Via; }
    CAtomMC &GetMMC() { return m_Mmc; }
    CAtomVideo &GetVIDEO() { return m_Video; }
    void ClearKeyboard();

    bool HasVRAMChanged(); // <-- Moet hier tussen staan!
    void ResetVRAMChanged();

private:
    CTextConsole *m_pConsole = nullptr;
    CKernel *m_pKernel;

    bool m_bPrinterActive = false;
    void InitKeymap();
    void InitMem();
    void SetDosRomPtr();
    void SetRRPtrs();
    void ResetROM();
    int RamEnabled(uint16_t addr);
    inline uint16_t GetSW();
    bool m_bVRAMChanged;
    void RPCLog(const char *format, ...);
    // Stubs/Helpers voor hardware
    // Stubs voor optionele / niet-geïmplementeerde randapparatuur

    void PollSound() {}
    //   void DoDebugger(int linenum) {}

    uint8_t SIDRead(uint16_t addr)
    {
        (void)addr;
        return 0xFF;
    }
    void SIDWrite(uint16_t addr, uint8_t val)
    {
        (void)addr;
        (void)val;
    }

    // Geheugen Buffers & Pointers
    uint8_t *m_pRam;
    uint8_t *m_pRom;
    bool m_bPAL = true; // Standaard 50 Hz (PAL)

    uint8_t *m_pUtilityPtr;
    uint8_t *m_pABasicPtr;
    uint8_t *m_pAFloatPtr;
    uint8_t *m_pDosRomPtr;
    uint8_t *m_pAKernelPtr;

    // 6502 Registers & Status
    uint8_t m_a, m_x, m_y, m_s;
    uint16_t m_pc;

    struct PS
    {
        uint8_t n : 1;
        uint8_t v : 1;
        uint8_t d : 1;
        uint8_t i : 1;
        uint8_t z : 1;
        uint8_t c : 1;
    } m_p;

    // Timers & Status
    int m_skipint, m_nmi, m_nmilock, m_interrupt, m_timetolive, m_oldnmi;
    int m_skipint2, m_oldnmi2;
    int m_tapeon;
    int m_totcyc;
    int m_tapecyc;
    int m_cycles;
    int m_output;
    int m_ins;
    int m_lns;

    int m_RR_bankreg;
    int m_RR_enables;
    int m_RR_jumpers;

    int m_ramrom_enable;
    int m_main_ramflag;
    // int m_fdc1770;
    // int m_GD_bank;
    int m_debugon = true;
    int m_snow;
    uint16_t m_vid_top;
    int m_sndatomsid;
    // int m_motoron;
    // int m_fdctime;
    // int m_disctime;
    // int m_motorspin;

    uint8_t m_fetcheddat[32];
    uint8_t m_fetchc[0x10000];
    uint8_t m_readc[0x10000];
    uint8_t m_writec[0x10000];

    // --- Geïnkapselde Randapparatuur (Slechts 1 exemplaar elk!) ---
    unsigned m_nCoreId;
    bool m_bInitialized;
    // int  m_nCurrentLine;  // Huidige scanline (0..261)
    u8 m_nGfxMode; // MC6847 videomodus (0..15)
    u8 m_nCss;     // Color Set Select (0 of 1)

    // Systeem-onderdelen
    // CAtomEmulator m_Emulator;
    CPia8255 m_Pia;
    CVia6522 m_Via;
    // CMc6847 m_Vdg;
    CAtomMC m_Mmc;
    CAtomVideo m_Video;
    CDebugger m_Debugger;

    // Toetsenbord-status
    uint8_t m_aKeyLookup[128];
    uint8_t m_aKeyState[128];
};