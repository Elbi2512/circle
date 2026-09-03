#ifndef _DEBUGGER_H_
#define _DEBUGGER_H_

#include <stdint.h>
#include <stddef.h>
#include <circle/types.h>

class CAtomEmulator;

class CDebugger
{
public:
    CDebugger(CAtomEmulator *pEmulator = nullptr);
    ~CDebugger();

    void SetEmulator(CAtomEmulator *pEmulator) { m_pEmulator = pEmulator; }
    CAtomEmulator* GetEmulator(void) const     { return m_pEmulator; }
   /*    CAtomEmulator *GetEmulator(unsigned nCore)
    {
        if (nCore < 4)
        {
            return &m_AtomEmulator[nCore];
        }
        return &m_AtomEmulator[0];
    } */

    unsigned Disassemble(uint16_t addr, char *pOutBuf, size_t nBufSize);
    
    // Zorg dat deze typen exact matchen met debugger.cpp:
    void TraceInstruction(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t s, uint8_t status);
    void DoDebugger(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t s, uint8_t status);
    
    void DebugRead(uint16_t addr, uint8_t val);
    void DebugWrite(uint16_t addr, uint8_t val);

    void SetDebug(bool bEnable) { m_bDebugOn = bEnable; }
    bool IsDebug(void) const    { return m_bDebugOn; }

private:
    static const char s_OpcodeNames[256][6];
    static const int  s_OpcodeAddrModes[256];

    bool m_bDebugOn;
    bool m_bDebugOnBrk;
    CAtomEmulator *m_pEmulator;

    int m_Breakpoints[8];
    int m_BreakRead[8];
    int m_BreakWrite[8];
    int m_WatchRead[8];
    int m_WatchWrite[8];
};

#endif