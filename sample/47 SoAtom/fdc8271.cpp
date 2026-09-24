#include "fdc8271.h"
#include <string.h>

const CFdc8271::ParamLookup CFdc8271::s_ParamTable[] = {
    {0x35, 4}, {0x29, 1}, {0x2C, 0}, {0x3D, 1}, {0x3A, 2}, {0x13, 3}, {0x0B, 3}, {0x1B, 3}, {0x1F, 3}, {0x23, 5}, {-1, -1}};

CFdc8271::CFdc8271()
    : m_nByte(0), m_nVerify(0), m_nCurDrive(0), m_bNMI(false),
      m_bMotorOn(false), m_nMotorSpin(0), m_nFdcTime(0)
{
    memset(&m_State, 0, sizeof(m_State));
    Reset();
}

CFdc8271::~CFdc8271()
{
}

void CFdc8271::Reset()
{
    memset(&m_State, 0, sizeof(m_State));
    m_State.paramnum = 0;
    m_State.paramreq = 0;
    m_State.status = 0;
    m_State.command = 0xFF;
    m_State.curtrack[0] = m_State.curtrack[1] = 0;
    m_State.realtrack[0] = m_State.realtrack[1] = 0;

    m_nFdcTime = 0;
    m_nCurDrive = 0;
    m_nByte = 0;
    m_nVerify = 0;
    m_bMotorOn = false;
    m_nMotorSpin = 0;

    UpdateNMI();
}

void CFdc8271::UpdateNMI()
{
    if (m_State.status & 0x08)
    {
        m_bNMI = true;
    }
    else
    {
        m_bNMI = false;
    }
}

void CFdc8271::SpinUp()
{
    m_bMotorOn = true;
    m_nMotorSpin = 0;
}

void CFdc8271::SpinDown()
{
    m_bMotorOn = false;
}

void CFdc8271::SetSpinDown()
{
    m_nMotorSpin = 45000;
}

int CFdc8271::GetParamsCount()
{
    int c = 0;
    while (s_ParamTable[c].cmd != -1)
    {
        if (s_ParamTable[c].cmd == m_State.command)
        {
            return s_ParamTable[c].count;
        }
        c++;
    }
    return 0;
}

void CFdc8271::Seek()
{
    int diff = m_State.params[0] - m_State.curtrack[m_nCurDrive];
    m_State.realtrack[m_nCurDrive] += diff;
    DiscSeek(m_nCurDrive, m_State.realtrack[m_nCurDrive]);
}

uint8_t CFdc8271::Read(uint16_t addr)
{
    uint8_t val;

    switch (addr & 7)
    {
    case 0: /* Status register */
        return m_State.status;

    case 1: /* Result register */
        m_State.status &= ~0x18;
        UpdateNMI();
        val = m_State.result;
        m_State.result = 0;
        return val;

    case 4: /* Data register */
        m_State.status &= ~0x0C;
        UpdateNMI();
        return m_State.data;

    default:
        break;
    }
    return 0;
}

void CFdc8271::Write(uint16_t addr, uint8_t val)
{
    switch (addr & 7)
    {
    case 0: /* Command register */
        if (m_State.status & 0x80)
            return;

        m_State.command = val & 0x3F;
        if (m_State.command == 0x17)
            m_State.command = 0x13;

        m_State.drivesel = val >> 6;
        m_nCurDrive = (val & 0x80) ? 1 : 0;
        m_State.paramnum = 0;
        m_State.paramreq = GetParamsCount();
        m_State.status = 0x80;

        if (!m_State.paramreq)
        {
            switch (m_State.command)
            {
            case 0x2C: /* Read drive status */
            {
                uint8_t track0 = m_State.curtrack[m_nCurDrive] ? 0 : 2;
                m_State.status = 0x10;
                m_State.result = 0x80 | 8 | track0;
                if (m_State.drivesel & 1)
                    m_State.result |= 0x04;
                if (m_State.drivesel & 2)
                    m_State.result |= 0x40;
                break;
            }
            default:
                m_State.result = 0x10;
                m_State.status = 0x10;
                UpdateNMI();
                m_nFdcTime = 0;
                break;
            }
        }
        break;

    case 1: /* Parameter register */
        if (m_State.paramnum < 5)
            m_State.params[m_State.paramnum++] = val;

        if (m_State.paramnum == m_State.paramreq)
        {
            switch (m_State.command)
            {
            case 0x0B: /* Write sector */
            case 0x13: /* Read sector */
            case 0x1F: /* Verify sector */
                m_State.sectorsleft = m_State.params[2] & 31;
                m_State.cursector = m_State.params[1];
                SpinUp();
                m_State.phase = 0;
                if (m_State.curtrack[m_nCurDrive] != m_State.params[0])
                    Seek();
                else
                    m_nFdcTime = 200;

                if (m_State.command == 0x1F)
                    m_nVerify = 1;
                break;

            case 0x1B: /* Read ID */
                m_State.sectorsleft = m_State.params[2] & 31;
                SpinUp();
                m_State.phase = 0;
                if (m_State.curtrack[m_nCurDrive] != m_State.params[0])
                    Seek();
                else
                    m_nFdcTime = 200;
                break;

            case 0x23: /* Format track */
                SpinUp();
                m_State.phase = 0;
                if (m_State.curtrack[m_nCurDrive] != m_State.params[0])
                    Seek();
                else
                    m_nFdcTime = 200;
                break;

            case 0x29: /* Seek */
                Seek();
                SpinUp();
                break;

            case 0x35: /* Specify */
                m_State.status = 0;
                break;

            case 0x3A: /* Write special register */
                m_State.status = 0;
                switch (m_State.params[0])
                {
                case 0x12:
                    m_State.curtrack[0] = val;
                    break;
                case 0x17:
                    break;
                case 0x1A:
                    m_State.curtrack[1] = val;
                    break;
                case 0x23:
                    m_State.drvout = m_State.params[1];
                    break;
                default:
                    m_State.result = 0x18;
                    m_State.status = 0x18;
                    UpdateNMI();
                    m_nFdcTime = 0;
                    break;
                }
                break;

            case 0x3D: /* Read special register */
                m_State.status = 0x10;
                m_State.result = 0;
                switch (m_State.params[0])
                {
                case 0x06:
                    m_State.result = 0;
                    break;
                case 0x12:
                    m_State.result = m_State.curtrack[0];
                    break;
                case 0x1A:
                    m_State.result = m_State.curtrack[1];
                    break;
                case 0x23:
                    m_State.result = m_State.drvout;
                    break;
                default:
                    m_State.result = 0x18;
                    m_State.status = 0x18;
                    UpdateNMI();
                    m_nFdcTime = 0;
                    break;
                }
                break;

            default:
                m_State.result = 0x18;
                m_State.status = 0x18;
                UpdateNMI();
                m_nFdcTime = 0;
                break;
            }
        }
        break;

    case 2: /* Reset register */
        if (val & 1)
            Reset();
        break;

    case 4: /* Data register */
        m_State.data = val;
        m_State.written = 1;
        m_State.status &= ~0x0C;
        UpdateNMI();
        break;

    default:
        break;
    }
}

void CFdc8271::Callback()
{
    m_nFdcTime = 0;

    switch (m_State.command)
    {
    case 0x0B: /* Write */
        if (!m_State.phase)
        {
            m_State.curtrack[m_nCurDrive] = m_State.params[0];
            DiscWriteSector(m_nCurDrive, m_State.cursector, m_State.params[0], (m_State.drvout & 0x20) ? 1 : 0, 0);
            m_State.phase = 1;
            m_State.status = 0x8C;
            m_State.result = 0;
            UpdateNMI();
            return;
        }
        m_State.sectorsleft--;
        if (!m_State.sectorsleft)
        {
            m_State.status = 0x18;
            m_State.result = 0;
            UpdateNMI();
            SetSpinDown();
            m_nVerify = 0;
            return;
        }
        m_State.cursector++;
        DiscWriteSector(m_nCurDrive, m_State.cursector, m_State.params[0], (m_State.drvout & 0x20) ? 1 : 0, 0);
        m_nByte = 0;
        m_State.status = 0x8C;
        m_State.result = 0;
        UpdateNMI();
        break;

    case 0x13: /* Read */
    case 0x1F: /* Verify */
        if (!m_State.phase)
        {
            m_State.curtrack[m_nCurDrive] = m_State.params[0];
            DiscReadSector(m_nCurDrive, m_State.cursector, m_State.params[0], (m_State.drvout & 0x20) ? 1 : 0, 0);
            m_State.phase = 1;
            return;
        }
        m_State.sectorsleft--;
        if (!m_State.sectorsleft)
        {
            m_State.status = 0x18;
            m_State.result = 0;
            UpdateNMI();
            SetSpinDown();
            m_nVerify = 0;
            return;
        }
        m_State.cursector++;
        DiscReadSector(m_nCurDrive, m_State.cursector, m_State.params[0], (m_State.drvout & 0x20) ? 1 : 0, 0);
        m_nByte = 0;
        break;

    case 0x1B: /* Read ID */
        if (!m_State.phase)
        {
            m_State.curtrack[m_nCurDrive] = m_State.params[0];
            DiscReadAddress(m_nCurDrive, m_State.params[0], (m_State.drvout & 0x20) ? 1 : 0, 0);
            m_State.phase = 1;
            return;
        }
        m_State.sectorsleft--;
        if (!m_State.sectorsleft)
        {
            m_State.status = 0x18;
            m_State.result = 0;
            UpdateNMI();
            SetSpinDown();
            return;
        }
        m_State.cursector++;
        DiscReadAddress(m_nCurDrive, m_State.params[0], (m_State.drvout & 0x20) ? 1 : 0, 0);
        m_nByte = 0;
        break;

    case 0x23: /* Format */
        if (!m_State.phase)
        {
            m_State.curtrack[m_nCurDrive] = m_State.params[0];
            DiscWriteSector(m_nCurDrive, m_State.cursector, m_State.params[0], (m_State.drvout & 0x20) ? 1 : 0, 0);
            m_State.phase = 1;
            m_State.status = 0x8C;
            m_State.result = 0;
            UpdateNMI();
            return;
        }
        if (m_State.phase == 2)
        {
            m_State.status = 0x18;
            m_State.result = 0;
            UpdateNMI();
            SetSpinDown();
            m_nVerify = 0;
            return;
        }
        DiscFormat(m_nCurDrive, m_State.params[0], (m_State.drvout & 0x20) ? 1 : 0, 0);
        m_State.phase = 2;
        break;

    case 0x29: /* Seek */
        m_State.curtrack[m_nCurDrive] = m_State.params[0];
        m_State.status = 0x18;
        m_State.result = 0;
        UpdateNMI();
        SetSpinDown();
        break;

    default:
        break;
    }
}

void CFdc8271::Data(uint8_t dat)
{
    if (m_nVerify)
        return;

    m_State.data = dat;
    m_State.status = 0x8C;
    m_State.result = 0;
    UpdateNMI();
    m_nByte++;
}

void CFdc8271::FinishRead()
{
    m_nFdcTime = 200;
}

void CFdc8271::NotFound()
{
    m_State.result = 0x18;
    m_State.status = 0x18;
    UpdateNMI();
    m_nFdcTime = 0;
    SetSpinDown();
}

void CFdc8271::DataCrcError()
{
    m_State.result = 0x0E;
    m_State.status = 0x18;
    UpdateNMI();
    m_nFdcTime = 0;
    SetSpinDown();
}

void CFdc8271::HeaderCrcError()
{
    m_State.result = 0x0C;
    m_State.status = 0x18;
    UpdateNMI();
    m_nFdcTime = 0;
    SetSpinDown();
}

int CFdc8271::GetData(int last)
{
    m_nByte++;
    if (!m_State.written)
        return -1;

    if (!last)
    {
        m_State.status = 0x8C;
        m_State.result = 0;
        UpdateNMI();
    }
    m_State.written = 0;
    return m_State.data;
}

void CFdc8271::WriteProtect()
{
    m_State.result = 0x12;
    m_State.status = 0x18;
    UpdateNMI();
    m_nFdcTime = 0;
}

// Stubs voor schijfkopie operaties (koppelbaar aan SD-kaart image / FATFS)
void CFdc8271::DiscSeek(int drive, int track)
{
    (void)drive;
    (void)track;
}
void CFdc8271::DiscReadSector(int drive, int sector, int track, int side, int density)
{
    (void)drive;
    (void)sector;
    (void)track;
    (void)side;
    (void)density;
}
void CFdc8271::DiscWriteSector(int drive, int sector, int track, int side, int density)
{
    (void)drive;
    (void)sector;
    (void)track;
    (void)side;
    (void)density;
}
void CFdc8271::DiscReadAddress(int drive, int track, int side, int density)
{
    (void)drive;
    (void)track;
    (void)side;
    (void)density;
}
void CFdc8271::DiscFormat(int drive, int track, int side, int density)
{
    (void)drive;
    (void)track;
    (void)side;
    (void)density;
}