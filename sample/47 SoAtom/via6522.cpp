#include "via6522.h"
#include <string.h>

CVia6522::CVia6522()
    : m_ora(0), m_orb(0), m_ddra(0), m_ddrb(0),
      m_porta(0), m_portb(0), m_irb(0),
      m_t1c(0x1FFFE), m_t1l(0x1FFFE), m_t2c(0x1FFFE), m_t2l(0x1FFFE),
      m_acr(0), m_pcr(0), m_ifr(0), m_ier(0),
      m_t1hit(1), m_t2hit(1), m_timerout(1)
{
    Reset();
}

CVia6522::~CVia6522()
{
}

void CVia6522::Reset()
{
    m_ora = 0x80;
    m_orb = 0x00;
    m_ddra = 0x00;
    m_ddrb = 0x00;
    m_porta = 0x00;
    m_portb = 0x00;
    m_irb = 0x00;

    m_ifr = 0;
    m_ier = 0;
    m_t1c = m_t1l = 0x1FFFE;
    m_t2c = m_t2l = 0x1FFFE;
    m_t1hit = 1;
    m_t2hit = 1;
    m_timerout = 1;
    m_acr = 0;
    m_pcr = 0;

    UpdateIFR();
}

void CVia6522::UpdateIFR()
{
    if ((m_ifr & 0x7F) & (m_ier & 0x7F))
    {
        m_ifr |= 0x80;
    }
    else
    {
        m_ifr &= ~0x80;
    }
}

void CVia6522::UpdateTimers(int cycles)
{
    // Verwerk Timer 1
    if (m_t1c < -3)
    {
        while (m_t1c < -3)
        {
            m_t1c += m_t1l + 4;
        }

        if (!m_t1hit)
        {
            m_ifr |= TIMER1INT;
            UpdateIFR();
        }

        if ((m_acr & 0x80) && !m_t1hit)
        {
            m_timerout ^= 1;
        }

        if (!(m_acr & 0x40))
        {
            m_t1hit = 1;
        }
    }

    // Verwerk Timer 2
    if (!(m_acr & 0x20))
    {
        if (m_t2c < -3 && !m_t2hit)
        {
            if (!m_t2hit)
            {
                m_ifr |= TIMER2INT;
                UpdateIFR();
            }
            m_t2hit = 1;
        }
    }
}

void CVia6522::Write(uint16_t addr, uint8_t val)
{
    switch (addr & 0xF)
    {
    case ORA:
        m_ifr &= ~PORTAINT;
        UpdateIFR();
        // Fallthrough naar ORAnh!
    case ORAnh:
        m_ora = val;
        m_porta = (m_porta & ~m_ddra) | (m_ora & m_ddra);
        break;

    case ORB:
        m_orb = val;
        m_portb = (m_portb & ~m_ddrb) | (m_orb & m_ddrb);
        m_ifr &= ~PORTBINT;
        UpdateIFR();
        break;

    case DDRA:
        m_ddra = val;
        break;

    case DDRB:
        m_ddrb = val;
        break;

    case ACR:
        m_acr = val;
        break;

    case PCR:
        m_pcr = val;
        break;

    case T1LL:
    case T1CL:
        m_t1l = (m_t1l & 0xFF00) | val;
        break;

    case T1LH:
        m_t1l = (m_t1l & 0xFF) | (static_cast<int32_t>(val) << 8);
        if (m_acr & 0x40)
        {
            m_ifr &= ~TIMER1INT;
            UpdateIFR();
        }
        break;

    case T1CH:
        if ((m_acr & 0xC0) == 0x80)
        {
            m_timerout = 0;
        }
        m_t1l = (m_t1l & 0xFF) | (static_cast<int32_t>(val) << 8);
        m_t1c = m_t1l + 1;
        m_ifr &= ~TIMER1INT;
        UpdateIFR();
        m_t1hit = 0;
        break;

    case T2CL:
        m_t2l = (m_t2l & 0xFF00) | val;
        break;

    case T2CH:
        m_t2l = (m_t2l & 0xFF) | (static_cast<int32_t>(val) << 8);
        m_t2c = m_t2l;
        if (!(m_acr & 0x20))
        {
            m_t2c++;
        }
        m_ifr &= ~TIMER2INT;
        UpdateIFR();
        m_t2hit = 0;
        break;

    case IER:
        if (val & 0x80)
        {
            m_ier |= (val & 0x7F);
        }
        else
        {
            m_ier &= ~(val & 0x7F);
        }
        UpdateIFR();
        break;

    case IFR:
        m_ifr &= ~(val & 0x7F);
        UpdateIFR();
        break;

    default:
        break;
    }
}

uint8_t CVia6522::Read(uint16_t addr)
{
    uint8_t temp = 0xFF;

    switch (addr & 0xF)
    {
    case ORA:
        m_ifr &= ~PORTAINT;
        UpdateIFR();
        // Fallthrough naar ORAnh!
    case ORAnh:
        temp = m_ora & m_ddra;
        // PA7 input is cleared (printer busy status simulation)
        temp |= (m_porta & ~m_ddra & 0x7F);
        return temp;

    case ORB:
        UpdateIFR();
        temp = m_orb & m_ddrb;
        if (m_acr & 2)
        {
            temp |= (m_irb & ~m_ddrb);
        }
        else
        {
            temp |= (m_portb & ~m_ddrb);
        }
        if (m_acr & 0x80)
        {
            temp &= 0x7F;
            temp |= (m_timerout << 7); // Bugfix: |= i.p.v. !=
        }
        return temp;

    case DDRA:
        return m_ddra;

    case DDRB:
        return m_ddrb;

    case T1LL:
        return m_t1l & 0xFF;

    case T1LH:
        return (m_t1l >> 8) & 0xFF;

    case T1CL:
        m_ifr &= ~TIMER1INT;
        UpdateIFR();
        if (m_t1c < -1)
            return 0xFF;
        return m_t1c & 0xFF;

    case T1CH:
        if (m_t1c < -1)
            return 0xFF;
        return (m_t1c >> 8) & 0xFF;

    case T2CL:
        m_ifr &= ~TIMER2INT;
        UpdateIFR();
        return m_t2c & 0xFF;

    case T2CH:
        return (m_t2c >> 8) & 0xFF;

    case ACR:
        return m_acr;

    case PCR:
        return m_pcr;

    case IER:
        return m_ier | 0x80;

    case IFR:
        return m_ifr;

    default:
        break;
    }

    return 0xFF;
}