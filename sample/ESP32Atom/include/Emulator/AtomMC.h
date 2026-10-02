#pragma once

#include "PlatformTypes.h"

class CAtomMC
{
public:
    CAtomMC();
    void SetEmulator(class CAtomEmulator *emulator) { m_emulator = emulator; }
    bool Initialize(const char *path);
    void SetBasePath(const char *path);
    const char *GetBasePath() const { return m_basePath; }
    void Reset();
    EmulatorByte Read(EmulatorWord address);
    void Write(EmulatorWord address, EmulatorByte value);
    void Process();

private:
    class CAtomEmulator *m_emulator = nullptr;
    char m_basePath[96];
    EmulatorByte m_atomToMMC;
    EmulatorByte m_mmcToAtom;
    EmulatorByte m_latchedAddress;
};

extern const char *const g_AtomCoreFolders[4];
