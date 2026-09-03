#pragma once

#include <stdint.h>
#include <circle/types.h>

class CFdc8271
{
public:
    CFdc8271();
    ~CFdc8271();

    void Reset();

    // Register I/O via de 6502 geheugenbus ($0A00-$0A07)
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
    int GetData(int last);

    // Timing & Motor status
    void SpinDown();
    int GetFdcTime() const { return m_nFdcTime; }
    void SetFdcTime(int time) { m_nFdcTime = time; }
    void DecrementFdcTime(int cycles) { m_nFdcTime -= cycles; }

    bool IsMotorOn() const { return m_bMotorOn; }
    int GetMotorSpin() const { return m_nMotorSpin; }
    void DecrementMotorSpin()
    {
        if (m_nMotorSpin > 0)
            m_nMotorSpin--;
    }

    bool GetNMI() const { return m_bNMI; }

private:
    void UpdateNMI();
    void SpinUp();
    void SetSpinDown();
    int GetParamsCount();
    void Seek();

    // Schijf I/O stubs (kunnen gekoppeld worden aan schijfkopie-bestanden / SD-kaart)
    void DiscSeek(int drive, int track);
    void DiscReadSector(int drive, int sector, int track, int side, int density);
    void DiscWriteSector(int drive, int sector, int track, int side, int density);
    void DiscReadAddress(int drive, int track, int side, int density);
    void DiscFormat(int drive, int track, int side, int density);

private:
    struct I8271State
    {
        uint8_t command;
        uint8_t params[5];
        int drivesel;
        int paramnum;
        int paramreq;
        uint8_t status;
        uint8_t result;
        int curtrack[2];
        int cursector;
        int realtrack[2];
        int sectorsleft;
        uint8_t data;
        int phase;
        int written;
        uint8_t drvout;
    };

    I8271State m_State;

    int m_nByte;
    int m_nVerify;
    int m_nCurDrive;
    bool m_bNMI;

    // Timing & Motor beheervariabelen
    bool m_bMotorOn;
    int m_nMotorSpin;
    int m_nFdcTime;

    // Command-to-parameter-count opzoektabel
    struct ParamLookup
    {
        int cmd;
        int count;
    };
    static const ParamLookup s_ParamTable[];
};