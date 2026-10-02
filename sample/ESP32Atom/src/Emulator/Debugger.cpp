#include "Emulator/Debugger.h"
#include "Emulator/AtomEmulator.h"
#include <Arduino.h>
#include <string.h>

enum AddressingModes
{
    IMP,
    IMPA,
    IMM,
    ZP,
    ZPX,
    ZPY,
    INDX,
    INDY,
    IND,
    ABS,
    ABSX,
    ABSY,
    IND16,
    IND1X,
    BRA
};

const char CAtomDebugger::s_OpcodeNames[256][6] =
    {
        /*00*/ "BRK", "ORA", "HLT", "SLO", "NOP", "ORA", "ASL", "SLO", "PHP", "ORA", "ASL", "ANC", "NOP", "ORA", "ASL", "SLO",
        /*10*/ "BPL", "ORA", "HLT", "SLO", "NOP", "ORA", "ASL", "SLO", "CLC", "ORA", "NOP", "SLO", "NOP", "ORA", "ASL", "SLO",
        /*20*/ "JSR", "AND", "HLT", "RLA", "NOP", "AND", "ROL", "RLA", "PLP", "AND", "ROL", "ANC", "BIT", "AND", "ROL", "RLA",
        /*30*/ "BMI", "AND", "HLT", "RLA", "NOP", "AND", "ROL", "RLA", "SEC", "AND", "NOP", "RLA", "NOP", "AND", "ROL", "RLA",
        /*40*/ "RTI", "EOR", "HLT", "SRE", "NOP", "EOR", "LSR", "SRE", "PHA", "EOR", "LSR", "ASR", "JMP", "EOR", "LSR", "SRE",
        /*50*/ "BVC", "EOR", "HLT", "SRE", "NOP", "EOR", "LSR", "SRE", "CLI", "EOR", "NOP", "SRE", "NOP", "EOR", "LSR", "SRE",
        /*60*/ "RTS", "ADC", "HLT", "RRA", "NOP", "ADC", "ROR", "RRA", "PLA", "ADC", "ROR", "ARR", "JMP", "ADC", "ROR", "RRA",
        /*70*/ "BVS", "ADC", "HLT", "RRA", "NOP", "ADC", "ROR", "RRA", "SEI", "ADC", "NOP", "RRA", "NOP", "ADC", "ROR", "RRA",
        /*80*/ "BRA", "STA", "NOP", "SAX", "STY", "STA", "STX", "SAX", "DEY", "NOP", "TXA", "ANE", "STY", "STA", "STX", "SAX",
        /*90*/ "BCC", "STA", "HLT", "SHA", "STY", "STA", "STX", "SAX", "TYA", "STA", "TXS", "SHS", "SHY", "STA", "SHX", "SHA",
        /*A0*/ "LDY", "LDA", "LDX", "LAX", "LDY", "LDA", "LDX", "LAX", "TAY", "LDA", "TAX", "LXA", "LDY", "LDA", "LDX", "LAX",
        /*B0*/ "BCS", "LDA", "HLT", "LAX", "LDY", "LDA", "LDX", "LAX", "CLV", "LDA", "TSX", "LAS", "LDY", "LDA", "LDX", "LAX",
        /*C0*/ "CPY", "CMP", "NOP", "DCP", "CPY", "CMP", "DEC", "DCP", "INY", "CMP", "DEX", "SBX", "CPY", "CMP", "DEC", "DCP",
        /*D0*/ "BNE", "CMP", "HLT", "DCP", "NOP", "CMP", "DEC", "DCP", "CLD", "CMP", "NOP", "DCP", "NOP", "CMP", "DEC", "DCP",
        /*E0*/ "CPX", "SBC", "NOP", "ISB", "CPX", "SBC", "INC", "ISB", "INX", "SBC", "NOP", "SBC", "CPX", "SBC", "INC", "ISB",
        /*F0*/ "BEQ", "SBC", "HLT", "ISB", "NOP", "SBC", "INC", "ISB", "SED", "SBC", "NOP", "ISB", "NOP", "SBC", "INC", "ISB"};

const int CAtomDebugger::s_OpcodeAddrModes[256] =
    {
        /*00*/ IMP, INDX, IMP, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMPA, IMM, ABS, ABS, ABS, ABS,
        /*10*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY, ABSX, ABSX, ABSX, ABSX,
        /*20*/ ABS, INDX, IMP, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMPA, IMM, ABS, ABS, ABS, ABS,
        /*30*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY, ABSX, ABSX, ABSX, ABSX,
        /*40*/ IMP, INDX, IMP, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMPA, IMM, ABS, ABS, ABS, ABS,
        /*50*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY, ABSX, ABSX, ABSX, ABSX,
        /*60*/ IMP, INDX, IMP, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMPA, IMM, IND16, ABS, ABS, ABS,
        /*70*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY, ABSX, ABSX, ABSX, ABSX,
        /*80*/ BRA, INDX, IMM, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
        /*90*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPY, ZPY, IMP, ABSY, IMP, ABSY, ABSX, ABSX, ABSX, ABSX,
        /*A0*/ IMM, INDX, IMM, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
        /*B0*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPY, ZPY, IMP, ABSY, IMP, ABSY, ABSX, ABSX, ABSY, ABSX,
        /*C0*/ IMM, INDX, IMM, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
        /*D0*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY, ABSX, ABSX, ABSX, ABSX,
        /*E0*/ IMM, INDX, IMM, INDX, ZP, ZP, ZP, ZP, IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
        /*F0*/ BRA, INDY, IMP, INDY, ZPX, ZPX, ZPX, ZPX, IMP, ABSY, IMP, ABSY, ABSX, ABSX, ABSX, ABSX};

CAtomDebugger::CAtomDebugger(CAtomEmulator *pEmulator)
    : m_pEmulator(pEmulator),
      m_bDebugOn(false),
      m_bDebugOnBrk(false)
{
    for (int i = 0; i < 8; i++)
    {
        m_Breakpoints[i] = -1;
        m_BreakRead[i] = -1;
        m_BreakWrite[i] = -1;
        m_WatchRead[i] = -1;
        m_WatchWrite[i] = -1;
    }
}

CAtomDebugger::~CAtomDebugger()
{
}

void CAtomDebugger::SetEmulator(CAtomEmulator *pEmulator)
{
    m_pEmulator = pEmulator;
}

CAtomEmulator *CAtomDebugger::GetEmulator(void) const
{
    return m_pEmulator;
}

void CAtomDebugger::SetDebug(bool bEnable)
{
    m_bDebugOn = bEnable;
}

bool CAtomDebugger::IsDebug(void) const
{
    return m_bDebugOn;
}

unsigned CAtomDebugger::Disassemble(uint16_t addr, char *pOutBuf, size_t nBufSize)
{
    if (!m_pEmulator || !pOutBuf || nBufSize == 0)
    {
        return 1;
    }

    uint8_t op = m_pEmulator->ReadMem(addr);
    uint8_t p1 = m_pEmulator->ReadMem(addr + 1);
    uint8_t p2 = m_pEmulator->ReadMem(addr + 2);

    const char *name = s_OpcodeNames[op];
    int mode = s_OpcodeAddrModes[op];
    unsigned nBytes = 1;

    char strFormat[64];

    switch (mode)
    {
    case IMP:
        snprintf(strFormat, sizeof(strFormat), "%-4s", name);
        nBytes = 1;
        break;
    case IMPA:
        snprintf(strFormat, sizeof(strFormat), "%-4s A", name);
        nBytes = 1;
        break;
    case IMM:
        snprintf(strFormat, sizeof(strFormat), "%-4s #$%02X", name, p1);
        nBytes = 2;
        break;
    case ZP:
        snprintf(strFormat, sizeof(strFormat), "%-4s $%02X", name, p1);
        nBytes = 2;
        break;
    case ZPX:
        snprintf(strFormat, sizeof(strFormat), "%-4s $%02X,X", name, p1);
        nBytes = 2;
        break;
    case ZPY:
        snprintf(strFormat, sizeof(strFormat), "%-4s $%02X,Y", name, p1);
        nBytes = 2;
        break;
    case IND:
        snprintf(strFormat, sizeof(strFormat), "%-4s ($%02X)", name, p1);
        nBytes = 2;
        break;
    case INDX:
        snprintf(strFormat, sizeof(strFormat), "%-4s ($%02X,X)", name, p1);
        nBytes = 2;
        break;
    case INDY:
        snprintf(strFormat, sizeof(strFormat), "%-4s ($%02X),Y", name, p1);
        nBytes = 2;
        break;
    case ABS:
        snprintf(strFormat, sizeof(strFormat), "%-4s $%02X%02X", name, p2, p1);
        nBytes = 3;
        break;
    case ABSX:
        snprintf(strFormat, sizeof(strFormat), "%-4s $%02X%02X,X", name, p2, p1);
        nBytes = 3;
        break;
    case ABSY:
        snprintf(strFormat, sizeof(strFormat), "%-4s $%02X%02X,Y", name, p2, p1);
        nBytes = 3;
        break;
    case IND16:
        snprintf(strFormat, sizeof(strFormat), "%-4s ($%02X%02X)", name, p2, p1);
        nBytes = 3;
        break;
    case BRA:
        snprintf(strFormat, sizeof(strFormat), "%-4s $%04X", name, (uint16_t)(addr + 2 + (int8_t)p1));
        nBytes = 2;
        break;
    default:
        snprintf(strFormat, sizeof(strFormat), "%-4s ???", name);
        nBytes = 1;
        break;
    }

    strncpy(pOutBuf, strFormat, nBufSize - 1);
    pOutBuf[nBufSize - 1] = '\0';

    return nBytes;
}

void CAtomDebugger::TraceInstruction(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t s, uint8_t status)
{
    char disBuf[32];
    Disassemble(pc, disBuf, sizeof(disBuf));

    Serial.printf("%04X: %-16s | A:%02X X:%02X Y:%02X S:01%02X P:%02X",
                          pc, disBuf, a, x, y, s, status);
}

void CAtomDebugger::DoDebugger(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t s, uint8_t status)
{
    for (int i = 0; i < 8; i++)
    {
        if (m_Breakpoints[i] == (int)pc)
        {
            char disBuf[32];
            Disassemble(pc, disBuf, sizeof(disBuf));

            Serial.printf("BREAK at %04X: %s | A=%02X X=%02X Y=%02X S=01%02X P=%02X",
                                  pc, disBuf, a, x, y, s, status);
            break;
        }
    }
}

void CAtomDebugger::DebugRead(uint16_t addr, uint8_t val)
{
    for (int i = 0; i < 8; i++)
    {
        if (m_WatchRead[i] == (int)addr)
        {
            Serial.printf("WATCH READ [0x%04X] = 0x%02X", addr, val);
        }
    }
}

void CAtomDebugger::DebugWrite(uint16_t addr, uint8_t val)
{
    for (int i = 0; i < 8; i++)
    {
        if (m_WatchWrite[i] == (int)addr)
        {
            Serial.printf("WATCH WRITE [0x%04X] <- 0x%02X", addr, val);
        }
    }
}