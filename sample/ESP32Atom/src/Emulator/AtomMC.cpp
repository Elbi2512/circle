#include "Emulator/AtomMC.h"
#include "AtomStoragePaths.h"
#include <string.h>

extern uint8_t ReadMMC(uint16_t address);
extern void WriteMMC(uint16_t address, uint8_t value);
extern void at_process(void);

const char *const g_AtomCoreFolders[4] = {ATOM_SD_GERA, ATOM_SD_ROLA, ATOM_SD_KEVO, ATOM_SD_TOWA};

CAtomMC::CAtomMC() { Reset(); }

bool CAtomMC::Initialize(const char *path)
{
    SetBasePath(path);
    return path != nullptr && path[0] != '\0';
}

void CAtomMC::Reset()
{
    m_atomToMMC = 0;
    m_mmcToAtom = 0xff;
    m_latchedAddress = 0;
    strncpy(m_basePath, ATOM_SD_ROOT, sizeof(m_basePath) - 1);
    m_basePath[sizeof(m_basePath) - 1] = 0;
}

void CAtomMC::SetBasePath(const char *path)
{
    if (!path) { m_basePath[0] = 0; return; }
    strncpy(m_basePath, path, sizeof(m_basePath) - 1);
    m_basePath[sizeof(m_basePath) - 1] = 0;
}

EmulatorByte CAtomMC::Read(EmulatorWord address)
{
    m_latchedAddress = static_cast<EmulatorByte>(address & 0x0f);
    m_mmcToAtom = ReadMMC(address);
    return m_mmcToAtom;
}

void CAtomMC::Write(EmulatorWord address, EmulatorByte value)
{
    m_latchedAddress = static_cast<EmulatorByte>(address & 0x0f);
    m_atomToMMC = value;
    WriteMMC(address, value);
}

void CAtomMC::Process()
{
    // The Arduino AtoMMC firmware owns directory and file state. Keep this
    // boundary explicit so emulator bus accesses do not duplicate that state.
    at_process();
}
