#pragma once

#include <stdint.h>
#include <stddef.h>
#include <circle/types.h>

class CAtomEmulator;

class CDebugger
{
public:
    CDebugger(CAtomEmulator *pEmulator = nullptr);
    ~CDebugger();

    void SetEmulator(CAtomEmulator *pEmulator);
    CAtomEmulator *GetEmulator(void) const;

    void SetDebug(bool bEnable);
    bool IsDebug(void) const;

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