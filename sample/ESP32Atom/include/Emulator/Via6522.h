#pragma once

#include "PlatformTypes.h"
#include <stdint.h>

class CVia6522
{
public:
    CVia6522();
    void Reset();
    void Write(EmulatorWord address, EmulatorByte value);
    EmulatorByte Read(EmulatorWord address);
    int32_t GetT1C() const { return m_t1c; }
    int32_t GetT2C() const { return m_t2c; }
    EmulatorByte GetACR() const { return m_acr; }
    void DecrementT1C(int cycles) { m_t1c -= cycles; }
    void DecrementT2C(int cycles) { m_t2c -= cycles; }
    void UpdateTimers(int cycles);
    bool IsInterruptPending() const { return (m_ifr & 0x80) != 0; }

private:
    void UpdateIFR();
    EmulatorByte m_ora, m_orb, m_ddra, m_ddrb, m_porta, m_portb, m_irb;
    int32_t m_t1c, m_t1l, m_t2c, m_t2l;
    EmulatorByte m_acr, m_pcr, m_ifr, m_ier;
    EmulatorByte m_t1hit, m_t2hit, m_timerout;
};
