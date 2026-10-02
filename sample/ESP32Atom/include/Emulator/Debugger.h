#pragma once

#include <stdint.h>
#include <stddef.h>
#include "PlatformTypes.h"

class CAtomEmulator;

class CAtomDebugger
{
public:
    CAtomDebugger(CAtomEmulator *pEmulator = nullptr);
    ~CAtomDebugger();

    void SetEmulator(CAtomEmulator *pEmulator);
    CAtomEmulator *GetEmulator(void) const;

    void SetDebug(bool bEnable);
    bool IsDebug(void) const;
    void Attach(CAtomEmulator *pEmulator) { SetEmulator(pEmulator); }
    void SetEnabled(bool enabled) { SetDebug(enabled); }
    bool IsBreakRequested(void) const { return m_bDebugOnBrk; }
    void ClearBreak(void) { m_bDebugOnBrk = false; }
    void Break(void) { m_bDebugOnBrk = true; }

    unsigned Disassemble(uint16_t addr, char *pOutBuf, size_t nBufSize);
    
    void TraceInstruction(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t s, uint8_t status);
    void DoDebugger(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t s, uint8_t status);
    
    void DebugRead(uint16_t addr, uint8_t val);
    void DebugWrite(uint16_t addr, uint8_t val);

private:
    CAtomEmulator *m_pEmulator;
    bool           m_bDebugOn;
    bool           m_bDebugOnBrk;

    int m_Breakpoints[8];
    int m_BreakRead[8];
    int m_BreakWrite[8];
    int m_WatchRead[8];
    int m_WatchWrite[8];

    static const char s_OpcodeNames[256][6];
    static const int  s_OpcodeAddrModes[256];
};