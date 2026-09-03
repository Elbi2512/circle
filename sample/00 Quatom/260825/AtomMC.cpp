#include "AtomMC.h"
#include "kernel.h"

#include <stdarg.h>      // <-- Voeg deze toe vóór stdio of ter vervanging ervan
#include <circle/util.h> // <-- Bevat memset, memcpy, etc.
#include <circle/fs/fat/fatfs.h>
#include <circle/string.h>
#include <circle/logger.h>

// --- Bare-metal Stubs voor POSIX File I/O in Circle ---
#ifndef BARE_METAL_FILE_STUBS
#define BARE_METAL_FILE_STUBS

#endif
// ----------------------------------------------------

#define ADDRESS_MASK 0x07

#define CMD_REG 0x00
#define LATCH_REG 0x01
#define READ_DATA_REG 0x02
#define WRITE_DATA_REG 0x03
#define STATUS_REG 0x04

#define CMD_DIR_OPEN 0x00
#define CMD_DIR_READ 0x01
#define CMD_DIR_CWD 0x02
#define CMD_DIR_MKDIR 0x04
#define CMD_DIR_RMDIR 0x05
#define CMD_RENAME 0x08

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
#define CMD_EXEC_PACKET 0x3F

#define CMD_LOAD_PARAM 0x40
#define CMD_GET_IMG_STATUS 0x41
#define CMD_GET_IMG_NAME 0x42
#define CMD_READ_IMG_SEC 0x43
#define CMD_WRITE_IMG_SEC 0x44
#define CMD_SER_IMG_INFO 0x45
#define CMD_VALID_IMG_NAMES 0x46
#define CMD_IMG_UNMOUNT 0x47

#define CMD_GET_CARD_TYPE 0x80
#define CMD_GET_FW_VER 0xE0
#define CMD_GET_BL_VER 0xE1
#define CMD_GET_CFG_BYTE 0xF0
#define CMD_SET_CFG_BYTE 0xF1
#define CMD_READ_AUX 0xFD
#define CMD_GET_HEARTBEAT 0xFE

#define STATUS_OK 0x3F
#define STATUS_COMPLETE 0x40
#define STATUS_EOF 0x60
#define STATUS_BUSY 0x80

#define EE_SYSFLAGS 0xFF

unsigned g_AtoMmcFile = 0; // 0 = geen bestand geopend

void AtoMMC_LoadFile(const char *pFilename, uint16_t address)
{
    CKernel *pKernel = CKernel::Get();
    if (!pKernel)
        return;

    CFATFileSystem *pFS = pKernel->GetFileSystem();

    unsigned nActiveCore = pKernel->GetCurrentCore();
    CAtomEmulator *pEmulator = pKernel->GetEmulator(nActiveCore);

    if (!pFS || !pEmulator)
        return;

    const char *pName = pFilename;
    if (pName[0] == '/')
    {
        pName++;
    }

    uint8_t *pRam = pEmulator->GetRam();

    unsigned hFile = pFS->FileOpen(pName);
    if (hFile != 0)
    {
        pFS->FileRead(hFile, &pRam[address], 0xFFFF);
        pFS->FileClose(hFile);

    //    CLogger::Get()->Write("AtomMC", LogNotice, "Bestand %s succesvol geladen op 0x%04X", pName, address);
    }
    else
    {
        CLogger::Get()->Write("AtomMC", LogError, "Kan bestand %s niet openen", pName);
    }
}

CAtomMC::CAtomMC()
    : m_baseMMCPath(),
      m_atomToMMC(0),
      m_blVersion(0x13),
      m_byteValueLatch(0),
      m_cardType(1),
      m_configByte(0),
      m_currentPath(),
      m_dir(),
      m_eePath(),
      m_eeprom(),
      m_fileNum(-1),
      m_globalAmount(0),
      m_globalData(),
      m_globalDataPresent(0),
      m_globalIndex(0),
      m_latchedAddress(0),
      m_latchedAddressLast(0),
      m_latchedAtomAddr(0),
      m_latchedData(0),
      m_mmcToAtom(0),
      m_pEmulator(nullptr),
      m_pOpenfile(nullptr),
      m_wasWrite(0),
      m_wildPattern(),
      m_windowData(),
      m_driveInfo(),
      info()
{
    memset(m_eeprom, 0xFF, EESIZE);
    memset(m_globalData, 0, sizeof(m_globalData));
    memset(m_windowData, 0, sizeof(m_windowData));
    memset(m_wildPattern, 0, sizeof(m_wildPattern));
    memset(m_driveInfo, 0xFF, sizeof(m_driveInfo));
    memset(m_baseMMCPath, 0, sizeof(m_baseMMCPath));
    memset(m_eePath, 0, sizeof(m_eePath));
    memset(m_currentPath, 0, sizeof(m_currentPath));
}

CAtomMC::~CAtomMC()
{
    Finalize();
}

void CAtomMC::SetEmulator(CAtomEmulator *pEmulator)
{
    m_pEmulator = pEmulator;
}

int CAtomMC::WildCmp(const char *wild, const char *string)
{
    const char *cp = NULL;
    const char *mp = NULL;

    while ((*string) && (*wild != '*'))
    {
        if ((*wild != *string) && (*wild != '?'))
            return 0;
        wild++;
        string++;
    }

    while (*string)
    {
        if (*wild == '*')
        {
            if (!*++wild)
                return 1;
            mp = wild;
            cp = string + 1;
        }
        else if ((*wild == *string) || (*wild == '?'))
        {
            wild++;
            string++;
        }
        else
        {
            wild = mp;
            string = cp++;
        }
    }

    while (*wild == '*')
        wild++;
    return !*wild;
}

bool CAtomMC::Initialize(const char *pFolderPath)
{
    CLogger::Get()->Write("AtomMMC", LogNotice, "Init MMC met pad: '%s'", pFolderPath);

    FRESULT res = f_opendir(&m_dir, pFolderPath);

    if (res != FR_OK)
    {
        CLogger::Get()->Write("AtomMMC", LogError, "FOUT: f_opendir faalt op '%s' met code %d", pFolderPath, res);
        m_bCardPresent = false;
        return false;
    }

    f_closedir(&m_dir);
  //  CLogger::Get()->Write("AtomMMC", LogNotice, "SUCCES: MMC map '%s' gevonden en geopend!", pFolderPath);

    m_bCardPresent = true;
    return true;
}

void CAtomMC::Reset()
{
    m_globalIndex = 0;
    m_globalAmount = 0;
    m_globalDataPresent = 0;
    m_fileNum = -1;
    m_currentPath[0] = '\0';
}

uint8_t CAtomMC::Read(uint16_t addr)
{
    (void)addr;
    m_wasWrite = 0;
    uint8_t current = m_mmcToAtom;
    Process();
    return current;
}

void CAtomMC::Write(uint16_t addr, uint8_t data)
{
    m_wasWrite = 1;
    m_latchedAtomAddr = (addr & 0x0F);
    m_atomToMMC = data;
    Process();
}

void CAtomMC::LoadEE()
{
    memset(m_eeprom, 0xFF, EESIZE);
}

void CAtomMC::SaveEE()
{
}

void CAtomMC::Finalize()
{
    SaveEE();
}

void CAtomMC::Process()
{
    uint8_t received;
    static uint8_t heartbeat = 0x55;

    m_latchedAddressLast = m_latchedAtomAddr;
    if (m_wasWrite)
    {
        m_latchedAddress = m_latchedAddressLast;
    }

    switch (m_latchedAddress & ADDRESS_MASK)
    {
    case CMD_REG:
        if (m_wasWrite)
        {
            m_latchedData = m_atomToMMC;
            received = m_latchedData;

            if ((received & 0x98) == 0x10)
            {
                m_fileNum = (received >> 5) & 3;
                received &= 0x9F;
            }

            if ((received & 0xF0) == 0x20)
            {
                m_fileNum = (received >> 2) & 3;
                received &= 0xF3;
            }

            m_mmcToAtom = STATUS_BUSY;

            if (received == CMD_DIR_OPEN)
                WfnDirectoryOpen();
            else if (received == CMD_DIR_READ)
                WfnDirectoryRead();
            else if (received == CMD_DIR_CWD)
                WfnSetCWDirectory();
            else if (received == CMD_DIR_MKDIR)
                WfnDirectoryCreate();
            else if (received == CMD_DIR_RMDIR)
                WfnDirectoryDelete();
            else if (received == CMD_RENAME)
                WfnRename();
            else if (received == CMD_FILE_CLOSE)
                WfnFileClose();
            else if (received == CMD_FILE_OPEN_READ)
                WfnFileOpenRead();
            else if (received == CMD_FILE_OPEN_IMG)
                WfnOpenSDDOSImg();
            else if (received == CMD_FILE_OPEN_WRITE)
                WfnFileOpenWrite();
            else if (received == CMD_FILE_OPEN_RAF)
                WfnFileOpenRAF();
            else if (received == CMD_FILE_DELETE)
                WfnFileDelete();
            else if (received == CMD_FILE_GETINFO)
                WfnFileGetInfo();
            else if (received == CMD_FILE_SEEK)
                WfnFileSeek();
            else if (received == CMD_INIT_READ)
            {
                m_mmcToAtom = m_globalData[0];
                m_globalIndex = 1;
                m_latchedAddress = READ_DATA_REG;
            }
            else if (received == CMD_INIT_WRITE)
            {
                m_globalIndex = 0;
                m_globalDataPresent = 0;
            }
            else if (received == CMD_READ_BYTES)
            {
                m_globalAmount = m_byteValueLatch;
                WfnFileRead();
            }
            else if (received == CMD_WRITE_BYTES)
            {
                m_globalAmount = m_byteValueLatch;
                WfnFileWrite();
            }
            else if (received == CMD_EXEC_PACKET)
            {
                WfnExecuteArbitrary();
            }
            else if (received == CMD_GET_IMG_STATUS)
            {
                m_mmcToAtom = m_driveInfo[m_byteValueLatch & 3].attribs;
            }
            else if (received == CMD_GET_IMG_NAME)
                WfnGetSDDOSImgNames();
            else if (received == CMD_READ_IMG_SEC)
                WfnReadSDDOSSect();
            else if (received == CMD_WRITE_IMG_SEC)
                WfnWriteSDDOSSect();
            else if (received == CMD_SER_IMG_INFO)
                WfnSerialiseSDDOSDrives();
            else if (received == CMD_VALID_IMG_NAMES)
                WfnValidateSDDOSDrives();
            else if (received == CMD_IMG_UNMOUNT)
                WfnUnmountSDDOSImg();
            else if (received == CMD_GET_CARD_TYPE)
                m_mmcToAtom = m_cardType;
            else if (received == CMD_GET_FW_VER)
                m_mmcToAtom = (2 << 4) | 13;
            else if (received == CMD_GET_BL_VER)
                m_mmcToAtom = m_blVersion;
            else if (received == CMD_GET_CFG_BYTE)
                m_mmcToAtom = m_configByte;
            else if (received == CMD_SET_CFG_BYTE)
            {
                m_configByte = m_byteValueLatch;
                m_eeprom[EE_SYSFLAGS] = m_configByte;
                SaveEE();
                m_mmcToAtom = STATUS_OK;
            }
            else if (received == CMD_READ_AUX)
                m_mmcToAtom = m_latchedAddress;
            else if (received == CMD_GET_HEARTBEAT)
            {
                m_mmcToAtom = heartbeat;
                heartbeat ^= 0xFF;
            }
        }
        break;

    case READ_DATA_REG:
        m_mmcToAtom = m_globalData[(int)m_globalIndex];
        m_globalIndex++;
        break;

    case WRITE_DATA_REG:
        if (m_wasWrite)
        {
            m_latchedData = m_atomToMMC;
            m_globalData[m_globalIndex] = m_latchedData;
            m_globalIndex++;
            m_globalDataPresent = 1;
        }
        break;

    case LATCH_REG:
        if (m_wasWrite)
        {
            m_latchedData = m_atomToMMC;
            m_byteValueLatch = m_latchedData;
            m_mmcToAtom = m_byteValueLatch;
        }
        break;
    }
}

void CAtomMC::GetWildcard()
{
   // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s, globalData: '%s'", __FUNCTION__, (const char *)m_globalData);

    size_t dataLen = strlen((const char *)m_globalData);
    size_t Idx = 0;
    int WildPos = -1;
    int LastSlash = -1;

    while ((Idx < dataLen) && (WildPos < 0))
    {
        if ((m_globalData[Idx] == '?') || (m_globalData[Idx] == '*'))
        {
            WildPos = (int)Idx;
        }

        if ((m_globalData[Idx] == '\\') || (m_globalData[Idx] == '/'))
        {
            LastSlash = (int)Idx;
        }

        Idx++;
    }

    constexpr size_t WILD_LEN = sizeof(m_wildPattern) - 1;

    if (WildPos > -1)
    {
        if (LastSlash > -1)
        {
            m_globalData[LastSlash] = 0x00;
            strncpy(m_wildPattern, (const char *)&m_globalData[LastSlash + 1], WILD_LEN);
            m_wildPattern[WILD_LEN] = '\0';
        }
        else
        {
            strncpy(m_wildPattern, (const char *)m_globalData, WILD_LEN);
            m_wildPattern[WILD_LEN] = '\0';
            m_globalData[0] = 0x00;
        }
    }
    else
    {
        strncpy(m_wildPattern, "*", WILD_LEN);
        m_wildPattern[WILD_LEN] = '\0';
    }
}

void CAtomMC::WfnDirectoryOpen()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s, globaldata: '%s'", __FUNCTION__, (const char *)m_globalData);

    // 1. Voer GetWildcard uit zoals de originele werkende code deed
    GetWildcard();

    // 2. Gebruik de core map + het huidige pad (m_currentPath)
    CString fullPath;
    fullPath.Format("%s%s", s_CoreFolderNames[m_pEmulator->GetCoreID()], m_currentPath);

    FRESULT res = f_opendir(&m_dir, fullPath);
    if (FR_OK != res)
    {
        m_mmcToAtom = STATUS_COMPLETE | res;
        return;
    }

    m_mmcToAtom = STATUS_OK;
}

void CAtomMC::WfnSetCWDirectory()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s, target: '%s'", __FUNCTION__, (const char *)m_globalData);

    // Als de map leeg is of "/", reset naar de root van deze core
    if (m_globalData[0] == '\0' || (m_globalData[0] == '/' && m_globalData[1] == '\0'))
    {
        m_currentPath[0] = '\0';
    }
    else if (strcmp((const char *)m_globalData, "..") == 0)
    {
        int lastSlashIdx = -1;
        for (int i = 0; m_currentPath[i] != '\0'; i++)
        {
            if (m_currentPath[i] == '/')
            {
                lastSlashIdx = i;
            }
        }

        if (lastSlashIdx != -1)
        {
            m_currentPath[lastSlashIdx] = '\0';
        }
        else
        {
            m_currentPath[0] = '\0';
        }
    }
    else
    {
        if (m_currentPath[0] != '\0')
        {
            strcat(m_currentPath, "/");
        }
        strcat(m_currentPath, (const char *)m_globalData);
    }

    // Controleer of de map fysiek bestaat via FatFs
    CString fullPath;
    fullPath.Format("%s%s", s_CoreFolderNames[m_pEmulator->GetCoreID()], m_currentPath);

    DIR testDir;
    FRESULT res = f_opendir(&testDir, fullPath);
    if (res == FR_OK)
    {
        f_closedir(&testDir);
        // Geef exact STATUS_OK terug i.p.v. STATUS_COMPLETE | FR_OK om de Atom niet te blokkeren
        m_mmcToAtom = STATUS_OK;
    }
    else
    {
        // Als de map niet bestaat, draaien we de pad-toevoeging weer terug om desync te voorkomen
        // (of we geven de foutcode terug)
        m_mmcToAtom = STATUS_COMPLETE | res;
    }
}

void CAtomMC::WfnDirectoryRead()
{
    FILINFO *filinfo = &filinfodata[0];
    char len;
    int Match;

    while (1)
    {
        char n = 0;

        FRESULT res = f_readdir(&m_dir, filinfo);
        if (res != FR_OK || !filinfo->fname[0])
        {
            m_mmcToAtom = STATUS_COMPLETE | res;
            return;
        }

        Match = WildCmp(m_wildPattern, filinfo->fname);

        if (Match)
        {
            len = (char)strlen(filinfo->fname);

            if (filinfo->fattrib & AM_DIR)
            {
                n = 1;
                m_globalData[0] = '<';
            }

            // strcpy((char *)&m_globalData[n], (const char *)filinfo->fname);
            strcpy((char *)&m_globalData[(int)n], (const char *)filinfo->fname);

            if (filinfo->fattrib & AM_DIR)
            {
                m_globalData[len + 1] = '>';
                m_globalData[len + 2] = 0;
                len += 2;
            }

            m_globalData[len + 1] = filinfo->fattrib;
            memcpy(&m_globalData[len + 2], (void *)&(filinfo->fsize), sizeof(DWORD));

            m_mmcToAtom = STATUS_OK;
            return;
        }
    }
}

CString CAtomMC::GetFullPath(const char *pSubPath)
{
    CString fullPath;
    fullPath.Format("%s", s_CoreFolderNames[m_pEmulator->GetCoreID()]);

    if (m_currentPath[0] != '\0')
    {
        fullPath += m_currentPath;
        fullPath += "/";
    }

    if (pSubPath != nullptr && pSubPath[0] != '\0')
    {
        // Als pSubPath begint met een slash, slaan we m_currentPath over (absolute pad-notatie binnen core)
        if (pSubPath[0] == '/')
        {
            fullPath.Format("%s%s", s_CoreFolderNames[m_pEmulator->GetCoreID()], pSubPath + 1);
        }
        else
        {
            fullPath += pSubPath;
        }
    }

    return fullPath;
}

void CAtomMC::WfnDirectoryCreate()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s", __FUNCTION__);
    CString fullPath;

    fullPath.Format("%s%s", s_CoreFolderNames[m_pEmulator->GetCoreID()], (const char *)m_globalData);
    FRESULT res = f_mkdir(fullPath);

    m_mmcToAtom = STATUS_COMPLETE | res;
}

void CAtomMC::WfnDirectoryDelete()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s", __FUNCTION__);
    CString fullPath;

    fullPath.Format("%s%s", s_CoreFolderNames[m_pEmulator->GetCoreID()], (const char *)m_globalData);
    FRESULT res = f_unlink((const char *)fullPath);

    m_mmcToAtom = STATUS_COMPLETE | res;
}

void CAtomMC::WfnRename()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s", __FUNCTION__);
    CString fullPathFrom, fullPathTo;

    fullPathFrom.Format("%s%s", s_CoreFolderNames[m_pEmulator->GetCoreID()], (const char *)m_globalData);
    fullPathTo.Format("%s%s", s_CoreFolderNames[m_pEmulator->GetCoreID()], (const char *)m_globalData + strlen((const char *)m_globalData) + 1);

    FRESULT res = f_rename(fullPathFrom, fullPathTo);

    m_mmcToAtom = STATUS_COMPLETE | res;
}

void CAtomMC::WfnFileOpenRead()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s", __FUNCTION__);
    uint8_t m_res;

    m_res = fileOpen(FA_OPEN_EXISTING | FA_READ);

    if (m_fileNum >= 0 && m_fileNum < 4)
    {
        FILINFO *filinfo = &filinfodata[m_fileNum];
        GetFileInfoSpecial(filinfo);
    }

    m_mmcToAtom = STATUS_COMPLETE | m_res;
}

void CAtomMC::WfnFileOpenWrite()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s", __FUNCTION__);
    uint8_t result = fileOpen(FA_CREATE_NEW | FA_WRITE);
    m_mmcToAtom = STATUS_COMPLETE | result;
}

void CAtomMC::WfnFileOpenRAF()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s", __FUNCTION__);
    uint8_t result = fileOpen(FA_OPEN_ALWAYS | FA_WRITE);
    m_mmcToAtom = STATUS_COMPLETE | result;
}

void CAtomMC::WfnFileGetInfo()
{
    // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s", __FUNCTION__);

    if (m_fileNum < 0 || m_fileNum >= 4)
    {
        m_mmcToAtom = STATUS_OK;
        return;
    }

    FIL *fil = &fildata[m_fileNum];
    FILINFO *filinfo = &filinfodata[m_fileNum];

    union
    {
        DWORD dword;
        char byte[4];
    } dwb;

    dwb.dword = f_size(fil);
    m_globalData[0] = dwb.byte[0];
    m_globalData[1] = dwb.byte[1];
    m_globalData[2] = dwb.byte[2];
    m_globalData[3] = dwb.byte[3];

    dwb.dword = 0;
    m_globalData[4] = dwb.byte[0];
    m_globalData[5] = dwb.byte[1];
    m_globalData[6] = dwb.byte[2];
    m_globalData[7] = dwb.byte[3];

    dwb.dword = fil->fptr;
    m_globalData[8] = dwb.byte[0];
    m_globalData[9] = dwb.byte[1];
    m_globalData[10] = dwb.byte[2];
    m_globalData[11] = dwb.byte[3];

    m_globalData[12] = filinfo->fattrib & 0x3F;

    m_mmcToAtom = STATUS_OK;
}

void CAtomMC::WfnFileRead()
{
    int ret;
    if (m_fileNum < 0 || m_fileNum >= 4)
    {
        m_mmcToAtom = STATUS_COMPLETE | 1;
        return;
    }

    FIL *fil = &fildata[m_fileNum];
    UINT read = 0;

    if (m_globalAmount == 0)
    {
        m_globalAmount = 256;
    }

    ret = f_read(fil, m_globalData, m_globalAmount, &read);

    if (m_fileNum > 0 && ret == 0 && m_globalAmount != read)
    {
        m_mmcToAtom = STATUS_EOF;
    }
    else
    {
        m_mmcToAtom = STATUS_COMPLETE | ret;
    }
}

void CAtomMC::WfnFileWrite()
{
    if (m_fileNum < 0 || m_fileNum >= 4)
    {
        m_mmcToAtom = STATUS_COMPLETE | 1;
        return;
    }

    FIL *fil = &fildata[m_fileNum];
    UINT written = 0;

    if (m_globalAmount == 0)
    {
        m_globalAmount = 256;
    }

    FRESULT res = f_write(fil, (void *)m_globalData, m_globalAmount, &written);
    m_mmcToAtom = STATUS_COMPLETE | res;
}

void CAtomMC::WfnFileClose()
{
    if (m_fileNum >= 0 && m_fileNum < 4)
    {
        f_close(&fildata[m_fileNum]);
    }
    m_pOpenfile = nullptr;
    m_mmcToAtom = STATUS_COMPLETE;
}

void CAtomMC::WfnFileDelete()
{
    if (m_fileNum >= 0 && m_fileNum < 4)
    {
        f_close(&fildata[m_fileNum]);
    }

    FRESULT res = f_unlink((const char *)&m_globalData[0]);
    m_mmcToAtom = STATUS_COMPLETE | res;
}

void CAtomMC::WfnFileSeek()
{
    if (m_fileNum < 0 || m_fileNum >= 4)
    {
        m_mmcToAtom = STATUS_COMPLETE | 1;
        return;
    }

    FIL *fil = &fildata[m_fileNum];

    union
    {
        DWORD dword;
        char byte[4];
    } dwb;

    dwb.byte[0] = m_globalData[0];
    dwb.byte[1] = m_globalData[1];
    dwb.byte[2] = m_globalData[2];
    dwb.byte[3] = m_globalData[3];

    FRESULT res = f_lseek(fil, dwb.dword);
    m_mmcToAtom = STATUS_COMPLETE | res;
}

void CAtomMC::WfnExecuteArbitrary()
{
    if (m_globalAmount == 0 && m_globalDataPresent == 0)
    {
        m_mmcToAtom = STATUS_COMPLETE | ERROR_NO_DATA;
        return;
    }

#ifndef COM_RE
#define COM_RE 0x4552
#endif
#ifndef COM_WE
#define COM_WE 0x4557
#endif

    WORD cmdWord = ((WORD)m_globalData[1] << 8) | m_globalData[0];

    switch (cmdWord)
    {
    case COM_RE:
    {
        WORD start = (WORD)m_globalData[2];
        WORD end = start + (WORD)m_globalData[3];

        WORD i, n = 0;
        for (i = start; i < end; ++i, ++n)
        {
            m_globalData[n] = (i < EESIZE) ? m_eeprom[i] : 0xFF;
        }

        m_mmcToAtom = STATUS_OK;
    }
    break;

    case COM_WE:
    {
        WORD start = (WORD)m_globalData[2];
        WORD end = start + (WORD)m_globalData[3];

        WORD i, n = 4;
        for (i = start; i < end; ++i, ++n)
        {
            if (i < EESIZE)
            {
                m_eeprom[i] = m_globalData[n];
            }
        }
        SaveEE();

        m_mmcToAtom = STATUS_OK;
    }
    break;

    default:
        m_mmcToAtom = STATUS_COMPLETE | ERROR_NO_DATA;
        break;
    }
}

void CAtomMC::WfnOpenSDDOSImg() { m_mmcToAtom = STATUS_OK; }
void CAtomMC::WfnReadSDDOSSect() { m_mmcToAtom = STATUS_OK; }
void CAtomMC::WfnWriteSDDOSSect() { m_mmcToAtom = STATUS_OK; }
void CAtomMC::WfnValidateSDDOSDrives() { m_mmcToAtom = STATUS_OK; }
void CAtomMC::WfnSerialiseSDDOSDrives() { m_mmcToAtom = STATUS_OK; }
void CAtomMC::WfnUnmountSDDOSImg() { m_mmcToAtom = STATUS_OK; }
void CAtomMC::WfnGetSDDOSImgNames() { m_mmcToAtom = STATUS_OK; }

void CAtomMC::GetFileInfoSpecial(FILINFO *fno)
{
    if (fno == nullptr)
        return;

    if (m_pOpenfile != nullptr)
    {
        fno->fsize = f_size(m_pOpenfile);
        fno->fdate = 0;
        fno->ftime = 0;
        fno->fattrib = AM_ARC;
    }
}

uint8_t CAtomMC::fileOpen(uint8_t mode)
{
    int ret = 0;
    if (m_pEmulator != nullptr)
    {
        CString fullPath = GetFullPath((const char *)m_globalData);
        // CLogger::Get()->Write("AtomMC", LogNotice, "Func: %s, fullPath ", __FUNCTION__, fullPath);
        // fullPath.Format("%s%s", s_CoreFolderNames[m_pEmulator->GetCoreID()], (const char *)m_globalData);

        if (m_fileNum == 0)
        {
            ret = f_open(&fildata[0], fullPath, mode);
            if (ret == FR_OK)
            {
                m_pOpenfile = &fildata[0];
            }
        }
        else
        {
            m_fileNum = 0;
            if (!fildata[1].obj.fs)
            {
                m_fileNum = 1;
            }
            else if (!fildata[2].obj.fs)
            {
                m_fileNum = 2;
            }
            else if (!fildata[3].obj.fs)
            {
                m_fileNum = 3;
            }
            if (m_fileNum > 0)
            {
                ret = f_open(&fildata[m_fileNum], fullPath, mode);
                if (ret == FR_OK)
                {
                    m_pOpenfile = &fildata[m_fileNum];
                    ret = FILENUM_OFFSET | m_fileNum;
                }
                else
                {
                    m_pOpenfile = nullptr;
                }
            }
            else
            {
                ret = ERROR_TOO_MANY_OPEN;
            }
        }
    }
    return STATUS_COMPLETE | ret;
}
