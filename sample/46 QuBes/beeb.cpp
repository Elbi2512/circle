#include "beeb.h"
#include <circle/util.h>
#include <circle/string.h>
#include "roms.h"
#include "bbc_cpu.h"
#include "bbc_machine.h"
#include "vrEmu6502.h"
//#include "sn76489.h"
// #include "via6522.h"
//#include "bbc_memory.h"
//#include "bbc_tape.h"

/* --------------------------------------------------------------------------
 * Framebuffer dimensions (native BBC Micro visible area)
 * -------------------------------------------------------------------------- */
#define BBC_FB_WIDTH 640
#define BBC_FB_HEIGHT 256

/* Set to 1 to bypass the whole BBC/CRTC/Teletext pipeline and just draw a
 * static "HELLO WORLD" text directly into the HDMI framebuffer. Used to
 * verify the display/scaling path independently of the emulator. */
#define BBC_VIDEO_TEST_PATTERN 0

/* Set to 1 to periodically dump CPU/CRTC/video-RAM state to the debug log
 * (once per second), instead of only relying on the HDMI screen. */
#define BBC_VIDEO_DEBUG_LOG 1

/* Set to 1 to skip mounting/auto-booting a disk image entirely, to check
 * whether the emulator reaches a normal BASIC prompt without DFS/FDC involved. */
#define BBC_SKIP_DISK_BOOT 1

static const char FromBeeb[] = "beeb";

/* Counters to detect a disk boot retry loop (incremented in DiskReadSector/
 * DiskWriteSector below, read from DumpEmulatorDebug). */
static volatile uint32_t s_diskReadCalls  = 0;
static volatile uint32_t s_diskReadFails  = 0;
static volatile uint32_t s_diskWriteCalls = 0;

#if BBC_VIDEO_TEST_PATTERN
extern "C" const uint8_t saa5050_builtin_rom[96][10][6];

static void DrawTestPattern(u32 *pFB, u32 fbPitchWords, u32 fbWidth, u32 fbHeight)
{
    static const char msg[] = "HELLO WORLD 12345";
    const int scale = 4;
    const int charW = 6 * scale;
    const int startX = 40;
    const int startY = 40;

    for (u32 y = 0; y < fbHeight; y++)
    {
        u32 color = (y < fbHeight / 2) ? 0xFF0000FF : 0xFF008000; /* blue / green split */
        for (u32 x = 0; x < fbWidth; x++)
        {
            pFB[y * fbPitchWords + x] = color;
        }
    }

    for (size_t i = 0; i < sizeof(msg) - 1; i++)
    {
        char c = msg[i];
        if (c < 0x20 || c >= 0x20 + 96)
            continue;
        int idx = c - 0x20;

        for (int row = 0; row < 10; row++)
        {
            for (int col = 0; col < 6; col++)
            {
                if (!saa5050_builtin_rom[idx][row][col])
                    continue;

                for (int sy = 0; sy < scale; sy++)
                {
                    for (int sx = 0; sx < scale; sx++)
                    {
                        int px = startX + (int)i * charW + col * scale + sx;
                        int py = startY + row * scale + sy;
                        if (px >= 0 && px < (int)fbWidth && py >= 0 && py < (int)fbHeight)
                        {
                            pFB[py * fbPitchWords + px] = 0xFFFFFFFF;
                        }
                    }
                }
            }
        }
    }

    /* Border box so scaling/centering issues are obvious too */
    for (u32 x = 0; x < fbWidth; x++)
    {
        pFB[0 * fbPitchWords + x] = 0xFFFF0000;
        pFB[(fbHeight - 1) * fbPitchWords + x] = 0xFFFF0000;
    }
    for (u32 y = 0; y < fbHeight; y++)
    {
        pFB[y * fbPitchWords + 0] = 0xFFFF0000;
        pFB[y * fbPitchWords + (fbWidth - 1)] = 0xFFFF0000;
    }
}
#endif

/* --------------------------------------------------------------------------
 * Debug dump: reroute emulator video/CPU state to the Circle debug log
 * instead of (or in addition to) the HDMI screen, for headless diagnosis.
 * -------------------------------------------------------------------------- */
static void DumpEmulatorDebug(bbc_machine_t *m)
{
    const mc6845_t *crtc = &m->video.crtc;
    uint16_t start_addr = mc6845_get_start_addr(crtc);
    bool teletext = m->video.ula.teletext_mode;
    uint16_t pc = m->cpu ? vrEmu6502GetPC(m->cpu) : 0;
    uint8_t acc = m->cpu ? vrEmu6502GetAcc(m->cpu) : 0;
    uint8_t xr = m->cpu ? vrEmu6502GetX(m->cpu) : 0;
    uint8_t yr = m->cpu ? vrEmu6502GetY(m->cpu) : 0;
    uint8_t sp = m->cpu ? vrEmu6502GetStackPointer(m->cpu) : 0;
    uint8_t opcode = m->cpu ? vrEmu6502GetCurrentOpcode(m->cpu) : 0;
    uint8_t reset_lo = m->mem.os_rom[0x3FFC];
    uint8_t reset_hi = m->mem.os_rom[0x3FFD];

    /* Prove the CPU is actually making forward progress (not stuck in a
     * tight 1-2 instruction loop) by comparing against the previous sample. */
    static uint64_t s_lastCyc = 0;
    static uint16_t s_lastPc  = 0xFFFF;
    static uint16_t s_minPc   = 0xFFFF;
    static uint16_t s_maxPc   = 0x0000;
    uint64_t deltaCyc = m->total_cycles - s_lastCyc;
    bool pcMoved = (pc != s_lastPc);
    if (pc < s_minPc) s_minPc = pc;
    if (pc > s_maxPc) s_maxPc = pc;
    s_lastCyc = m->total_cycles;
    s_lastPc  = pc;

    CLogger::Get()->Write(FromBeeb, LogNotice,
                          "DBG pc=%04X op=%02X a=%02X x=%02X y=%02X sp=%02X cyc=%llu (+%llu/s) resetVec=%02X%02X",
                          pc, opcode, acc, xr, yr, sp, (unsigned long long)m->total_cycles,
                          (unsigned long long)deltaCyc, reset_hi, reset_lo);

    CLogger::Get()->Write(FromBeeb, LogNotice,
                          "DBG progress: pcMoved=%d pcRange=[%04X..%04X]", pcMoved, s_minPc, s_maxPc);

    CLogger::Get()->Write(FromBeeb, LogNotice,
                          "DBG teletext=%d ulaCtrl=%02X R9=%u startAddr=%04X vCtr=%u rCtr=%u",
                          teletext, m->video.ula.control, crtc->max_scanline_addr,
                          start_addr, crtc->v_ctr, crtc->r_ctr);

    CLogger::Get()->Write(FromBeeb, LogNotice,
                          "DBG disk reads=%u fails=%u writes=%u",
                          s_diskReadCalls, s_diskReadFails, s_diskWriteCalls);

    static uint32_t s_lastIrqCount = 0;
    uint32_t irqDelta = m->dbg_irq_count - s_lastIrqCount;
    s_lastIrqCount = m->dbg_irq_count;
    CLogger::Get()->Write(FromBeeb, LogNotice,
                          "DBG sysvia irq_edges=%u (+%u) ier=%02X ifr=%02X acr=%02X t1lat=%04X t1cnt=%04X t2lat=%04X t2cnt=%04X",
                          m->dbg_irq_count, irqDelta,
                          m->sysvia.via.ier, m->sysvia.via.ifr, m->sysvia.via.acr,
                          m->sysvia.via.t1.latch, m->sysvia.via.t1.counter,
                          m->sysvia.via.t2.latch, m->sysvia.via.t2.counter);

    CLogger::Get()->Write(FromBeeb, LogNotice,
                          "DBG sysvia pcr=%02X ora_reads=%u t1cl_reads=%u ifr_writes=%u pcr_writes=%u",
                          m->sysvia.via.pcr, m->sysvia.via.dbg_ora_reads,
                          m->sysvia.via.dbg_t1cl_reads, m->sysvia.via.dbg_ifr_writes,
                          m->sysvia.via.dbg_pcr_writes);

    CLogger::Get()->Write(FromBeeb, LogNotice,
                          "DBG mode7 writes count=%u lastAddr=%04X lastVal=%02X fromPC=%04X x=%02X y=%02X",
                          m->dbg_mode7_write_count, m->dbg_mode7_last_addr,
                          m->dbg_mode7_last_val, m->dbg_mode7_last_pc,
                          m->dbg_mode7_last_x, m->dbg_mode7_last_y);

    CLogger::Get()->Write(FromBeeb, LogNotice,
                          "DBG mode7 zp @write: $D8=%02X $D9=%02X $F0=%02X $88=%02X $DE=%02X $DF=%02X",
                          m->dbg_mode7_zp_d8, m->dbg_mode7_zp_d9,
                          m->dbg_mode7_zp_f0, m->dbg_mode7_zp_88,
                          m->dbg_mode7_zp_de, m->dbg_mode7_zp_df);

    /* One-time raw OS ROM byte dump around the addresses seen doing the
     * repeated writes, so the actual 6502 instructions can be inspected. */
    static bool s_romDumped = false;
    if (!s_romDumped)
    {
        s_romDumped = true;
        static const uint16_t regions[] = { 0xCEC0, 0xCFC0, 0xC4C0, 0xAEE0, 0xE0A0, 0xC4A0 };
        for (unsigned r = 0; r < sizeof(regions) / sizeof(regions[0]); r++)
        {
            uint16_t base = regions[r];
            char hex[3 * 32 + 1];
            for (int i = 0; i < 32; i++)
            {
                uint8_t b = m->mem.os_rom[(base + i) - 0xC000];
                CString h;
                h.Format("%02X ", b);
                strncpy(&hex[i * 3], (const char *)h, 3);
            }
            hex[96] = '\0';
            CLogger::Get()->Write(FromBeeb, LogNotice, "DBG rom @%04X: %s", base, hex);
        }
    }

    if (teletext)
    {
        uint32_t base = (start_addr & 0x0800u) ? 0x7C00u : 0x3C00u;

        /* MOS scrolls Mode 7 by advancing the CRTC start address within the
         * 1K ring buffer, so the actually-visible top row is offset by
         * (start_addr & 0x3FF), not byte 0 of the 1K region. */
        uint32_t top_row = base + (start_addr & 0x03FFu);

        char line[41];
        for (int i = 0; i < 40; i++)
        {
            uint8_t b = m->mem.main_ram[(top_row + (uint32_t)i) & 0x7FFF] & 0x7F;
            line[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
        }
        line[40] = '\0';
        CLogger::Get()->Write(FromBeeb, LogNotice, "DBG top row @%04X: \"%s\"", top_row, line);

        /* Count how much of the 1K Mode 7 buffer is still the 0xFF fill
         * value vs. actually written by MOS, to see how much real content exists. */
        int ff_count = 0;
        for (int i = 0; i < 1024; i++)
        {
            if (m->mem.main_ram[(base + (uint32_t)i) & 0x7FFF] == 0xFF)
                ff_count++;
        }
        CLogger::Get()->Write(FromBeeb, LogNotice, "DBG mode7 buf @%04X: %d/1024 bytes still 0xFF", base, ff_count);
    }
}

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

    s_diskReadCalls++;

    bbc_circle_disk_ctx_t *ctx = (bbc_circle_disk_ctx_t *)user_ctx;
    if (!ctx || !ctx->isOpen)
    {
        s_diskReadFails++;
        *len = 0;
        return -1;
    }

    // DFS gebruikt strikt sectoren 0 t/m 9 per track (geen 1-based conversies!)
    if (track >= BBC_TRACKS || sector >= BBC_SECTORS_PER_TRACK)
    {
        // Buiten bereik: FDC hoort Record Not Found te krijgen
        s_diskReadFails++;
        *len = 0;
        return -1;
    }

    if (!ctx->is_dsd && side != 0)
    {
        s_diskReadFails++;
        *len = 0;
        return -1;
    }

    FSIZE_t offset;
    if (ctx->is_dsd)
    {
        offset = ((track * 2 + (side & 1)) * BBC_SECTORS_PER_TRACK + sector) * BBC_SECTOR_SIZE;
    }
    else
    {
        offset = ((track * BBC_SECTORS_PER_TRACK) + sector) * BBC_SECTOR_SIZE;
    }

    if (f_lseek(&ctx->file, offset) != FR_OK)
    {
        s_diskReadFails++;
        *len = 0;
        return -1;
    }

    UINT bytesRead = 0;
    FRESULT fr = f_read(&ctx->file, buf, BBC_SECTOR_SIZE, &bytesRead);
    if (fr != FR_OK || bytesRead != BBC_SECTOR_SIZE)
    {
        s_diskReadFails++;
        *len = 0;
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

    s_diskWriteCalls++;

    bbc_circle_disk_ctx_t *ctx = (bbc_circle_disk_ctx_t *)user_ctx;
    if (!ctx || !ctx->isOpen || ctx->read_only)
        return -1;

    // Normaliseer sector: DFS kan 0..9 of 1..10 doorgeven
    uint8_t sec_idx = sector;
    if (sec_idx >= BBC_SECTORS_PER_TRACK)
    {
        sec_idx -= 1; // 10 -> 9 (bij 1-based adressering)
    }

    // Als de FDC voorbij het einde van het spoor probeert te schrijven (bijv. sector 11+)
    if (track >= BBC_TRACKS || sec_idx >= BBC_SECTORS_PER_TRACK)
    {
        return -1;
    }

    if (!ctx->is_dsd && side != 0)
        return -1;

    if (len != BBC_SECTOR_SIZE)
        return -1;

    FSIZE_t offset;
    if (ctx->is_dsd)
    {
        offset = ((track * 2 + (side & 1)) * BBC_SECTORS_PER_TRACK + sec_idx) * BBC_SECTOR_SIZE;
    }
    else
    {
        offset = ((track * BBC_SECTORS_PER_TRACK) + sec_idx) * BBC_SECTOR_SIZE;
    }

    if (f_lseek(&ctx->file, offset) != FR_OK)
    {
        CLogger::Get()->Write(FromDisk, LogError, "Write seek fout bij offset %u", (unsigned)offset);
        return -1;
    }

    UINT bytesWritten = 0;
    FRESULT fr = f_write(&ctx->file, buf, BBC_SECTOR_SIZE, &bytesWritten);
    if (fr != FR_OK || bytesWritten != BBC_SECTOR_SIZE)
    {
        CLogger::Get()->Write(FromDisk, LogError, "Schrijffout: fr=%d bytes=%u (verwacht %u)", fr, bytesWritten, BBC_SECTOR_SIZE);
        return -1;
    }

    f_sync(&ctx->file);

    CLogger::Get()->Write(FromDisk, LogNotice, "Disk Write OK: Trk=%u Sec=%u (norm=%u, Offset 0x%05X)",
                          track, sector, sec_idx, (unsigned)offset);

    return 0;
}

void CBeebRunner::DiskSeek(void * /*user_ctx*/, uint8_t /*drive*/, uint8_t /*track*/)
{
    // No physical head movement needed
}

/* -----------------------------------------------------------------------
 * Mount SD disk image
 *
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
*/

boolean CBeebRunner::MountDisk(const char *pPath, uint8_t nDrive)
{
    if (nDrive >= 2 || pPath == nullptr)
        return FALSE;

    size_t len = strlen(pPath);
    if (len < 4)
        return FALSE;

    const char *ext = &pPath[len - 4];
    bool isDsd = (strcasecmp(ext, ".dsd") == 0);
    bool isSsd = (strcasecmp(ext, ".ssd") == 0);

    if (!isSsd && !isDsd)
    {
        CLogger::Get()->Write(FromBeeb, LogWarning, "Bestand %s is geen .ssd of .dsd", pPath);
        return FALSE;
    }

    if (m_Disk[nDrive].isOpen)
    {
        f_close(&m_Disk[nDrive].file);
        m_Disk[nDrive].isOpen = false;
    }

    m_Disk[nDrive].is_dsd = isDsd;
    m_Disk[nDrive].read_only = false;

    FRESULT res = f_open(&m_Disk[nDrive].file, pPath, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
    if (res != FR_OK)
    {
        // Probeer read-only als read-write niet lukt (bijv. lock op SD)
        res = f_open(&m_Disk[nDrive].file, pPath, FA_READ | FA_OPEN_EXISTING);
        m_Disk[nDrive].read_only = true;
    }

    if (res == FR_OK)
    {
        m_Disk[nDrive].isOpen = true;
        CLogger::Get()->Write(FromBeeb, LogNotice, "Disk %u gekoppeld: %s (%s, %s)",
                              nDrive, pPath, isDsd ? "DSD" : "SSD",
                              m_Disk[nDrive].read_only ? "read-only" : "read-write");

        bbc_machine_mount_disk(&m_Machine, nDrive,
                               DiskReadSector,
                               DiskWriteSector,
                               DiskSeek,
                               &m_Disk[nDrive]);
        return TRUE;
    }

    CLogger::Get()->Write(FromBeeb, LogError, "Fout bij openen van %s (FR=%d)", pPath, res);
    return FALSE;
}

bool CBeebRunner::MountSDDiskImage(void)
{
    // Zoek automatisch naar het eerste .ssd of .dsd bestand op SD:/
    DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, "SD:/");
    if (res != FR_OK)
    {
        CLogger::Get()->Write(FromBeeb, LogWarning, "Kan SD:/ niet openen voor disk scan (FR=%d)", res);
        return false;
    }

    char imgPath[256] = {};

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
            // circle_snprintf(imgPath, sizeof(imgPath), "SD:/%s", fno.fname);
            CString Path;
            Path.Format("SD:/%s", fno.fname);
            strncpy(imgPath, (const char *)Path, sizeof(imgPath) - 1);
            imgPath[sizeof(imgPath) - 1] = '\0';
            break;
        }
    }
    f_closedir(&dir);

    if (imgPath[0] != '\0')
    {
        bool bOK = MountDisk(imgPath, 0);
        if (bOK)
        {
            CLogger::Get()->Write(FromBeeb, LogError, "OK bij koppelen van disk image %s", imgPath);
        }
        else
        {
            CLogger::Get()->Write(FromBeeb, LogError, "Fout bij koppelen van disk image %s", imgPath);
        }
        return bOK;
    }

    CLogger::Get()->Write(FromBeeb, LogNotice, "Geen .ssd of .dsd gevonden op SD:/");
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
    case VK_Q:
        pos.row = 0;
        pos.col = 1;
        break;
    case VK_3:
        pos.row = 1;
        pos.col = 1;
        break;
    case VK_4:
        pos.row = 2;
        pos.col = 1;
        break;
    case VK_5:
        pos.row = 3;
        pos.col = 1;
        break;
    case VK_8:
        pos.row = 5;
        pos.col = 1;
        break;
    case VK_MINUS:
        pos.row = 7;
        pos.col = 1;
        break;
    case VK_LEFT:
        pos.row = 9;
        pos.col = 1;
        break;

    /* --- Col 2 --- */
    case VK_W:
        pos.row = 1;
        pos.col = 2;
        break;
    case VK_E:
        pos.row = 2;
        pos.col = 2;
        break;
    case VK_T:
        pos.row = 3;
        pos.col = 2;
        break;
    case VK_7:
        pos.row = 4;
        pos.col = 2;
        break;
    case VK_I:
        pos.row = 5;
        pos.col = 2;
        break;
    case VK_9:
        pos.row = 6;
        pos.col = 2;
        break;
    case VK_0:
        pos.row = 7;
        pos.col = 2;
        break;
    case VK_DOWN:
        pos.row = 9;
        pos.col = 2;
        break;

    /* --- Col 3 --- */
    case VK_1:
        pos.row = 0;
        pos.col = 3;
        break;
    case VK_2:
        pos.row = 1;
        pos.col = 3;
        break;
    case VK_D:
        pos.row = 2;
        pos.col = 3;
        break;
    case VK_R:
        pos.row = 3;
        pos.col = 3;
        break;
    case VK_6:
        pos.row = 4;
        pos.col = 3;
        break;
    case VK_U:
        pos.row = 5;
        pos.col = 3;
        break; // jouw comment liet 2 varianten zien
    case VK_O:
        pos.row = 6;
        pos.col = 3;
        break;
    case VK_P:
        pos.row = 7;
        pos.col = 3;
        break;
    case VK_LBRACKET:
        pos.row = 8;
        pos.col = 3;
        break;
    case VK_UP:
        pos.row = 9;
        pos.col = 3;
        break;

    /* --- Col 4 --- */
    case VK_CAPSLOCK:
        pos.row = 0;
        pos.col = 4;
        break;
    case VK_A:
        pos.row = 1;
        pos.col = 4;
        break;
    case VK_X:
        pos.row = 2;
        pos.col = 4;
        break;
    case VK_F:
        pos.row = 3;
        pos.col = 4;
        break;
    case VK_Y:
        pos.row = 4;
        pos.col = 4;
        break;
    case VK_J:
        pos.row = 5;
        pos.col = 4;
        break;
    case VK_K:
        pos.row = 6;
        pos.col = 4;
        break;
    case VK_AT:
        pos.row = 7;
        pos.col = 4;
        break;
    case VK_COLON:
        pos.row = 8;
        pos.col = 4;
        break;
    case VK_RETURN:
        pos.row = 9;
        pos.col = 4;
        break;

    /* --- Col 5 --- */
    case VK_S:
        pos.row = 1;
        pos.col = 5;
        break;
    case VK_C:
        pos.row = 2;
        pos.col = 5;
        break;
    case VK_G:
        pos.row = 3;
        pos.col = 5;
        break;
    case VK_H:
        pos.row = 4;
        pos.col = 5;
        break;
    case VK_N:
        pos.row = 5;
        pos.col = 5;
        break;
    case VK_L:
        pos.row = 6;
        pos.col = 5;
        break;
    case VK_SEMICOLON:
        pos.row = 7;
        pos.col = 5;
        break;
    case VK_RBRACKET:
        pos.row = 8;
        pos.col = 5;
        break;
    case VK_BACKSPACE:
        pos.row = 9;
        pos.col = 5;
        break;

    /* --- Col 6 --- */
    case VK_TAB:
        pos.row = 0;
        pos.col = 6;
        break;
    case VK_Z:
        pos.row = 1;
        pos.col = 6;
        break;
    case VK_SPACE:
        pos.row = 2;
        pos.col = 6;
        break;
    case VK_V:
        pos.row = 3;
        pos.col = 6;
        break;
    case VK_B:
        pos.row = 4;
        pos.col = 6;
        break;
    case VK_M:
        pos.row = 5;
        pos.col = 6;
        break;
    case VK_COMMA:
        pos.row = 6;
        pos.col = 6;
        break;
    case VK_PERIOD:
        pos.row = 7;
        pos.col = 6;
        break;
    case VK_SLASH:
        pos.row = 8;
        pos.col = 6;
        break;
    case VK_END:
        pos.row = 9;
        pos.col = 6;
        break;

    /* --- Col 7 --- */
    case VK_ESCAPE:
        pos.row = 0;
        pos.col = 7;
        break;
    case VK_BACKSLASH:
        pos.row = 8;
        pos.col = 7;
        break;
    case VK_RIGHT:
        pos.row = 9;
        pos.col = 7;
        break;
        // case VK_COMMA:     pos.row = 5; pos.col = 7; break;
        // case VK_PERIOD:    pos.row = 6; pos.col = 7; break;

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

    //  MountSDDiskImage();
#if !BBC_SKIP_DISK_BOOT
    MountDisk("SD:/Welcome.ssd", 0); // later nog met menu'tje..
#else
    CLogger::Get()->Write(FromBeeb, LogNotice, "BBC_SKIP_DISK_BOOT actief: geen disk gekoppeld");
#endif
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

#if BBC_VIDEO_TEST_PATTERN
    DrawTestPattern(pFB, fbPitchWords, fbWidth, fbHeight);
    return;
#endif

    // Bronresolutie van de BBC Micro
    const u32 srcW = BBC_FB_WIDTH;     // 640
    const u32 srcH = BBC_FB_HEIGHT; // 256

    // Bereken integer schaalfactor zodat het binnen het scherm past
    u32 scaleX = fbWidth / srcW;
    u32 scaleY = fbHeight / srcH;

    if (scaleX < 1)
        scaleX = 1;
    if (scaleY < 1)
        scaleY = 1;

    // Om letters niet verticaal uit te rekken of 2x te groot te maken,
    // zorgen we voor een evenredige breedte/hoogte verhouding:
    u32 renderW = srcW * scaleX;
    u32 renderH = srcH * scaleY;

    // Centreren op het HDMI-scherm (creëert automatisch een nette border rondom)
    u32 startX = (fbWidth > renderW) ? (fbWidth - renderW) / 2 : 0;
    u32 startY = (fbHeight > renderH) ? (fbHeight - renderH) / 2 : 0;

    for (u32 srcY = 0; srcY < srcH; srcY++)
    {
        // Haal 1 complete bronscanline op uit de emulator (640 pixels)
        bbc_video_render_row(&m_Machine.video, srcY, srcH, m_RowBuf, srcW);

        // Schrijf deze scanline uit over 'scaleY' doellijnen
        for (u32 sy = 0; sy < scaleY; sy++)
        {
            u32 dstY = startY + (srcY * scaleY) + sy;
            if (dstY >= fbHeight)
                break;

            u32 *pDstRow = pFB + (dstY * fbPitchWords) + startX;

            if (scaleX == 1)
            {
                for (u32 x = 0; x < srcW; x++)
                {
                    pDstRow[x] = s_bbc_palette_argb[m_RowBuf[x] & 7];
                }
            }
            else
            {
                // Horizontale duplicatie per pixel
                for (u32 x = 0; x < srcW; x++)
                {
                    u32 color = s_bbc_palette_argb[m_RowBuf[x] & 7];
                    u32 baseDstX = x * scaleX;
                    for (u32 sx = 0; sx < scaleX; sx++)
                    {
                        if (baseDstX + sx < fbWidth)
                        {
                            pDstRow[baseDstX + sx] = color;
                        }
                    }
                }
            }
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

#if BBC_VIDEO_TEST_PATTERN
                /* Static pattern: draw once, then leave the framebuffer alone
                 * so the HDMI scanout never catches a partially-written frame. */
                static bool s_bTestPatternDrawn = false;
                if (!s_bTestPatternDrawn)
                {
                    m_pFrameBuffer->WaitForVerticalSync();
                    RenderBBCFrame();
                    s_bTestPatternDrawn = true;
                }
#else
                /* Single-buffered framebuffer: sync to vblank before writing
                 * to avoid the HDMI scanout tearing mid-frame. */
                m_pFrameBuffer->WaitForVerticalSync();
                RenderBBCFrame();
#endif
                // Flash toggle elke 25 frames (~2 Hz) of 50 frames (~1 Hz)
                static unsigned s_nFrameCounter = 0;
                if (++s_nFrameCounter >= 25)
                {
                    s_nFrameCounter = 0;
                    bbc_video_toggle_flash(&m_Machine.video);
                }

#if BBC_VIDEO_DEBUG_LOG
                /* Reroute emulator video/CPU state to the debug log once/sec */
                static unsigned s_nDebugCounter = 0;
                if (++s_nDebugCounter >= 50)
                {
                    s_nDebugCounter = 0;
                    DumpEmulatorDebug(&m_Machine);
                }
#endif

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
    bool bCtrl = (ucModifiers & 0x11) != 0;
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