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
    if (!ctx || !ctx->isOpen)
        return -1;
    if (track >= BBC_TRACKS)
        return -1;
    if (sector >= BBC_SECTORS_PER_TRACK)
        return -1;
    if (!ctx->is_dsd && side != 0)
        return -1;

    FSIZE_t offset;
    if (ctx->is_dsd)
    {
        offset = ((track * 2 + side) * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    }
    else
    {
        offset = (track * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    }

    if (f_lseek(&ctx->file, offset) != FR_OK)
        return -1;

    UINT bytesRead = 0;
    if (f_read(&ctx->file, buf, BBC_SECTOR_SIZE, &bytesRead) != FR_OK || bytesRead != BBC_SECTOR_SIZE)
    {
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
    if (!ctx || !ctx->isOpen || ctx->read_only)
        return -1;
    if (track >= BBC_TRACKS)
        return -1;
    if (sector >= BBC_SECTORS_PER_TRACK)
        return -1;
    if (!ctx->is_dsd && side != 0)
        return -1;
    if (len != BBC_SECTOR_SIZE)
        return -1;

    FSIZE_t offset;
    if (ctx->is_dsd)
    {
        offset = ((track * 2 + side) * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    }
    else
    {
        offset = (track * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    }

    if (f_lseek(&ctx->file, offset) != FR_OK)
        return -1;

    UINT bytesWritten = 0;
    if (f_write(&ctx->file, buf, BBC_SECTOR_SIZE, &bytesWritten) != FR_OK || bytesWritten != BBC_SECTOR_SIZE)
    {
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

/* ------------------------------------------------------------
// USB HID naar BBC Keyboard Matrix
// ------------------------------------------------------------
col 0: 1        2     3    4    5    6    7
Row 0: SHIFT    CTRL  Q    1    CAPS 0    TAB ESC
Row 1: 3        W     2    A    S    Z    (no key)
Row 2: 4        E     R    D    F    X    C
Row 3: 5        T     6    G    H    V    B
Row 4: F4       7     8    Y    J    N    SPACE
Row 5: F5       I     O    U    K    M    ,
Row 6: F6       9     0    P    L    ;    .
Row 7: F7       -     =    @    :    /    (no key)
Row 8: F1       F2    F3   COPY UP   LEFT DELETE
Row 9: F8       F9    F0   RETURN RIGHT DOWN (no key)

*/

BbcKeyPos CBeebRunner::ConvertHIDToBBCKey(uint8_t hid)
{
    switch (hid)
    {
    // -------------------------
    // Row 0 (BBC row 0 → HID col,row swapped)
    // -------------------------
    case 0x14:
        return {2, 0}; // Q
    case 0x1E:
        return {3, 0}; // 1
    case 0x39:
        return {4, 0}; // Caps Lock
   // case 0x27:
   //     return {5, 0}; // 0
    case 0x2B:
        return {6, 0}; // Tab
    case 0x29:
        return {7, 0}; // Escape

    // -------------------------
    // Row 1
    // -------------------------
    case 0x20:
        return {1, 1}; // 3
    case 0x1A:
        return {2, 1}; // W
    case 0x1F:
        return {3, 1}; // 2
    case 0x04:
        return {4, 1}; // A
    case 0x16:
        return {5, 1}; // S
    case 0x1D:
        return {6, 1}; // Z

    // -------------------------
    // Row 2
    // -------------------------
    case 0x21:
        return {1, 2}; // 4
    case 0x08:
        return {2, 2}; // E
    case 0x15:
        return {3, 2}; // R
    case 0x07:
        return {4, 2}; // D
    case 0x09:
        return {5, 2}; // F
    case 0x1B:
        return {6, 2}; // X
    case 0x06:
        return {7, 2}; // C

    // -------------------------
    // Row 3
    // -------------------------
    case 0x22:
        return {1, 3}; // 5
    case 0x17:
        return {2, 3}; // T
    case 0x23:
        return {3, 3}; // 6
    case 0x0A:
        return {4, 3}; // G
    case 0x0B:
        return {5, 3}; // H
    case 0x19:
        return {6, 3}; // V
    case 0x05:
        return {7, 3}; // B

    // -------------------------
    // Row 4
    // -------------------------
    case 0x3D:
        return {0, 4}; // F4
    case 0x24:
        return {1, 4}; // 7
    case 0x25:
        return {2, 4}; // 8
    case 0x1C:
        return {3, 4}; // Y
    case 0x0D:
        return {4, 4}; // J
    case 0x11:
        return {5, 4}; // N
    case 0x2C:
        return {6, 4}; // Space

    // ENTER (jouw speciale mapping)
    case 0x28:
    case 0x58:
        return {9, 4};

    // -------------------------
    // Row 5
    // -------------------------
    case 0x3E:
        return {0, 5}; // F5
    case 0x0C:
        return {1, 5}; // I
    case 0x12:
        return {2, 5}; // O
    case 0x18:
        return {3, 5}; // U
    case 0x0E:
        return {4, 5}; // K
    case 0x10:
        return {5, 5}; // M
    case 0x36:
        return {6, 5}; // ,

    // -------------------------
    // Row 6
    // -------------------------
    case 0x3F:
        return {0, 6}; // F6
    case 0x26:
        return {1, 6}; // 9
    case 0x27:
        return {2, 6}; // 0
    case 0x13:
        return {3, 6}; // P
    case 0x0F:
        return {4, 6}; // L
    case 0x33:
        return {5, 6}; // ;
    case 0x37:
        return {6, 6}; // .

    // -------------------------
    // Row 7
    // -------------------------
    case 0x40:
        return {0, 7}; // F7
    case 0x2D:
        return {1, 7}; // -
    case 0x2E:
        return {2, 7}; // =
    case 0x34:
        return {4, 7}; // ' (Shift → ")
    case 0x38:
        return {5, 7}; // /

    // -------------------------
    // Row 8
    // -------------------------
    case 0x3A:
        return {0, 8}; // F1
    case 0x3B:
        return {1, 8}; // F2
    case 0x3C:
        return {2, 8}; // F3
    case 0x2F:
        return {3, 8}; // COPY ([)
    case 0x52:
    case 0x60:
        return {4, 8}; // UP
    case 0x4C:
    case 0x2A:
        return {7, 8}; // DELETE / Backspace

    // -------------------------
    // Row 9
    // -------------------------
    case 0x4D:
        return {0, 9}; // F8
    case 0x43:
        return {1, 9}; // F9
    case 0x45:
        return {2, 9}; // F0 (Break)
    case 0x4F:
    case 0x5E:
        return {4, 9}; // RIGHT
    case 0x65:
        return {5, 9}; // DOWN

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
    /* Core 1: Dedicated BBC 6502 Emulator Loop (2 MHz clock pacing)
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
*/
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
void CBeebRunner::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    if (!s_pThis)
        return;

    static uint8_t s_activeHID[6] = {0};
    static BbcKeyPos s_activePos[6] = {
        {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}};

    bbc_machine_t *m = &s_pThis->m_Machine;

    // 1. Modifiers (Row 0, Col 0 voor Shift en Row 0, Col 1 voor Ctrl)
    bool bShift = (ucModifiers & 0x22) != 0;
    bool bCtrl = (ucModifiers & 0x11) != 0;
    bbc_machine_key_event(m, 0, 0, bShift); // Shift = Row 0, Col 0
    bbc_machine_key_event(m, 0, 1, bCtrl);  // Ctrl  = Row 0, Col 1

    // 2. KEY-UP detectie: toetsen die niet langer ingedrukt zijn
    for (int i = 0; i < 6; i++)
    {
        uint8_t activeHID = s_activeHID[i];
        if (activeHID == 0)
            continue;

        bool stillPressed = false;
        for (int k = 0; k < 6; k++)
        {
            if ((RawKeys[k] & 0xFF) == activeHID)
            {
                stillPressed = true;
                break;
            }
        }

        if (!stillPressed)
        {
            BbcKeyPos pos = s_activePos[i];
            if (pos.row != 0xFF && pos.col != 0xFF)
            {
                bbc_machine_key_event(m, pos.row, pos.col, false);
                CLogger::Get()->Write(FromBeeb, LogNotice, "Key UP: Row=%d Col=%d (HID 0x%02X)", pos.row, pos.col, activeHID);
            }
            s_activeHID[i] = 0;
            s_activePos[i] = {0xFF, 0xFF};
        }
    }

    // 3. KEY-DOWN detectie: toetsen die nieuw zijn ingedrukt
    for (int k = 0; k < 6; k++)
    {
        uint8_t hid = RawKeys[k] & 0xFF;
        if (hid == 0)
            continue;

        // F12 onderscheppen als hardware Reset / Break
        if (hid == 0x45)
        {
            bool alreadyDown = false;
            for (int i = 0; i < 6; i++)
            {
                if (s_activeHID[i] == 0x45)
                {
                    alreadyDown = true;
                    break;
                }
            }
            if (!alreadyDown)
            {
                CLogger::Get()->Write(FromBeeb, LogNotice, "F12 ingedrukt: Hardware BREAK");
                bbc_machine_break(m, bShift);
                // Registreer in activeHID zodat hij niet continu triggert zolang hij ingedrukt blijft
                for (int i = 0; i < 6; i++)
                {
                    if (s_activeHID[i] == 0)
                    {
                        s_activeHID[i] = 0x45;
                        s_activePos[i] = {0xFF, 0xFF};
                        break;
                    }
                }
            }
            continue;
        }

        bool alreadyTracked = false;
        for (int i = 0; i < 6; i++)
        {
            if (s_activeHID[i] == hid)
            {
                alreadyTracked = true;
                break;
            }
        }

        if (!alreadyTracked)
        {
            BbcKeyPos pos = ConvertHIDToBBCKey(hid);
            if (pos.row != 0xFF && pos.col != 0xFF)
            {
                int freeSlot = -1;
                for (int i = 0; i < 6; i++)
                {
                    if (s_activeHID[i] == 0)
                    {
                        freeSlot = i;
                        break;
                    }
                }

                if (freeSlot >= 0)
                {
                    s_activeHID[freeSlot] = hid;
                    s_activePos[freeSlot] = pos;
                }

                bbc_machine_key_event(m, pos.row, pos.col, true);
                CLogger::Get()->Write(FromBeeb, LogNotice, "Key DOWN: Row=%d Col=%d (HID 0x%02X)", pos.row, pos.col, hid);
            }
        }
    }

    // 4. Data cache flush/barrier voor Core 1
    asm volatile("dmb sy" ::: "memory");
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