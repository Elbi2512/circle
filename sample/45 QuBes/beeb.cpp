#include "beeb.h"
#include <circle/util.h>
#include <circle/string.h>
#include "roms.h"
#include "bbc_cpu.h"
#include "bbc_machine.h"
#include "sn76489.h"
// #include "via6522.h"
#include "bbc_memory.h"
#include "bbc_tape.h"

#define BBC_BUF_W 640 /* pixels per VGA line / BBC row */

static const char FromBeeb[] = "beeb";

CBeebRunner *CBeebRunner::s_pThis = nullptr;

static const u64 FRAME_TIME_US = 20000; // 50 Hz (PAL BBC timing)
static const u64 MAX_CATCHUP_US = FRAME_TIME_US * 5;

// BBC ULA 8-kleuren palet naar 32-bit ARGB (0xAARRGGBB)
// Bit 0 = R, Bit 1 = G, Bit 2 = B
static const u32 s_bbc_palette_argb[8] __attribute__((aligned(16))) = {
    0xFF000000, // 0 Black
    0xFFFF0000, // 1 Red
    0xFF00FF00, // 2 Green
    0xFFFFFF00, // 3 Yellow
    0xFF0000FF, // 4 Blue
    0xFFFF00FF, // 5 Magenta
    0xFF00FFFF, // 6 Cyan
    0xFFFFFFFF  // 7 White
};

#define BBC_SECTORS_PER_TRACK 10
#define BBC_SECTOR_SIZE 256
#define BBC_TRACKS 80

static const char FromDisk[] = "bbcdisk";

int CBeebRunner::DiskReadSector(void *user_ctx,
                                uint8_t drive, uint8_t track, uint8_t sector,
                                uint8_t side, uint8_t density,
                                uint8_t *buf, uint16_t *len)
{
    (void)drive;
    (void)density;

    bbc_circle_disk_ctx_t *ctx = (bbc_circle_disk_ctx_t *)user_ctx;
    if (!ctx || !ctx->isOpen) return -1;
    if (track  >= BBC_TRACKS)            return -1;
    if (sector >= BBC_SECTORS_PER_TRACK) return -1;
    if (!ctx->is_dsd && side != 0)       return -1;

    FSIZE_t offset;
    if (ctx->is_dsd) {
        offset = ((track * 2 + side) * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    } else {
        offset = (track * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    }

    if (f_lseek(&ctx->file, offset) != FR_OK) return -1;

    UINT bytesRead = 0;
    if (f_read(&ctx->file, buf, BBC_SECTOR_SIZE, &bytesRead) != FR_OK || bytesRead != BBC_SECTOR_SIZE) {
        return -1;
    }

    *len = BBC_SECTOR_SIZE;
    return 0;
}

int CBeebRunner::DiskWriteSector(void *user_ctx,
                                 uint8_t drive, uint8_t track, uint8_t sector,
                                 uint8_t side, uint8_t density, bool deleted,
                                 const uint8_t *buf, uint16_t len)
{
    (void)drive;
    (void)density;
    (void)deleted;

    bbc_circle_disk_ctx_t *ctx = (bbc_circle_disk_ctx_t *)user_ctx;
    if (!ctx || !ctx->isOpen || ctx->read_only) return -1;
    if (track  >= BBC_TRACKS)                   return -1;
    if (sector >= BBC_SECTORS_PER_TRACK)        return -1;
    if (!ctx->is_dsd && side != 0)              return -1;
    if (len != BBC_SECTOR_SIZE)                 return -1;

    FSIZE_t offset;
    if (ctx->is_dsd) {
        offset = ((track * 2 + side) * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    } else {
        offset = (track * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    }

    if (f_lseek(&ctx->file, offset) != FR_OK) return -1;

    UINT bytesWritten = 0;
    if (f_write(&ctx->file, buf, BBC_SECTOR_SIZE, &bytesWritten) != FR_OK || bytesWritten != BBC_SECTOR_SIZE) {
        return -1;
    }

    f_sync(&ctx->file);
    return 0;
}

void CBeebRunner::DiskSeek(void * /*user_ctx*/, uint8_t /*drive*/, uint8_t /*track*/)
{
    // No physical head movement needed
}

bool CBeebRunner::MountSDDiskImage(void)
{
    DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, "SD:/");
    if (res != FR_OK)
    {
        CLogger::Get()->Write(FromBeeb, LogWarning, "Kan SD:/ niet openen (FR=%d)", res);
        return false;
    }

    char imgPath[256] = {};
    bool isDsd = false;

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0')
    {
        if (fno.fattrib & AM_DIR)
            continue;
        size_t len = strlen(fno.fname);
        if (len < 4)
            continue;

        const char *ext = &fno.fname[len - 4];
        if (strcasecmp(ext, ".ssd") == 0 || strcasecmp(ext, ".dsd") == 0)
        {
            CString Path;
            Path.Format("SD:/%s", fno.fname);
            strncpy(imgPath, (const char *)Path, sizeof(imgPath) - 1);
            imgPath[sizeof(imgPath) - 1] = '\0';
            // circle_snprintf(imgPath, sizeof(imgPath), "SD:/%s", fno.fname);
            isDsd = (strcasecmp(ext, ".dsd") == 0);
            break;
        }
    }
    f_closedir(&dir);

    if (imgPath[0] == '\0')
    {
        CLogger::Get()->Write(FromBeeb, LogNotice, "Geen .ssd of .dsd disk image gevonden");
        return false;
    }

    m_Disk[0].is_dsd = isDsd;
    m_Disk[0].read_only = false;
    res = f_open(&m_Disk[0].file, imgPath, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
    if (res != FR_OK)
    {
        res = f_open(&m_Disk[0].file, imgPath, FA_READ | FA_OPEN_EXISTING);
        m_Disk[0].read_only = true;
    }

    if (res == FR_OK)
    {
        m_Disk[0].isOpen = true;
        CLogger::Get()->Write(FromBeeb, LogNotice, "Disk gekoppeld: %s (%s, %s)",
                              imgPath, isDsd ? "DSD" : "SSD",
                              m_Disk[0].read_only ? "read-only" : "read-write");

        bbc_machine_mount_disk(&m_Machine, 0,
                               DiskReadSector,
                               DiskWriteSector,
                               DiskSeek,
                               &m_Disk[0]);
        return true;
    }

    CLogger::Get()->Write(FromBeeb, LogError, "Fout bij openen van %s", imgPath);
    return false;
}

// ------------------------------------------------------------
// USB HID naar BBC Keyboard Matrix
// ------------------------------------------------------------
BbcKeyPos CBeebRunner::ConvertHIDToBBCKey(uint8_t hid)
{
    switch (hid)
    {
    // Row 0
    case 0x14:
        return {0, 1}; // Q
    case 0x43:
        return {0, 2}; // F10 (F0)
    case 0x1E:
        return {0, 3}; // 1
    case 0x39:
        return {0, 4}; // Caps Lock
    case 0x2B:
        return {0, 6}; // Tab
    case 0x29:
        return {0, 7}; // Escape

    // Row 1
    case 0x20:
        return {1, 1}; // 3
    case 0x1A:
        return {1, 2}; // W
    case 0x1F:
        return {1, 3}; // 2
    case 0x04:
        return {1, 4}; // A
    case 0x16:
        return {1, 5}; // S
    case 0x1D:
        return {1, 6}; // Z

    // Row 2
    case 0x21:
        return {2, 1}; // 4
    case 0x08:
        return {2, 2}; // E
    case 0x15:
        return {2, 3}; // R
    case 0x07:
        return {2, 4}; // D
    case 0x09:
        return {2, 5}; // F
    case 0x1B:
        return {2, 6}; // X
    case 0x06:
        return {2, 7}; // C

    // Row 3
    case 0x22:
        return {3, 1}; // 5
    case 0x17:
        return {3, 2}; // T
    case 0x23:
        return {3, 3}; // 6
    case 0x0A:
        return {3, 4}; // G
    case 0x0B:
        return {3, 5}; // H
    case 0x19:
        return {3, 6}; // V
    case 0x05:
        return {3, 7}; // B

    // Row 4
    case 0x3D:
        return {4, 1}; // F4
    case 0x24:
        return {4, 2}; // 7
    case 0x25:
        return {4, 3}; // 8
    case 0x1C:
        return {4, 4}; // Y
    case 0x0D:
        return {4, 5}; // J
    case 0x11:
        return {4, 6}; // N
    case 0x2C:
        return {4, 7}; // Space

    // Row 5
    case 0x3E:
        return {5, 1}; // F5
    case 0x0C:
        return {5, 2}; // I
    case 0x12:
        return {5, 3}; // O
    case 0x18:
        return {5, 4}; // U
    case 0x0E:
        return {5, 5}; // K
    case 0x10:
        return {5, 6}; // M
    case 0x36:
        return {5, 7}; // Comma

    // Row 6
    case 0x3F:
        return {6, 1}; // F6
    case 0x26:
        return {6, 2}; // 9
    case 0x27:
        return {6, 3}; // 0
    case 0x13:
        return {6, 4}; // P
    case 0x0F:
        return {6, 5}; // L
    case 0x37:
        return {6, 7}; // Period

    // Row 7
    case 0x40:
        return {7, 1}; // F7
    case 0x2D:
        return {7, 2}; // Minus
    case 0x2E:
        return {7, 3}; // Equals
    case 0x34:
        return {7, 4}; // Quote / AT
    case 0x33:
        return {7, 5}; // Semicolon
    case 0x38:
        return {7, 6}; // Slash

    // Row 8
    case 0x3A:
        return {8, 0}; // F1
    case 0x3B:
        return {8, 1}; // F2
    case 0x3C:
        return {8, 2}; // F3
    case 0x45:
        return {8, 3}; // F12 -> Break
    case 0x52:
    case 0x60:
        return {8, 5}; // Up Arrow
    case 0x2A:
    case 0x4C:
        return {8, 7}; // Backspace / Delete

    // Row 9
    case 0x4D:
    case 0x65:
        return {9, 3}; // End -> COPY
    case 0x4F:
    case 0x5E:
        return {9, 5}; // Right Arrow
    case 0x28:
    case 0x58:
        return {9, 6}; // Return / KP Enter

    default:
        return {0xFF, 0xFF};
    }
}

// ------------------------------------------------------------
// Runner Constructie & Initialisatie
// ------------------------------------------------------------
CBeebRunner::CBeebRunner(CBcmFrameBuffer *pFrameBuffer, CTimer *pTimer,
                         CDeviceNameService *pDeviceNameService, CMemorySystem *pMemorySystem)
    : CMultiCoreSupport(pMemorySystem),
      m_pFrameBuffer(pFrameBuffer),
      m_pTimer(pTimer),
      m_pDeviceNameService(pDeviceNameService),
      m_pKeyboard(nullptr),
      m_bKeyboardReady(FALSE),
      m_bCoreInitDone(FALSE),
      m_bShutdown(FALSE)
{
    s_pThis = this;
    memset(m_Disk, 0, sizeof(m_Disk));
    memset(&m_Machine, 0, sizeof(m_Machine));
}

CBeebRunner::~CBeebRunner(void)
{
    for (int i = 0; i < 2; i++)
    {
        if (m_Disk[i].isOpen)
        {
            f_close(&m_Disk[i].file);
        }
    }
    s_pThis = nullptr;
}

boolean CBeebRunner::Initialize(void)
{
    uint32_t os_size = (uint32_t)(os12_rom_end - os12_rom);
    uint32_t basic_size = (uint32_t)(basic2_rom_end - basic2_rom);
    uint32_t dfs_size = (uint32_t)(dfs1770_rom_end - dfs1770_rom);

    CLogger::Get()->Write(FromBeeb, LogNotice, "BBC Micro Emulator Init: OS=%u B, BASIC=%u B, DFS=%u B",
                          os_size, basic_size, dfs_size);

    bbc_machine_init(&m_Machine, os12_rom, os_size, basic2_rom, basic_size);
    bbc_machine_load_sideways_rom(&m_Machine, dfs1770_rom, dfs_size, 14);

    MountSDDiskImage();
    bbc_machine_reset(&m_Machine);

    boolean bOK = CMultiCoreSupport::Initialize();
    if (bOK)
    {
        m_bCoreInitDone = TRUE;
    }
    return bOK;
}

// ------------------------------------------------------------
// Framebuffer Rendering (Direct 640x480)
// ------------------------------------------------------------
void CBeebRunner::RenderBBCFrame(void)
{
    u32 *pFB = (u32 *)(uintptr_t)m_pFrameBuffer->GetBuffer();
    if (!pFB)
        return;

    const u32 fbPitchWords = m_pFrameBuffer->GetPitch() / 4;
    const u32 fbWidth = m_pFrameBuffer->GetWidth();
    const u32 fbHeight = m_pFrameBuffer->GetHeight();

    const u32 startX = (fbWidth > BBC_BUF_W) ? (fbWidth - BBC_BUF_W) / 2 : 0;
    const u32 startY = (fbHeight > BBC_BUF_H) ? (fbHeight - BBC_BUF_H) / 2 : 0;

    for (int line = 0; line < BBC_BUF_H; line++)
    {
        bbc_video_render_row(&m_Machine.video, line, BBC_BUF_H, m_RowBuf, BBC_BUF_W);

        u32 *pDstRow = pFB + ((startY + line) * fbPitchWords) + startX;
        for (int x = 0; x < BBC_BUF_W; x++)
        {
            pDstRow[x] = s_bbc_palette_argb[m_RowBuf[x] & 7];
        }
    }
}

// ------------------------------------------------------------
// Multi-core Run Loop
// ------------------------------------------------------------
void CBeebRunner::Run(unsigned nCore)
{
    // Core 1: Dedicated BBC 6502 Emulator Loop (2 MHz clock pacing)
    if (nCore == 1)
    {
        while (!m_bCoreInitDone)
        {
            asm volatile("dmb sy" ::: "memory");
        }

        CLogger::Get()->Write(FromBeeb, LogNotice, "Core 1: BBC 6502 CPU Task gestart (2 MHz)");

        u64 nNextFrameTime = m_pTimer->GetClockTicks64();

        while (!m_bShutdown)
        {
            u64 nCurrentTime = m_pTimer->GetClockTicks64();
            if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
            {
                nNextFrameTime = nCurrentTime;
            }

            // 2 MHz = 40.000 cycles per 50 Hz (20 ms) frame
            int cyclesBudget = 40000;
            while (cyclesBudget > 0)
            {
                cyclesBudget -= bbc_machine_step(&m_Machine);
            }

            nNextFrameTime += FRAME_TIME_US;
            while (m_pTimer->GetClockTicks64() < nNextFrameTime)
            {
                asm volatile("yield");
            }
        }
        return;
    }

    // Core 0: Master (USB HID, Video VSYNC rendering en I/O)
    if (nCore == 0)
    {
        CLogger::Get()->Write(FromBeeb, LogNotice, "Core 0: Beeb Master Loop gestart (USB + Video)");
        u64 nNextFrameTime = m_pTimer->GetClockTicks64();
        static u64 nLastLEDTime = 0;

        while (!m_bShutdown)
        {
            // 1. USB Toetsenbord detectie
            if (m_pKeyboard == nullptr)
            {
                CDevice *pDev = m_pDeviceNameService->GetDevice("ukbd1", FALSE);
                if (pDev != nullptr)
                {
                    m_pKeyboard = (CUSBKeyboardDevice *)pDev;
                    m_pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw);
                    m_pKeyboard->RegisterRemovedHandler(KeyboardRemovedHandler);
                    CLogger::Get()->Write(FromBeeb, LogNotice, "USB Toetsenbord succesvol gekoppeld!");
                    m_bKeyboardReady = TRUE;
                }
            }

            // 2. Keyboard LEDs
            u64 nCurrentTime = m_pTimer->GetClockTicks64();
            if (m_pKeyboard != nullptr && (nCurrentTime - nLastLEDTime >= 100000))
            {
                nLastLEDTime = nCurrentTime;
                m_pKeyboard->UpdateLEDs();
            }

            // 3. 50 Hz Frame Render tick
            if (nCurrentTime >= nNextFrameTime)
            {
                if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
                {
                    nNextFrameTime = nCurrentTime;
                }

                RenderBBCFrame();
                nNextFrameTime += FRAME_TIME_US;
            }

            asm volatile("yield");
        }
    }
}

// ------------------------------------------------------------
// Keyboard Handlers
// ------------------------------------------------------------
void CBeebRunner::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    if (!s_pThis)
        return;

    static uint8_t s_lastHID[6] = {0};
    static BbcKeyPos s_lastPos[6] = {{0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}};

    bbc_machine_t *m = &s_pThis->m_Machine;

    // Directe modifiers (Shift & Ctrl)
    bool bShift = (ucModifiers & 0x22) != 0;
    bool bCtrl = (ucModifiers & 0x11) != 0;
    bbc_machine_key_event(m, 0, 0, bShift); // Shift = Row 0, Col 0
    bbc_machine_key_event(m, 1, 0, bCtrl);  // Ctrl  = Row 1, Col 0

    // KeyDown detectie
    for (int i = 0; i < 6; i++)
    {
        uint8_t hid = RawKeys[i] & 0xFF;
        if (!hid)
            continue;

        bool alreadyPressed = false;
        for (int j = 0; j < 6; j++)
        {
            if (s_lastHID[j] == hid)
            {
                alreadyPressed = true;
                break;
            }
        }

        if (!alreadyPressed)
        {
            BbcKeyPos pos = ConvertHIDToBBCKey(hid);
            if (pos.row != 0xFF)
            {
                bbc_machine_key_event(m, pos.row, pos.col, true);
                s_lastPos[i] = pos;
                s_lastHID[i] = hid;
            }
        }
    }

    // KeyUp detectie
    for (int i = 0; i < 6; i++)
    {
        uint8_t oldHID = s_lastHID[i];
        if (!oldHID)
            continue;

        bool stillPressed = false;
        for (int j = 0; j < 6; j++)
        {
            if (RawKeys[j] == oldHID)
            {
                stillPressed = true;
                break;
            }
        }

        if (!stillPressed)
        {
            BbcKeyPos pos = s_lastPos[i];
            if (pos.row != 0xFF)
            {
                bbc_machine_key_event(m, pos.row, pos.col, false);
            }
            s_lastPos[i] = {0xFF, 0xFF};
            s_lastHID[i] = 0;
        }
    }
}

void CBeebRunner::KeyboardRemovedHandler(CDevice *pDevice, void *pContext)
{
    if (s_pThis != nullptr)
    {
        CLogger::Get()->Write(FromBeeb, LogNotice, "USB Toetsenbord ontkoppeld.");
        s_pThis->m_pKeyboard = nullptr;
        s_pThis->m_bKeyboardReady = FALSE;
    }
}