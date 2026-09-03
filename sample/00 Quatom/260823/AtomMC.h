#pragma once

#include <stdint.h>
#include <stddef.h>
#include <circle/types.h>
#include <ff.h>
#include <circle/fs/fat/fatfs.h> // <--- Voeg deze toe voor FILINFO

static const char *const s_CoreFolderNames[4] = {
    "SD:/Gera/", "SD:/Rola/", "SD:/Kevo/", "SD:/Towa/"};

#ifndef MAXPATH
#define MAXPATH 260
#endif
#define CMD_REG 0x00
#define LATCH_REG 0x01
#define READ_DATA_REG 0x02
#define WRITE_DATA_REG 0x03
#define STATUS_REG 0x04

// DIR_CMD_REG commands
#define CMD_DIR_OPEN 0x00
#define CMD_DIR_READ 0x01
#define CMD_DIR_CWD 0x02
#define CMD_DIR_GETCWD 0x03
#define CMD_DIR_MKDIR 0x04
#define CMD_DIR_RMDIR 0x05

#define CMD_RENAME 0x08

// CMD_REG_COMMANDS
#define CMD_FILE_CLOSE 0x10
#define CMD_FILE_OPEN_READ 0x11
#define CMD_FILE_OPEN_IMG 0x12
#define CMD_FILE_OPEN_WRITE 0x13
#define CMD_FILE_DELETE 0x14
#define CMD_FILE_GETINFO 0x15
#define CMD_FILE_SEEK 0x16
#define CMD_FILE_OPEN_RAF 0x17

#define CMD_INIT_READ 0x20
#define CMD_INIT_WRITE 0x21
#define CMD_READ_BYTES 0x22
#define CMD_WRITE_BYTES 0x23

// READ_DATA_REG "commands"

// EXEC_PACKET_REG "commands"
#define CMD_EXEC_PACKET 0x3F

// SDOS_LBA_REG commands
#define CMD_LOAD_PARAM 0x40
#define CMD_GET_IMG_STATUS 0x41
#define CMD_GET_IMG_NAME 0x42
#define CMD_READ_IMG_SEC 0x43
#define CMD_WRITE_IMG_SEC 0x44
#define CMD_SER_IMG_INFO 0x45
#define CMD_VALID_IMG_NAMES 0x46
#define CMD_IMG_UNMOUNT 0x47

// UTIL_CMD_REG commands
#define CMD_GET_CARD_TYPE 0x80
#define CMD_GET_PORT_DDR 0xA0
#define CMD_SET_PORT_DDR 0xA1
#define CMD_READ_PORT 0xA2
#define CMD_WRITE_PORT 0xA3
#define CMD_GET_FW_VER 0xE0
#define CMD_GET_BL_VER 0xE1
#define CMD_GET_CFG_BYTE 0xF0
#define CMD_SET_CFG_BYTE 0xF1
#define CMD_READ_AUX 0xFD
#define CMD_GET_HEARTBEAT 0xFE

// Status codes
#define STATUS_OK 0x3F
#define STATUS_COMPLETE 0x40
#define STATUS_EOF 0x60
#define STATUS_BUSY 0x80

#define ERROR_MASK 0x3F

// To be or'd with STATUS_COMPLETE
#define ERROR_NO_DATA 0x08
#define ERROR_INVALID_DRIVE 0x09
#define ERROR_READ_ONLY 0x0A
#define ERROR_ALREADY_MOUNT 0x0A
#define ERROR_TOO_MANY_OPEN 0x12

// Offset returned file numbers by 0x20, to disambiguate from errors
#define FILENUM_OFFSET 0x20

#define EESIZE 1024

class CAtomEmulator; // Forward declaration

class CAtomMC
{
public:
    CAtomMC();
    ~CAtomMC();

    void Finalize();
    bool Initialize(const char *pFolderPath);
    void LoadEE();
    void Process();
    uint8_t Read(uint16_t addr);
    void Reset();
    void SaveEE();
    void SetEmulator(CAtomEmulator *pEmulator);
    void Write(uint16_t addr, uint8_t data);
    
    bool m_bCardPresent = false;

private:
    uint8_t fileOpen(uint8_t);
    void GetFileInfoSpecial(FILINFO *);
    CString GetFullPath(const char *pSubPath);
    void GetWildcard();
    int WildCmp(const char *wild, const char *string);
    void WfnDirectoryCreate();
    void WfnDirectoryDelete();
    void WfnDirectoryOpen();
    void WfnDirectoryRead();
    void WfnExecuteArbitrary();
    void WfnFileClose();
    void WfnFileDelete();
    void WfnFileGetInfo();
    void WfnFileOpenRAF();
    void WfnFileOpenRead();
    void WfnFileOpenWrite();
    void WfnFileRead();
    void WfnFileSeek();
    void WfnFileWrite();
    void WfnGetSDDOSImgNames();
    void WfnOpenSDDOSImg();
    void WfnReadSDDOSSect();
    void WfnRename();
    void WfnSerialiseSDDOSDrives();
    void WfnSetCWDirectory();
    void WfnUnmountSDDOSImg();
    void WfnValidateSDDOSDrives();
    void WfnWriteSDDOSSect();

    // Variabelen (alfabetisch gesorteerd)
    char m_baseMMCPath[MAXPATH + 1];
    uint8_t m_atomToMMC;
    uint8_t m_blVersion;
    uint8_t m_byteValueLatch;
    uint8_t m_cardType;
    uint8_t m_configByte;
    char m_currentPath[MAXPATH + 1];
    DIR m_dir;
    char m_eePath[MAXPATH + 1];
    uint8_t m_eeprom[EESIZE];
    FIL fildata[4];
    FILINFO filinfodata[4];
    int m_fileNum;
    uint16_t m_globalAmount;
    uint8_t m_globalData[256];
    uint8_t m_globalDataPresent;
    uint8_t m_globalIndex;
    uint8_t m_latchedAddress;
    uint8_t m_latchedAddressLast;
    uint8_t m_latchedAtomAddr;
    uint8_t m_latchedData;
    uint8_t m_mmcToAtom;
    CAtomEmulator *m_pEmulator;
    FIL *m_pOpenfile;
    uint8_t m_wasWrite;
    char m_wildPattern[17];
    char m_windowData[512];

    struct ImgInfo
    {
        char filename[13];
        uint8_t attribs;
    } m_driveInfo[4];

    FILINFO info;
};