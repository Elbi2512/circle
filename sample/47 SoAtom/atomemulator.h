#pragma once
// #define debugger

#include <stdint.h>
#include <stdlib.h>
#include <circle/types.h>

#include "pia8255.h"
#include "via6522.h"
#include "AtomMC.h"
#include "AtomVideo.h"
#ifdef debugger
#include "debugger.h"
#endif
// ---------------------------------------------------------------------------
// Geheugen- en ROM-constanten
// ---------------------------------------------------------------------------
#define ROM_MEM_SIZE        0xC000
#define ROM_SIZE_ATOM       0x1000
#define RAM_ROM_SIZE        0x20000
#define ROM_OFS_RAMROM      0x0C000

#define ROM_OFS_UTILITY     0x0000
#define ROM_OFS_ABASIC      0x1000
#define ROM_OFS_AFLOAT      0x2000
#define ROM_OFS_DOSROM      0x3000
#define ROM_OFS_AKERNEL     0x4000

#define RAM_ROM_ROMS        0x08

// Atom RAMROM mappings
#define ROM_OFS_RR_UTILITY  (ROM_OFS_RAMROM + 0x00000)
#define ROM_OFS_RR_ABASIC1  (ROM_OFS_RAMROM + 0x10000)
#define ROM_OFS_RR_AFLOAT1  (ROM_OFS_RAMROM + 0x11000)
#define ROM_OFS_RR_DOSROM1  (ROM_OFS_RAMROM + 0x12000)
#define ROM_OFS_RR_AKERNEL1 (ROM_OFS_RAMROM + 0x13000)

#define ROM_OFS_RR_ABASIC0  (ROM_OFS_RAMROM + 0x14000)
#define ROM_OFS_RR_AFLOAT0  (ROM_OFS_RAMROM + 0x15000)
#define ROM_OFS_RR_DOSROM0  (ROM_OFS_RAMROM + 0x16000)
#define ROM_OFS_RR_AKERNEL0 (ROM_OFS_RAMROM + 0x17000)

#define RAMROM_FLAG_EXTRAM   0x01
#define RAMROM_FLAG_BLKA_RAM 0x02
#define RAMROM_FLAG_DISKROM  0x04
#define RAMROM_FLAG_BBCMODE  0x08

#define RR_BLKA_enabled() (!RR_bit_set(RAMROM_FLAG_BLKA_RAM) && RR_bit_set(RAMROM_FLAG_DISKROM))

class CTextConsole;
class CKernel;
extern volatile bool g_bTrueFullscreen;

class CAtomEmulator
{
public:
    CAtomEmulator(void);
    ~CAtomEmulator();

    bool Initialize(unsigned nCoreId = 0);
    void Reset();
    void Update();
    void Exec6502(int linenum, int cpl);
    void PollTime(int c);

    // Geheugen- en I/O-toegang
    uint8_t ReadMem(uint16_t addr);
    void WriteMem(uint16_t addr, uint8_t val);
    bool LoadROM(const char *pName, int Size, int Offset);
    bool LoadROMs();

    // Toetsenbord-interface
    void KeyDown(uint8_t scancode);
    void KeyUp(uint8_t scancode);
    void ClearKeyboard();
    void SetShiftState(bool bPressed) { m_Pia.SetShift(bPressed); }
    void SetCtrlState(bool bPressed)  { m_Pia.SetCtrl(bPressed); }
    void SetReptState(bool bPressed)  { m_Pia.SetRept(bPressed); }

    // Display & Video Status
    bool IsFieldSyncActive(void) const;
    void SetPAL(bool bPAL)            { m_bPAL = bPAL; }
    uint8_t GetGfxMode() const        { return m_nGfxMode; }
    bool HasVRAMChanged() const       { return m_bVRAMChanged; }
    void ResetVRAMChanged()           { m_bVRAMChanged = false; }
    void ForceVRAMChanged()           { m_bVRAMChanged = true; }
    void ForceFullRedraw()
    {
        m_bFullRedrawNeeded = true;
        m_bVRAMChanged = true;
        memset(m_LastTextVRAM, 0xFF, sizeof(m_LastTextVRAM));
    }

    // Turbo Modus (F11)
    void ToggleTurbo(void)            { m_bTurbo = !m_bTurbo; }
    bool IsTurbo(void) const          { return m_bTurbo; }
    void SetTurbo(bool bTurbo)        { m_bTurbo = bTurbo; }

    // Pointers & Componenten
    unsigned GetCoreID() const        { return m_nCoreId; }
    uint8_t *GetRAM()                 { return m_pRam; }
    const uint8_t *GetVRAMPointer() const { return m_Video.GetVRAM(); }

    void SetKernel(CKernel *pKernel)  { m_pKernel = pKernel; }
    void SetConsole(CTextConsole *pConsole) { m_pConsole = pConsole; }

    CPia8255   &GetPIA()   { return m_Pia; }
    CVia6522   &GetVIA()   { return m_Via; }
    CAtomMC    &GetMMC()   { return m_Mmc; }
    CAtomVideo &GetVIDEO() { return m_Video; }

private:
    void InitKeymap();
    void InitMem();
    void SetDosRomPtr();
    void SetRRPtrs();
    void ResetROM();
    int  RamEnabled(uint16_t addr);
    inline uint16_t GetSW();

    void PollSound() {}
    uint8_t SIDRead(uint16_t addr) { (void)addr; return 0xFF; }
    void    SIDWrite(uint16_t addr, uint8_t val) { (void)addr; (void)val; }

    // -----------------------------------------------------------------------
    // Member variabelen (Volgorde strikt gelijk aan constructor-lijst!)
    // -----------------------------------------------------------------------
    
    // Basis configuratie & Context
    unsigned      m_nCoreId{0};
    bool          m_bInitialized{false};
    volatile bool m_bTurbo{false};
    bool          m_bPAL{true};
    CKernel      *m_pKernel{nullptr};
    CTextConsole *m_pConsole{nullptr};

    // Geheugenbuffers
    uint8_t *m_pRam{nullptr};
    uint8_t *m_pRom{nullptr};
    uint8_t *m_pUtilityPtr{nullptr};
    uint8_t *m_pABasicPtr{nullptr};
    uint8_t *m_pAFloatPtr{nullptr};
    uint8_t *m_pDosRomPtr{nullptr};
    uint8_t *m_pAKernelPtr{nullptr};

    // 6502 CPU Registers & Status
    uint8_t  m_a{0}, m_x{0}, m_y{0}, m_s{0xFF};
    uint16_t m_pc{0x0000};

    struct PS
    {
        uint8_t n : 1;
        uint8_t v : 1;
        uint8_t d : 1;
        uint8_t i : 1;
        uint8_t z : 1;
        uint8_t c : 1;
    } m_p;

    // Interrupts & Executie-timing
    int m_skipint{0};
    int m_nmi{0};
    int m_nmilock{0};
    int m_interrupt{0};
    int m_timetolive{0};
    int m_oldnmi{0};
    int m_fskipcount{0};
    int m_skipint2{0};
    int m_oldnmi2{0};
    int m_tapeon{0};
    int m_totcyc{0};
    int m_tapecyc{635};
    int m_cycles{0};
    int m_output{0};
    int m_ins{0};
    int m_lns{0};

    // RAMROM / Systeembank registers
    uint8_t m_RR_bankreg{0};
    uint8_t m_RR_enables{0};
    uint8_t m_RR_jumpers{0};
    uint8_t m_ramrom_enable{1};
    uint8_t m_main_ramflag{5};

    // Video & Display status
    u8       m_nGfxMode{0};
    u8       m_nCss{0};
    uint8_t  m_nRtcIndex{0};
    int      m_debugon{1};
    int      m_snow{0};
    uint16_t m_vid_top{0xA000};
    int      m_sndatomsid{0};
    bool     m_bVRAMChanged{true};
    bool     m_bFullRedrawNeeded{true};

    // Caches & Buffers
    uint8_t m_fetcheddat[32];
    uint8_t m_fetchc[0x10000];
    uint8_t m_readc[0x10000];
    uint8_t m_writec[0x10000];
    uint8_t m_LastTextVRAM[512];

    // Hardware componenten
    CPia8255   m_Pia;
    CVia6522   m_Via;
    CAtomMC    m_Mmc;
    CAtomVideo m_Video;
    #ifdef debugger
    CDebugger  m_Debugger;
#endif
    // Toetsenbord mapping
    uint8_t m_aKeyLookup[128];
    uint8_t m_aKeyState[128];
};