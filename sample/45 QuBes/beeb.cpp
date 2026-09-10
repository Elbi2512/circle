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
extern "C" void bbc_debug_log(const char *msg, unsigned val1, unsigned val2)
{
    CLogger::Get()->Write(FromBeeb, LogNotice, "%s %u, %u", msg, val1, val2);
}

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

/* -----------------------------------------------------------------------
 * BBC Micro Model B keyboard matrix mapping
 *
 * The BBC keyboard is a 10-row × 8-column matrix.
 *
 *  row\col  0       1       2       3       4       5       6       7
 *  0        SHIFT   Q       F0      1       CAPS    SHIFTLK TAB     ESCAPE
 *  1        CTRL    3       W       2       A       S       Z       (none)
 *  2        (none)  4       E       R       D       F       X       C
 *  3        (none)  5       T       6       G       H       V       B
 *  4        (none)  F4      7       8       Y       J       N       SPACE
 *  5        (none)  F5      I       O       U       K       M       COMMA
 *  6        (none)  F6      9       0       P       L       (none)  PERIOD
 *  7        (none)  F7      MINUS   EQUALS  AT      COLON   SLASH   (none)
 *  8        F1      F2      F3      BREAK   (none)  UP      (none)  DELETE
 *  9        (none)  (none)  (none)  COPY    (none)  RIGHT   RETURN  (none)
 * ----------------------------------------------------------------------- */

BbcKeyPos CBeebRunner::ConvertHIDToBBCKey(uint8_t hid)
{
 //   CLogger::Get()->Write(FromBeeb, LogNotice, "ConvertHID: HID 0x%02X (%u)", hid, hid);

    BbcKeyPos pos = {0xFF, 0xFF};
switch (hid)
{
    /* --- Col 1 --- */
    case VK_Q:         pos.row = 0; pos.col = 1; break;
    case VK_3:         pos.row = 1; pos.col = 1; break;
    case VK_4:         pos.row = 2; pos.col = 1; break;
    case VK_5:         pos.row = 3; pos.col = 1; break;
    case VK_8:         pos.row = 5; pos.col = 1; break;
    case VK_MINUS:     pos.row = 7; pos.col = 1; break;
    case VK_LEFT:      pos.row = 9; pos.col = 1; break;
 

    /* --- Col 2 --- */
    case VK_W:         pos.row = 1; pos.col = 2; break;
    case VK_E:         pos.row = 2; pos.col = 2; break;
    case VK_T:         pos.row = 3; pos.col = 2; break;
    case VK_7:         pos.row = 4; pos.col = 2; break;
    case VK_I:         pos.row = 5; pos.col = 2; break;
    case VK_9:         pos.row = 6; pos.col = 2; break;
    case VK_0:         pos.row = 7; pos.col = 2; break;
    case VK_DOWN:      pos.row = 9; pos.col = 2; break;

    /* --- Col 3 --- */
    case VK_1:         pos.row = 0; pos.col = 3; break;
    case VK_2:         pos.row = 1; pos.col = 3; break;
    case VK_D:         pos.row = 2; pos.col = 3; break;
    case VK_R:         pos.row = 3; pos.col = 3; break;
    case VK_6:         pos.row = 4; pos.col = 3; break;
    case VK_U:         pos.row = 5; pos.col = 3; break; // jouw comment liet 2 varianten zien
    case VK_O:         pos.row = 6; pos.col = 3; break;
    case VK_P:         pos.row = 7; pos.col = 3; break;
    case VK_LBRACKET:  pos.row = 8; pos.col = 3; break;
    case VK_UP:        pos.row = 9; pos.col = 3; break;

    /* --- Col 4 --- */
    case VK_CAPSLOCK:  pos.row = 0; pos.col = 4; break;
    case VK_A:         pos.row = 1; pos.col = 4; break;
    case VK_X:         pos.row = 2; pos.col = 4; break;
    case VK_F:         pos.row = 3; pos.col = 4; break;
    case VK_Y:         pos.row = 4; pos.col = 4; break;
    case VK_J:         pos.row = 5; pos.col = 4; break;
    case VK_K:         pos.row = 6; pos.col = 4; break;
    case VK_AT:        pos.row = 7; pos.col = 4; break;
    case VK_COLON:     pos.row = 8; pos.col = 4; break;
    case VK_RETURN:    pos.row = 9; pos.col = 4; break;

    /* --- Col 5 --- */
    case VK_S:         pos.row = 1; pos.col = 5; break;
    case VK_C:         pos.row = 2; pos.col = 5; break;
    case VK_G:         pos.row = 3; pos.col = 5; break;
    case VK_H:         pos.row = 4; pos.col = 5; break;
    case VK_N:         pos.row = 5; pos.col = 5; break;
    case VK_L:         pos.row = 6; pos.col = 5; break;
    case VK_SEMICOLON: pos.row = 7; pos.col = 5; break;
    case VK_RBRACKET:  pos.row = 8; pos.col = 5; break;
    case VK_DELETE:    pos.row = 9; pos.col = 5; break;

    /* --- Col 6 --- */
    case VK_TAB:       pos.row = 0; pos.col = 6; break;
    case VK_Z:         pos.row = 1; pos.col = 6; break;
    case VK_SPACE:     pos.row = 2; pos.col = 6; break;
    case VK_V:         pos.row = 3; pos.col = 6; break;
    case VK_B:         pos.row = 4; pos.col = 6; break;
    case VK_M:         pos.row = 5; pos.col = 6; break;
    case VK_COMMA:     pos.row = 6; pos.col = 6; break;
    case VK_PERIOD:    pos.row = 7; pos.col = 6; break;
    case VK_SLASH:     pos.row = 8; pos.col = 6; break;
    case VK_END:       pos.row = 9; pos.col = 6; break;
  
    /* --- Col 7 --- */
    case VK_ESCAPE:    pos.row = 0; pos.col = 7; break;
    case VK_BACKSLASH: pos.row = 8; pos.col = 7; break;
    case VK_RIGHT:     pos.row = 9; pos.col = 7; break;
    //case VK_COMMA:     pos.row = 5; pos.col = 7; break;
    //case VK_PERIOD:    pos.row = 6; pos.col = 7; break;

    default:
        pos.row = 0xFF;
        pos.col = 0xFF;
        break;
}

    return pos;
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

    // 1. Modifiers (Shift en Ctrl)
    // In bbc_machine.c verwacht bbc_machine_key_event(m, row, col, pressed)
    // Row 0, Col 0 = Shift | Row 0, Col 1 = Ctrl
    bool bShift = (ucModifiers & 0x22) != 0;
    bool bCtrl  = (ucModifiers & 0x11) != 0;
    bbc_machine_key_event(m, 0, 0, bShift);
    bbc_machine_key_event(m, 0, 1, bCtrl);

    // 2. KEY-UP detectie: toetsen die niet langer in het HID-rapport staan
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
                // Let op: pos.col wordt row (0..7) en pos.row wordt col (0..9) voor de emulator
                bbc_machine_key_event(m, pos.col, pos.row, false);
                CLogger::Get()->Write(FromBeeb, LogNotice, "Key UP: Row=%d Col=%d (HID 0x%02X)", pos.row, pos.col, activeHID);
            }
            s_activeHID[i] = 0;
            s_activePos[i] = {0xFF, 0xFF};
        }
    }

    // 3. KEY-DOWN detectie: nieuwe toetsen in RawKeys
    for (int k = 0; k < 6; k++)
    {
        uint8_t hid = RawKeys[k] & 0xFF;
        if (hid == 0)
            continue;

        // F12 / Break afhandeling
        if (hid == 0x45) // VK_F12
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
                   // CLogger::Get()->Write(FromBeeb, LogNotice, "Freeslot: %u | HID 0x%02X -> Row=%d Col=%d", freeSlot, hid, pos.row, pos.col);
                }

                // Let op: pos.col wordt row (0..7) en pos.row wordt col (0..9) voor de emulator
                bbc_machine_key_event(m, pos.col, pos.row, true);
                // CLogger::Get()->Write(FromBeeb, LogNotice, "Key DOWN: HID 0x%02X -> Row=%d Col=%d", hid, pos.row, pos.col);
            }
        }
    }

    // 4. Data cache flush voor Core 1
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