#pragma once

#include <stdint.h>
#include <stddef.h>
#include <circle/types.h>
#include <circle/string.h>
#include <fatfs/ff.h>

#ifndef MAXPATH
#define MAXPATH 260
#endif

#define EESIZE 1024

// Pad-definities per emulatorkern (0..3)
extern const char *const s_CoreFolderNames[4];

// Forward declaration
class CAtomEmulator;

class CAtomMC
{
public:
    CAtomMC();
    ~CAtomMC();

    bool Initialize(const char *pFolderPath);
    void Reset();
    void Finalize();

    void SetEmulator(CAtomEmulator *pEmulator);

    uint8_t Read(uint16_t addr);
    void    Write(uint16_t addr, uint8_t data);
    void    Process();

    void LoadEE();
    void SaveEE();

    bool m_bCardPresent{false};

private:
    uint8_t fileOpen(uint8_t mode);
    void    GetFileInfoSpecial(FILINFO *fno);
    CString GetFullPath(const char *pSubPath);
    void    GetWildcard();
    int     WildCmp(const char *wild, const char *string);

    // Command Handlers
    void WfnDirectoryOpen();
    void WfnDirectoryRead();
    void WfnSetCWDirectory();
    void WfnDirectoryCreate();
    void WfnDirectoryDelete();
    void WfnRename();

    void WfnFileClose();
    void WfnFileOpenRead();
    void WfnFileOpenWrite();
    void WfnFileOpenRAF();
    void WfnFileDelete();
    void WfnFileGetInfo();
    void WfnFileSeek();
    void WfnFileRead();
    void WfnFileWrite();

    void WfnExecuteArbitrary();

    // SDDOS Image Stubs
    void WfnOpenSDDOSImg();
    void WfnReadSDDOSSect();
    void WfnWriteSDDOSSect();
    void WfnValidateSDDOSDrives();
    void WfnSerialiseSDDOSDrives();
    void WfnUnmountSDDOSImg();
    void WfnGetSDDOSImgNames();

    // -----------------------------------------------------------------------
    // Member variabelen (volgorde strikt gelijk aan constructor-lijst!)
    // -----------------------------------------------------------------------
    CAtomEmulator *m_pEmulator{nullptr};

    uint8_t m_cardType{1};
    uint8_t m_blVersion{0x13};
    uint8_t m_configByte{0};

    uint8_t m_atomToMMC{0};
    uint8_t m_mmcToAtom{0};
    uint8_t m_byteValueLatch{0};
    uint8_t m_latchedAddress{0};
    uint8_t m_latchedAddressLast{0};
    uint8_t m_latchedAtomAddr{0};
    uint8_t m_latchedData{0};
    uint8_t m_wasWrite{0};

    int      m_fileNum{-1};
    uint16_t m_globalAmount{0};
    uint8_t  m_globalIndex{0};
    uint8_t  m_globalDataPresent{0};

    char m_wildPattern[17];
    char m_currentPath[MAXPATH + 1];
    char m_baseMMCPath[MAXPATH + 1];
    char m_eePath[MAXPATH + 1];

    uint8_t m_globalData[256];
    uint8_t m_windowData[512];
    uint8_t m_eeprom[EESIZE];

    DIR     m_dir;
    FIL     fildata[4];
    FILINFO filinfodata[4];
    FIL    *m_pOpenfile{nullptr};

    struct ImgInfo
    {
        char    filename[13];
        uint8_t attribs;
    } m_driveInfo[4];

    FILINFO info;
};

// Vrije helper functie
void AtoMMC_LoadFile(const char *pFilename, uint16_t address);