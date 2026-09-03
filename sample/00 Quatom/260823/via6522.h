#pragma once

#include <stdint.h>
#include <circle/types.h>

class CVia6522
{
public:
    CVia6522();
    ~CVia6522();

    void Reset();
    void Write(uint16_t addr, uint8_t val);
    uint8_t Read(uint16_t addr);
    void UpdateTimers(int cycles);

    // Publieke toegang tot interne timer-tellers voor 6502 PollTime afhandeling
    int32_t GetT1C() const { return m_t1c; }
    int32_t GetT2C() const { return m_t2c; }
    uint8_t GetACR() const { return m_acr; }

    void DecrementT1C(int cycles) { m_t1c -= cycles; }
    void DecrementT2C(int cycles) { m_t2c -= cycles; }

    // Interrupt status opvragen
    bool IsInterruptPending() const { return (m_ifr & 0x80) != 0; }

private:
    void UpdateIFR();

private:
    // Register adresseringen
    static constexpr uint8_t ORB   = 0x00;
    static constexpr uint8_t ORA   = 0x01;
    static constexpr uint8_t DDRB  = 0x02;
    static constexpr uint8_t DDRA  = 0x03;
    static constexpr uint8_t T1CL  = 0x04;
    static constexpr uint8_t T1CH  = 0x05;
    static constexpr uint8_t T1LL  = 0x06;
    static constexpr uint8_t T1LH  = 0x07;
    static constexpr uint8_t T2CL  = 0x08;
    static constexpr uint8_t T2CH  = 0x09;
    static constexpr uint8_t SR    = 0x0A;
    static constexpr uint8_t ACR   = 0x0B;
    static constexpr uint8_t PCR   = 0x0C;
    static constexpr uint8_t IFR   = 0x0D;
    static constexpr uint8_t IER   = 0x0E;
    static constexpr uint8_t ORAnh = 0x0F;

    // Interrupt Bit-masks
    static constexpr uint8_t TIMER1INT = 0x40;
    static constexpr uint8_t TIMER2INT = 0x20;
    static constexpr uint8_t PORTBINT  = 0x18;
    static constexpr uint8_t PORTAINT  = 0x03;

    // Interne VIA Registers & Status
    uint8_t m_ora;
    uint8_t m_orb;
    uint8_t m_ddra;
    uint8_t m_ddrb;
    uint8_t m_porta;
    uint8_t m_portb;
    uint8_t m_irb;

    int32_t m_t1c;
    int32_t m_t1l;
    int32_t m_t2c;
    int32_t m_t2l;

    uint8_t m_acr;
    uint8_t m_pcr;
    uint8_t m_ifr;
    uint8_t m_ier;

    uint8_t m_t1hit;
    uint8_t m_t2hit;
    uint8_t m_timerout;
};