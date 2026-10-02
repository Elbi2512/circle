#pragma once

#include "PlatformTypes.h"

class CPia8255
{
public:
    CPia8255();
    void Reset();
    void Initialize() { Reset(); }
    void PollTape() {}
    void Write(EmulatorWord address, EmulatorByte value);
    EmulatorByte Read(EmulatorWord address) const;
    void KeyDown(EmulatorByte key);
    void KeyUp(EmulatorByte key);
    void ClearKeyboard();
    void SetShift(bool value) { m_shift = value; }
    void SetCtrl(bool value) { m_ctrl = value; }
    void SetRept(bool value) { m_rept = value; }
    EmulatorByte GetGfxMode() const { return m_nGfxMode; }
    EmulatorByte GetCss() const { return m_nCss; }

private:
    EmulatorByte m_nGfxMode;
    EmulatorByte m_nCss;
    EmulatorByte m_PortA;
    EmulatorByte m_PortB;
    EmulatorByte m_PortC;
    EmulatorByte m_Control;
    bool m_bShift;
    bool m_bCtrl;
    bool m_bRept;
    EmulatorByte m_KeyMatrix[10];
};
