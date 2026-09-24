#include "fdc1770.h"
#include <string.h>

CFdc1770::CFdc1770()
    : m_nByte(0), m_nCurDrive(0), m_bNMI(false),
      m_bMotorOn(false), m_nMotorSpin(0), m_nFdcTime(0)
{
    memset(&m_State, 0, sizeof(m_State));
    Reset();
}

CFdc1770::~CFdc1770()
{
}

void CFdc1770::Reset()
{
    m_bNMI = false;
    SetINTRQ(0);
    SetDRQ(0);

    memset(&m_State, 0, sizeof(m_State));
    m_State.status = 0;
    m_State.fifo = 0;

    m_nMotorSpin = 45000;
    m_nFdcTime = 0;
}

void CFdc1770::SpinUp()
{
    m_State.status |= ST_MOTOR;
    m_bMotorOn = true;
    m_nMotorSpin = 0;
}

void CFdc1770::SpinDown()
{
    m_State.status &= ~ST_MOTOR;
    m_bMotorOn = false;
}

void CFdc1770::SetSpinDown()
{
    m_nMotorSpin = 45000;
}

void CFdc1770::SetDRQ(int state)
{
    if (state)
        m_State.ctrl |= CTL_DRQ;
    else
        m_State.ctrl &= ~CTL_DRQ;
}

void CFdc1770::SetINTRQ(int state)
{
    if (state)
        m_bNMI = true;
    else
        m_bNMI = false;
}

uint8_t CFdc1770::Read(uint16_t addr)
{
    uint8_t temp;

    switch (addr)
    {
    case CTRLREG:
        m_State.ctrl ^= CTL_INDEX;
        return m_State.ctrl;

    case WDSTAT:
        SetINTRQ(0);
        return m_State.status;

    case WDTRK:
        return m_State.track;

    case WDSEC:
        return m_State.sector;

    case WDDATA:
        temp = m_State.data;
        m_State.status &= ~ST_DRQ;
        SetDRQ(0);
        return temp;

    default:
        break;
    }

    return 0xFE;
}

void CFdc1770::Write(uint16_t addr, uint8_t val)
{
    uint8_t track0 = m_State.curtrack ? 0 : 4;

    switch (addr)
    {
    case CTRLREG:
        m_State.ctrl = val;
        if (val & CTL_DS0)
            m_nCurDrive = 0;
        else
            m_nCurDrive = 1;

        m_State.curside = (m_State.ctrl & CTL_SIDE) ? 1 : 0;
        m_State.density = !(m_State.ctrl & CTL_DDEN);

        if ((val & CTL_RESET) == 0)
            Reset();
        break;

    case WDCMD:
        if ((m_State.status & ST_BUSY) && (val >> 4) != 0xD)
        {
            return; // Reject command while busy
        }

        m_State.command = val;
        if ((val >> 4) != 0xD)
            SpinUp();

        switch (val >> 4)
        {
        case 0x0: /* Restore */
            m_State.status = ST_MOTOR | ST_SPINUP | ST_BUSY | track0;
            DiscSeek(m_nCurDrive, 0);
            break;

        case 0x1: /* Seek */
            m_State.status = ST_MOTOR | ST_SPINUP | ST_BUSY | track0;
            DiscSeek(m_nCurDrive, m_State.data);
            break;

        case 0x2:
        case 0x3: /* Step */
            m_State.status = ST_MOTOR | ST_SPINUP | ST_BUSY | track0;
            m_State.curtrack += m_State.stepdir;
            if (m_State.curtrack < 0)
                m_State.curtrack = 0;
            DiscSeek(m_nCurDrive, m_State.curtrack);
            break;

        case 0x4:
        case 0x5: /* Step in */
            m_State.status = ST_MOTOR | ST_SPINUP | ST_BUSY | track0;
            m_State.curtrack++;
            DiscSeek(m_nCurDrive, m_State.curtrack);
            m_State.stepdir = 1;
            break;

        case 0x6:
        case 0x7: /* Step out */
            m_State.status = ST_MOTOR | ST_SPINUP | ST_BUSY | track0;
            m_State.curtrack--;
            if (m_State.curtrack < 0)
                m_State.curtrack = 0;
            DiscSeek(m_nCurDrive, m_State.curtrack);
            m_State.stepdir = -1;
            break;

        case 0x8: /* Read sector */
            m_State.status = ST_MOTOR | ST_BUSY;
            DiscReadSector(m_nCurDrive, m_State.sector, m_State.track, m_State.curside, m_State.density);
            m_nByte = 0;
            break;

        case 0xA: /* Write sector */
            m_State.status = ST_MOTOR | ST_BUSY;
            DiscWriteSector(m_nCurDrive, m_State.sector, m_State.track, m_State.curside, m_State.density);
            m_nByte = 0;
            SetDRQ(1);
            m_State.status |= ST_DRQ;
            break;

        case 0xC: /* Read address */
            m_State.status = ST_MOTOR | ST_BUSY;
            DiscReadAddress(m_nCurDrive, m_State.track, m_State.curside, m_State.density);
            m_nByte = 0;
            break;

        case 0xD: /* Force interrupt */
            m_nFdcTime = 0;
            m_State.status = ST_MOTOR | track0;
            SetINTRQ(1);
            SpinDown();
            break;

        case 0xF: /* Write track */
            m_State.status = ST_MOTOR | ST_BUSY;
            DiscFormat(m_nCurDrive, m_State.track, m_State.curside, m_State.density);
            break;

        default:
            m_nFdcTime = 0;
            SetINTRQ(1);
            m_State.status = 0x90;
            SpinDown();
            break;
        }
        break;

    case WDTRK:
        m_State.track = val;
        break;

    case WDSEC:
        m_State.sector = val;
        break;

    case WDDATA:
        m_State.status &= ~ST_DRQ;
        m_State.data = val;
        SetDRQ(0);
        m_State.written = 1;
        break;

    default:
        break;
    }
}

void CFdc1770::Callback()
{
    uint8_t track0 = m_State.curtrack ? 0 : 4;
    m_nFdcTime = 0;

    switch (m_State.command >> 4)
    {
    case 0: /* Restore */
        m_State.curtrack = m_State.track = 0;
        m_State.status = ST_MOTOR;
        SetSpinDown();
        SetINTRQ(1);
        break;

    case 1: /* Seek */
        m_State.curtrack = m_State.track = m_State.data;
        m_State.status = ST_MOTOR | track0;
        SetSpinDown();
        SetINTRQ(1);
        break;

    case 3: /* Step */
    case 5: /* Step in */
    case 7: /* Step out */
        m_State.track = m_State.curtrack;
        /* Fallthrough */
    case 2:
    case 4:
    case 6:
        m_State.status = ST_MOTOR | track0;
        SetSpinDown();
        SetINTRQ(1);
        break;

    case 8:   /* Read sector */
    case 0xA: /* Write sector */
        m_State.status = ST_MOTOR;
        SetSpinDown();
        SetINTRQ(1);
        break;

    case 0xC: /* Read address */
        m_State.status = ST_MOTOR;
        SetSpinDown();
        SetINTRQ(1);
        m_State.sector = m_State.track;
        break;

    case 0xF: /* Write track */
        m_State.status = ST_MOTOR;
        SetSpinDown();
        SetINTRQ(1);
        break;

    default:
        break;
    }
}

void CFdc1770::Data(uint8_t dat)
{
    if (!(m_State.status & ST_BUSY))
        return;

    if (m_State.status & ST_DRQ)
    {
        m_nFdcTime = 0;
        m_State.status = ST_MOTOR | ST_LOST; // Overrun
        SpinDown();
        return;
    }

    m_State.data = dat;
    m_State.status |= ST_DRQ;
    SetDRQ(1);
}

void CFdc1770::FinishRead()
{
    m_nFdcTime = 200;
}

void CFdc1770::NotFound()
{
    m_nFdcTime = 0;
    m_State.status = ST_MOTOR | ST_RNF; // Record not found
    SetINTRQ(1);
    SpinDown();
}

void CFdc1770::DataCrcError()
{
    m_nFdcTime = 0;
    m_State.status = ST_MOTOR | ST_CRC;
    SetINTRQ(1);
    SpinDown();
}

void CFdc1770::HeaderCrcError()
{
    m_nFdcTime = 0;
    m_State.status = ST_MOTOR | ST_RNF | ST_CRC;
    SetINTRQ(1);
    SpinDown();
}

int CFdc1770::GetData(int last)
{
    if (!(m_State.status & ST_BUSY))
        return 0xFF;

    if (!m_State.written)
        return -1;

    if (!last)
    {
        m_State.status |= ST_DRQ;
        SetDRQ(1);
    }
    m_State.written = 0;
    return m_State.data;
}

void CFdc1770::WriteProtect()
{
    m_nFdcTime = 0;
    m_State.status = ST_MOTOR | ST_WRITEP;
    SetINTRQ(1);
    SpinDown();
}

// Stubs voor schijfkopie-operaties
void CFdc1770::DiscSeek(int drive, int track)
{
    (void)drive;
    (void)track;
}
void CFdc1770::DiscReadSector(int drive, int sector, int track, int side, int density)
{
    (void)drive;
    (void)sector;
    (void)track;
    (void)side;
    (void)density;
}
void CFdc1770::DiscWriteSector(int drive, int sector, int track, int side, int density)
{
    (void)drive;
    (void)sector;
    (void)track;
    (void)side;
    (void)density;
}
void CFdc1770::DiscReadAddress(int drive, int track, int side, int density)
{
    (void)drive;
    (void)track;
    (void)side;
    (void)density;
}
void CFdc1770::DiscFormat(int drive, int track, int side, int density)
{
    (void)drive;
    (void)track;
    (void)side;
    (void)density;
}