#pragma once

#include <stdint.h>
#include <circle/types.h>

// Adresdefinities
#define WDBASE  0xBC10
#define CTRLREG 0xBC14

#define WDCMD   (WDBASE + 0)
#define WDSTAT  (WDBASE + 0)
#define WDTRK   (WDBASE + 1)
#define WDSEC   (WDBASE + 2)
#define WDDATA  (WDBASE + 3)

#define DRQ     0x01
#define INTRQ   0x02

// Status register masks
#define ST_BUSY     0x01
#define ST_DRQ      0x02
#define ST_IP       0x02
#define ST_LOST     0x04
#define ST_TR00     0x04
#define ST_CRC      0x08
#define ST_RNF      0x10
#define ST_TYPE     0x20
#define ST_SPINUP   0x20
#define ST_WRITEP   0x40
#define ST_MOTOR    0x80

// GDOS Control register masks
#define CTL_SIDE    0x01 // Side select mask
#define CTL_DS0     0x02 // Drive select 0
#define CTL_DS1     0x04 // Drive select 1
#define CTL_DDEN    0x08 // Double density enable (low)
#define CTL_RESET   0x10 // Reset FDC
#define CTL_4080    0x20 // 40(low)/80(high) track switch
#define CTL_INDEX   0x40 // Index pulse
#define CTL_DRQ     0x80 // DRQ

class CFdc1770
{
public:
    CFdc1770();
    ~CFdc1770();

    void Reset();

    // Register I/O via geheugenbus
    uint8_t Read(uint16_t addr);
    void Write(uint16_t addr, uint8_t val);

    // Event & Callback handlers
    void Callback();
    void Data(uint8_t dat);
    void FinishRead();
    void NotFound();
    void DataCrcError();
    void HeaderCrcError();
    void WriteProtect();
    int  GetData(int last);

    // Status setters/getters
    void SpinDown();
    int  GetFdcTime() const { return m_nFdcTime; }
    void SetFdcTime(int time) { m_nFdcTime = time; }
    void DecrementFdcTime(int cycles) { m_nFdcTime -= cycles; }

    bool IsMotorOn() const { return m_bMotorOn; }
    int  GetMotorSpin() const { return m_nMotorSpin; }
    void DecrementMotorSpin() { if (m_nMotorSpin > 0) m_nMotorSpin--; }

    bool GetNMI() const { return m_bNMI; }

private:
    void SpinUp();
    void SetSpinDown();

    void SetDRQ(int state);
    void SetINTRQ(int state);

    // Disc I/O stubs
    void DiscSeek(int drive, int track);
    void DiscReadSector(int drive, int sector, int track, int side, int density);
    void DiscWriteSector(int drive, int sector, int track, int side, int density);
    void DiscReadAddress(int drive, int track, int side, int density);
    void DiscFormat(int drive, int track, int side, int density);

private:
    struct WD1770State
    {
        uint8_t command;
        uint8_t sector;
        uint8_t track;
        uint8_t status;
        uint8_t data;
        uint8_t ctrl;
        int     curside;
        int     curtrack;
        int     density;
        int     written;
        int     stepdir;
        int     fifo;
    };

    WD1770State m_State;

    int  m_nByte;
    int  m_nCurDrive;
    bool m_bNMI;

    // Timing & Motor beheervariabelen
    bool m_bMotorOn;
    int  m_nMotorSpin;
    int  m_nFdcTime;
};