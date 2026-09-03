#include "atom.h"
#include <circle/logger.h>
#include <circle/util.h>

#define DBG(fmt, ...) CLogger::Get()->Write("DBG", LogDebug, fmt, ##__VA_ARGS__)

static const char FromAtom[] = "atom";
volatile bool g_bFullscreenMode = false;
static volatile unsigned g_FullscreenCore = 0;
volatile bool g_bTrueFullscreen = false;
static unsigned g_SkipCounter[4] = { 0, 0, 0, 0 };
static const u64 FRAME_TIME_US = 20000;
static const u64 MAX_CATCHUP_US = FRAME_TIME_US * 5;
static volatile unsigned g_nCoreLoad[4] = { 0, 0, 0, 0 };

CWelcomeAnimation g_WelcomeAnim;
CAtomRunner *CAtomRunner::s_pThis = nullptr;
volatile unsigned CAtomRunner::m_nActiveCore = 0;

static const u32 Palette8[9] __attribute__((aligned(16))) = {
  0xFF000000, 0xFF00FF00, 0xFFFFFF00, 0xFF0000FF,
  0xFFFF0000, 0xFFFFFFFF, 0xFF00FFFF, 0xFFFF00FF,
  0xFF101010
};

static u8 s_LastVRAM[4][256 * 192] = { 0 };
static bool s_InitializedVRAM[4] = { false };

static inline u32 *GetFrameBufferPtr(CBcmFrameBuffer &fb) {
  return (u32 *)(uintptr_t)fb.GetBuffer();
}

// ------------------------------------------------------------
// Render- en tekenfuncties
/* ------------------------------------------------------------
static void DrawFocusBorders(CBcmFrameBuffer &frameBuffer, unsigned nActiveCore) {
  u32 *pFB = GetFrameBufferPtr(frameBuffer);
  if (!pFB) return;

  const u32 fbWidth = frameBuffer.GetWidth();
  const u32 fbHeight = frameBuffer.GetHeight();
  const u32 pitchWords = frameBuffer.GetPitch() / 4;

  const int quadW = 512;
  const int quadH = 384;
  const u32 borderNormal = 0xFF404040;
  const u32 borderActive = 0xFFFFFF00;

  for (unsigned core = 0; core < 4; ++core) {
    int baseX = (core % 2) * quadW;
    int baseY = (core / 2) * quadH;
    u32 color = (core == nActiveCore) ? borderActive : borderNormal;

    for (int x = 0; x < quadW && baseX + x < (int)fbWidth; ++x) {
      int tx = baseX + x;
      int ty = baseY;
      int by = baseY + quadH - 1;
      if (ty >= 0 && ty < (int)fbHeight) pFB[ty * pitchWords + tx] = color;
      if (by >= 0 && by < (int)fbHeight) pFB[by * pitchWords + tx] = color;
    }

    for (int y = 0; y < quadH && baseY + y < (int)fbHeight; ++y) {
      int ly = baseY + y;
      int lx = baseX;
      int rx = baseX + quadW - 1;
      if (lx >= 0 && lx < (int)fbWidth) pFB[ly * pitchWords + lx] = color;
      if (rx >= 0 && rx < (int)fbWidth) pFB[rx * pitchWords + rx] = color;
    }
  }
} */

static void DrawVUMeter(CBcmFrameBuffer &fb, int x, int y, int width, int height, unsigned pct, unsigned nCore) {
  if (g_bTrueFullscreen) return;

  u32 *pBuffer = GetFrameBufferPtr(fb);
  if (!pBuffer) return;

  unsigned pitchWords = fb.GetPitch() / 4;
  if (pct > 100) pct = 100;

  int filledWidth = (width * (int)pct) / 100;
  u32 fillColor = (pct < 60) ? 0xFF00D000 : ((pct < 85) ? 0xFFFFA500 : 0xFFFF2020);
  u32 bgColor = 0xFF202020;
  u32 borderColor = 0xFF808080;

  for (int dy = 0; dy < height; dy++) {
    int py = y + dy;
    for (int dx = 0; dx < width; dx++) {
      int px = x + dx;
      u32 color;
      if (dx == 0 || dx == width - 1 || dy == 0 || dy == height - 1)
        color = borderColor;
      else if (dx <= filledWidth)
        color = fillColor;
      else
        color = bgColor;

      pBuffer[py * pitchWords + px] = color;
    }
  }
}

static void RenderCoreQuadrantBuffer(u32 *pDestBuffer, unsigned nCoreId, const u8 *pLocalVRAM) {
  if (!pLocalVRAM || !pDestBuffer) return;

  constexpr int SRC_W = 256;
  constexpr int SRC_H = 192;
  constexpr int DST_W = 512;

  if (g_WelcomeAnim.IsPlaying(nCoreId)) {
    g_WelcomeAnim.DrawCurrentFrameCentered(pDestBuffer, DST_W, 0, 0, nCoreId);
    s_InitializedVRAM[nCoreId] = false;
    CAtomRunner::Get()->GetEmulator(nCoreId)->ResetVRAMChanged();
    return;
  }

  u8 *pLastVRAM = s_LastVRAM[nCoreId];
  bool forceFullRedraw = !s_InitializedVRAM[nCoreId];
  if (forceFullRedraw) {
    memcpy(pLastVRAM, pLocalVRAM, SRC_W * SRC_H);
    s_InitializedVRAM[nCoreId] = true;
  }

  for (int y = 0; y < SRC_H; y++) {
    const u8 *pSrcRow = &pLocalVRAM[y * SRC_W];
    u8 *pLastRow = &pLastVRAM[y * SRC_W];

    bool lineChanged = forceFullRedraw || (memcmp(pSrcRow, pLastRow, SRC_W) != 0);
    if (!lineChanged) continue;

    memcpy(pLastRow, pSrcRow, SRC_W);

    u32 *pDstRow1 = pDestBuffer + ((2 * y + 0) * DST_W);
    u32 *pDstRow2 = pDestBuffer + ((2 * y + 1) * DST_W);

    for (int x = 0; x < SRC_W; x++) {
      u32 P = CAtomVideo::GetColorARGB(pSrcRow[x]);
      if ((nCoreId == 1 || nCoreId == 2) && P == Palette8[0]) {
        P = Palette8[8];
      }
      pDstRow1[2 * x + 0] = P;
      pDstRow1[2 * x + 1] = P;
      pDstRow2[2 * x + 0] = P;
      pDstRow2[2 * x + 1] = P;
    }
  }
}

static void BlitQuadrantToScreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u32 *pSourceBuffer) {
  if (!pSourceBuffer) return;

  u32 *pFB = GetFrameBufferPtr(frameBuffer);
  if (!pFB) return;

  const int fbW = (int)frameBuffer.GetWidth();
  const int fbH = (int)frameBuffer.GetHeight();
  const int pitch = (int)frameBuffer.GetPitch() / 4;

  constexpr int QW = 512;
  constexpr int QH = 384;

  int baseX = (nCoreId & 1) ? 512 : 0;
  int baseY = (nCoreId & 2) ? 384 : 0;

  if (baseX + QW > fbW || baseY + QH > fbH) return;

  for (int y = 0; y < QH; ++y) {
    const u32 *pSrcRow = pSourceBuffer + (y * QW);
    u32 *pDstRow = pFB + (baseY + y) * pitch + baseX;
    for (int x = 0; x < QW; ++x) {
      pDstRow[x] = pSrcRow[x];
    }
  }
}

static void RenderSingleCoreFullscreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM) {
  if (!pLocalVRAM || !frameBuffer.GetBuffer()) return;

  constexpr int SRC_W = 256;
  constexpr int SRC_H = 192;

  const u32 fbWidth = frameBuffer.GetWidth();
  const u32 fbHeight = frameBuffer.GetHeight();
  const u32 pitchWords = frameBuffer.GetPitch() / 4;
  u32 *pFB = GetFrameBufferPtr(frameBuffer);

  const int scale = 4;
  const int dstW = SRC_W * scale;
  const int dstH = SRC_H * scale;

  int startX = 0;
  int startY = 0;

  if (g_bTrueFullscreen) {
    if (fbWidth > (unsigned)dstW) startX = (fbWidth - dstW) / 2;
    if (fbHeight > (unsigned)dstH) startY = (fbHeight - dstH) / 2;

    for (unsigned y = 0; y < fbHeight; y++) {
      u32 *pRow = pFB + y * pitchWords;
      for (unsigned x = 0; x < fbWidth; x++) {
        if (x < (unsigned)startX || x >= (unsigned)(startX + dstW) || y < (unsigned)startY || y >= (unsigned)(startY + dstH)) {
          pRow[x] = 0xFF000000;
        }
      }
    }
  }

  for (int y = 0; y < SRC_H; y++) {
    const u8 *pSrcRow = &pLocalVRAM[y * SRC_W];

    for (int x = 0; x < SRC_W; x++) {
      u32 P = CAtomVideo::GetColorARGB(pSrcRow[x]);
      if ((nCoreId == 1 || nCoreId == 2) && P == Palette8[0]) {
        P = Palette8[8];
      }

      int dstX0 = startX + x * scale;
      int dstY0 = startY + y * scale;

      for (int dy = 0; dy < scale; dy++) {
        int py = dstY0 + dy;
        if (py < 0 || py >= (int)fbHeight) continue;

        u32 *pDstRow = pFB + py * pitchWords;
        for (int dx = 0; dx < scale; dx++) {
          int px = dstX0 + dx;
          if (px < 0 || px >= (int)fbWidth) continue;
          pDstRow[px] = P;
        }
      }
    }
  }
}

// ------------------------------------------------------------
// Keyboard translation
// ------------------------------------------------------------
struct TAtomKeyResult {
  uint8_t atomKey;
  bool bForceShift;
  bool bSuppressShift;
};

static TAtomKeyResult ConvertHIDToAtomKeyCustom(uint8_t hidCode, uint8_t ucModifiers) {
  TAtomKeyResult res = { 0, false, false };
  bool bShift = (ucModifiers & 0x22) != 0;

  if (hidCode >= 0x04 && hidCode <= 0x1D) {
    res.atomKey = 'A' + (hidCode - 0x04);
    return res;
  }

  switch (hidCode) {
    case 0x4F: res.atomKey = 0x02; return res;
    case 0x50:
      res.atomKey = 0x02;
      res.bForceShift = true;
      return res;
    case 0x51:
      res.atomKey = 0x01;
      res.bForceShift = true;
      return res;
    case 0x52: res.atomKey = 0x01; return res;
  }

  if (hidCode >= 0x1E && hidCode <= 0x27) {
    if (!bShift) {
      res.atomKey = (hidCode == 0x27) ? '0' : ('1' + (hidCode - 0x1E));
      return res;
    } else {
      switch (hidCode) {
        case 0x1F:
          res.atomKey = '@';
          res.bSuppressShift = true;
          return res;
        case 0x25:
          res.atomKey = ';';
          res.bForceShift = true;
          return res;
        case 0x26:
          res.atomKey = '8';
          res.bForceShift = true;
          return res;
        case 0x27:
          res.atomKey = '9';
          res.bForceShift = true;
          return res;
        case 0x39: res.atomKey = 0x03; return res;
        default: res.atomKey = (hidCode == 0x27) ? '0' : ('1' + (hidCode - 0x1E)); return res;
      }
    }
  }

  switch (hidCode) {
    case 0x2B: res.atomKey = 0x09; return res;
    case 0x2E:
      if (!bShift) {
        res.atomKey = '-';
        res.bForceShift = true;
      } else {
        res.atomKey = ';';
        res.bForceShift = true;
      }
      return res;
    case 0x31:
    case 0x32: res.atomKey = '\\'; return res;
    case 0x34:
      res.atomKey = '\'';
      res.bForceShift = true;
      return res;
    case 0x28: res.atomKey = '\r'; return res;
    case 0x29: res.atomKey = 0x1B; return res;
    case 0x2A: res.atomKey = 0x08; return res;
    case 0x2C: res.atomKey = ' '; return res;
    case 0x2D: res.atomKey = '-'; return res;
    case 0x2F: res.atomKey = '['; return res;
    case 0x30: res.atomKey = ']'; return res;
    case 0x33: res.atomKey = ';'; return res;
    case 0x36: res.atomKey = ','; return res;
    case 0x37: res.atomKey = '.'; return res;
    case 0x38: res.atomKey = '/'; return res;
    default: break;
  }

  return res;
}

// ------------------------------------------------------------
// CWelcomeAnimation implementatie
// ------------------------------------------------------------
CWelcomeAnimation::CWelcomeAnimation()
  : m_pFrames(nullptr), m_TotalFrames(210), m_SubFrameCounter(0) {
  for (int i = 0; i < 4; i++) {
    m_IsPlaying[i] = true;
    m_CurrentFrame[i] = 0;
    m_FrameDelay[i] = 0;
  }
}

CWelcomeAnimation::~CWelcomeAnimation() {
  Free();
}

bool CWelcomeAnimation::Initialize(const char *pBinPath) {
  FIL file;
  FRESULT res = f_open(&file, pBinPath, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK) return false;

  UINT fileSize = f_size(&file);
  if (fileSize == 0) {
    f_close(&file);
    return false;
  }

  m_pFrames = new u32[fileSize / sizeof(u32)];
  if (!m_pFrames) {
    f_close(&file);
    return false;
  }

  UINT bytesRead = 0;
  FRESULT readRes = f_read(&file, m_pFrames, fileSize, &bytesRead);
  f_close(&file);

  if (readRes != FR_OK || bytesRead != fileSize) {
    delete[] m_pFrames;
    m_pFrames = nullptr;
    return false;
  }

  asm volatile("dsb sy" ::
                 : "memory");
  return true;
}

bool CWelcomeAnimation::IsPlaying(unsigned coreId) const {
  return (coreId < 4) ? m_IsPlaying[coreId] : false;
}

void CWelcomeAnimation::StopCore(unsigned coreId) {
  if (coreId < 4) {
    m_IsPlaying[coreId] = false;
    asm volatile("dmb sy" ::
                   : "memory");

    if (!IsAnyPlaying()) {
      //CLogger::Get()->Write(FromAtom, LogError, "Free");
      Free();
    }
  }
}

void CWelcomeAnimation::UpdateAnimation() {
  m_SubFrameCounter++;
  if (m_SubFrameCounter >= 2) {
    m_SubFrameCounter = 0;

    for (int i = 0; i < 4; i++) {
      if (!g_WelcomeAnim.IsPlaying(i))
        //(!m_IsPlaying[i])
        continue;

      m_FrameDelay[i]++;
      if (m_FrameDelay[i] > i) {
        m_FrameDelay[i] = 0;
        if (i == 0 || i == 3) {
          m_CurrentFrame[i]++;
          if (m_CurrentFrame[i] >= m_TotalFrames) m_CurrentFrame[i] = 0;
        } else {
          m_CurrentFrame[i]--;
          if (m_CurrentFrame[i] < 0) m_CurrentFrame[i] = m_TotalFrames - 1;
        }
      }
    }
  }
}

void CWelcomeAnimation::DrawCurrentFrameCentered(u32 *pFB, u32 pitch, u32 startX, u32 startY, unsigned coreId) {
  if (!m_pFrames || coreId >= 4) {
    CLogger::Get()->Write(FromAtom, LogError, "!m_pFrames || coreId >= 4 %d ", coreId);
    return;
  }
  // CLogger::Get()->Write(FromAtom, LogError, "u32 pitch, u32 startX, u32 startY, unsigned coreId %d,  %d,  %d,  %d,  ", pitch, startX, startY, coreId);

  constexpr int FRAME_W = 256;
  constexpr int FRAME_H = 109;
  constexpr int WIN_W = 512;
  constexpr int WIN_H = 384;

  int offsetX = (WIN_W - FRAME_W) / 2;
  int offsetY = (WIN_H - FRAME_H) / 2;

  int frameIdx = m_CurrentFrame[coreId];
  const u32 *pSrcFrame = &m_pFrames[frameIdx * (FRAME_W * FRAME_H)];

  for (int y = 0; y < FRAME_H; y++) {
    u32 *pDstRow = pFB + ((startY + offsetY + y) * pitch) + startX + offsetX;
    const u32 *pSrcRow = &pSrcFrame[y * FRAME_W];

    for (int x = 0; x < FRAME_W; x++) {
      u32 pixel = pSrcRow[x];
      u8 alpha = (pixel >> 24) & 0xFF;
      pDstRow[x] = (alpha > 10) ? pixel : 0xFF000000;
    }
  }
}

// ------------------------------------------------------------
// CAtomRunner implementatie
// ------------------------------------------------------------
CAtomRunner::CAtomRunner(CBcmFrameBuffer *pFrameBuffer, CTimer *pTimer,
                         CDeviceNameService *pDeviceNameService, CMemorySystem *pMemorySystem)
  : CMultiCoreSupport(pMemorySystem),
    m_pFrameBuffer(pFrameBuffer),
    m_pTimer(pTimer),
    m_pDeviceNameService(pDeviceNameService),
    m_pConsole(nullptr),
    m_pKeyboard(nullptr),
    m_bKeyboardReady(FALSE),
    m_bCoreInitDone(FALSE),
    m_bShutdown(FALSE) {
  s_pThis = this;
  for (int i = 0; i < 4; i++) {
    m_LocalQuadrantBuffer[i] = nullptr;
  }
}

CAtomRunner::~CAtomRunner(void) {
  for (int i = 0; i < 4; i++) {
    delete[] m_LocalQuadrantBuffer[i];
  }
  delete m_pConsole;
  s_pThis = nullptr;
}

boolean CAtomRunner::Initialize(void) {
  boolean bOK = TRUE;

  for (int i = 0; i < 4; i++) {
    m_LocalQuadrantBuffer[i] = new u32[512 * 384];
    if (!m_LocalQuadrantBuffer[i]) {
      CLogger::Get()->Write(FromAtom, LogError, "Kan buffer voor core %d niet alloceren!", i);
      bOK = FALSE;
    } else {
      u32 *pDest = m_LocalQuadrantBuffer[i];
      for (unsigned j = 0; j < 512 * 384; ++j) {
        *pDest++ = 0xFF080808;
      }
    }
  }

  if (bOK) {
    m_pConsole = new CTextConsole(m_pFrameBuffer, 1040, 24, 0xFFFFFFFF, 0xFF080808);
    m_pConsole->WriteString("Quatom Multi-Core Monitor Ready.\r\n");
    m_pConsole->WriteString("-----------------------------------------\r\n");
  }

  if (bOK) {
    CLogger::Get()->Write(FromAtom, LogNotice, "Bezig met laden van AtomMP4.bin...");
    if (!g_WelcomeAnim.Initialize("SD:/AtomMP4.bin")) {
      CLogger::Get()->Write(FromAtom, LogError, "Kan AtomMP4.bin niet laden, we gaan door...");
    } else {
      CLogger::Get()->Write(FromAtom, LogNotice, "AtomMP4.bin succesvol geladen!");
    }
  }

  if (bOK) {
    bOK = CMultiCoreSupport::Initialize();
  }

  if (bOK) {
    m_pConsole->WriteString("Multicore started\r\n");
    for (int core = 0; core < 4; core++) {
      bOK = m_AtomEmulator[core].Initialize(core);
      if (bOK) {
        CLogger::Get()->Write(FromAtom, LogNotice, "--> Starten van AtomEmulator %d", core);
      }
    }
  }

  if (bOK) {
    // DrawFocusBorders(*m_pFrameBuffer, 0);
    m_bCoreInitDone = TRUE;
  }

  return bOK;
}

void CAtomRunner::ProcessEmulatorFrame(unsigned coreIdx)
{
    const u64 tStart = m_pTimer->GetClockTicks64();

    // 1. Emulatie stap uitvoeren
    m_AtomEmulator[coreIdx].Update();

    // 2. Renderen indien VRAM gewijzigd of skip-teller bereikt
    const bool bVRAM = m_AtomEmulator[coreIdx].HasVRAMChanged();
    bool bDoRender = bVRAM || (++g_SkipCounter[coreIdx] >= 5);

    if (bDoRender)
    {
        g_SkipCounter[coreIdx] = 0;

        if (g_bFullscreenMode)
        {
            if (g_FullscreenCore == coreIdx)
            {
                RenderSingleCoreFullscreen(*m_pFrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
            }
        }
        else
        {
            RenderCoreQuadrantBuffer(m_LocalQuadrantBuffer[coreIdx], coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
            BlitQuadrantToScreen(*m_pFrameBuffer, coreIdx, m_LocalQuadrantBuffer[coreIdx]);
        }

        m_AtomEmulator[coreIdx].ResetVRAMChanged();
    }

    // 3. CPU Load meting & sliding average
    const u64 tWork = m_pTimer->GetClockTicks64() - tStart;
    unsigned rawLoad = (unsigned)((tWork * 100) / FRAME_TIME_US);
    if (rawLoad > 100) rawLoad = 100;
    g_nCoreLoad[coreIdx] = (g_nCoreLoad[coreIdx] * 7 + rawLoad) / 8;
}

void CAtomRunner::Run(unsigned nCore)
{
    // ============================================================
    // SLAVE CORES (Core 1..3 -> Emulator 0..2)
    // ============================================================
 if (nCore > 0)
    {
        while (!m_bCoreInitDone)
        {
            asm volatile("dmb sy" ::: "memory");
        }

        unsigned coreIdx = nCore - 1;
        if (coreIdx < 3)
        {
            // Eenmalige opstartlog is veilig
            CLogger::Get()->Write(FromAtom, LogNotice, "Core %d: Acorn Atom Emulator gestart...", nCore);

            u64 nNextFrameTime = m_pTimer->GetClockTicks64();

            while (!m_bShutdown)
            {
                u64 nCurrentTime = m_pTimer->GetClockTicks64();

                // Voorkom achterstand/drift bij incidentele vertraging
                if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
                    nNextFrameTime = nCurrentTime;

                // 1. Voer de emulatie en render-stap uit
                ProcessEmulatorFrame(coreIdx);

                // 2. Wacht netjes tot het volgende 50Hz frame (20000 µs)
                nNextFrameTime += FRAME_TIME_US;
                while (m_pTimer->GetClockTicks64() < nNextFrameTime)
                {
                    asm volatile("yield");
                }
            }
        }
        return;
    }

    // ============================================================
    // MASTER CORE (Core 0 -> USB, UI, Animatie & Emulator 3)
    // ============================================================
    CLogger::Get()->Write(FromAtom, LogNotice, "Core 0 Run-loop gestart (USB, I/O Master & Emulator 3)...");
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
                CLogger::Get()->Write(FromAtom, LogNotice, "SUCCESS: USB Toetsenbord gekoppeld op Core 0!");
                m_bKeyboardReady = TRUE;
            }
        }

        // 2. Toetsenbord LEDs periodiek updaten
        u64 nCurrentTime = m_pTimer->GetClockTicks64();
        if (m_pKeyboard != nullptr && (nCurrentTime - nLastLEDTime >= 100000))
        {
            nLastLEDTime = nCurrentTime;
            m_pKeyboard->UpdateLEDs();
        }

        // 3. Frame tick Core 0
        if (nCurrentTime >= nNextFrameTime)
        {
            if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
            {
                nNextFrameTime = nCurrentTime;
            }

            // Welkomstanimatie update of vrijgeven
            if (g_WelcomeAnim.IsAnyPlaying())
            {
                static u64 lastAnimTick = 0;
                if (nCurrentTime - lastAnimTick >= FRAME_TIME_US)
                {
                    lastAnimTick = nCurrentTime;
                    g_WelcomeAnim.UpdateAnimation();
                }
            }
            else
            {
                g_WelcomeAnim.Free();
            }

            // Gemeenschappelijke update & render voor Core 0 (Emulator 3)
            ProcessEmulatorFrame(3);

            // VU-meters periodiek renderen (4x per seconde)
            static u64 nLastVUTime = 0;
            if (nCurrentTime - nLastVUTime >= 250000)
            {
                nLastVUTime = nCurrentTime;
                const u32 fbHeight = m_pFrameBuffer->GetHeight();
                for (unsigned c = 0; c < 4; c++)
                {
                    const int barX = (2 * 512) + 10;
                    const int barY = fbHeight - 10 - ((4 - c) * 15);
                    DrawVUMeter(*m_pFrameBuffer, barX, barY, 325, 10, g_nCoreLoad[c], nCore);
                }
            }

            nNextFrameTime += FRAME_TIME_US;
        }

        asm volatile("yield");
    }
}

void CAtomRunner::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]) {
  if (!s_pThis) return;

  static uint8_t s_lastHID[6] = { 0 };
  static uint8_t s_lastAtomKey[6] = { 0 };

  unsigned activeCore = s_pThis->m_nActiveCore;
  unsigned newCore = activeCore;

  // ------------------------------------------------------------
  // 1. Function keys (F1..F12)
  // ------------------------------------------------------------
  for (int i = 0; i < 6; i++) {
    uint8_t hid = RawKeys[i] & 0xFF;
    if (!hid) continue;

    if (hid == 0x42) {
      g_bTrueFullscreen = !g_bTrueFullscreen;
      CLogger::Get()->Write(FromAtom, LogNotice, "F9: True Fullscreen = %s", g_bTrueFullscreen ? "AAN" : "UIT");
      continue;
    }

    if (hid == 0x45) {
      s_pThis->m_AtomEmulator[activeCore].Reset();
      CLogger::Get()->Write(FromAtom, LogNotice, "HARDWARE RESET uitgevoerd voor Core %d via F12!", activeCore);
      continue;
    }

    // F1..F4 -> Quadrant mode & core focus
    if (hid >= 0x3A && hid <= 0x3D) {
      newCore = hid - 0x3A;
      if (g_bFullscreenMode) {
        g_bFullscreenMode = false;
        for (int c = 0; c < 4; c++) g_SkipCounter[c] = 5;
        if (s_pThis && (u32 *)(uintptr_t)s_pThis->m_pFrameBuffer->GetBuffer()) {
          memset((void *)(uintptr_t)s_pThis->m_pFrameBuffer->GetBuffer(), 0, s_pThis->m_pFrameBuffer->GetSize());
        }
      }
      if (g_WelcomeAnim.IsPlaying(newCore)) {
        g_WelcomeAnim.StopCore(newCore);
        CLogger::Get()->Write(FromAtom, LogError, "g_WelcomeAnim.StopCore(newCore) %d", newCore);
      }

      s_InitializedVRAM[newCore] = false;  // Forceer redraw van Atom VRAM
      // DrawFocusBorders(*s_pThis->m_pFrameBuffer, newCore);
      continue;
    }

    // F5..F8 -> Fullscreen mode
    if (hid >= 0x3E && hid <= 0x41) {
      g_bFullscreenMode = true;
      g_FullscreenCore = (hid - 0x3E);
      newCore = g_FullscreenCore;
      g_WelcomeAnim.StopCore(newCore);
      s_InitializedVRAM[newCore] = false;
      continue;
    }

    // Normale toets -> stop direct de animatie op de actieve core
    if (hid < 0x3A || hid > 0x45) {
      if (g_WelcomeAnim.IsPlaying(activeCore)) {
        g_WelcomeAnim.StopCore(activeCore);
        s_InitializedVRAM[activeCore] = false;  // Forceer hertekenen van VRAM ipv animatie
      }
    }
  }

  // ------------------------------------------------------------
  // 2. Core switch uitvoeren
  // ------------------------------------------------------------
  if (newCore != activeCore) {
    s_pThis->m_AtomEmulator[activeCore].ClearKeyboard();
    memset(s_lastHID, 0, sizeof(s_lastHID));
    memset(s_lastAtomKey, 0, sizeof(s_lastAtomKey));
    s_pThis->m_nActiveCore = newCore;

    // if (!g_bFullscreenMode) {
    //   DrawFocusBorders(*s_pThis->m_pFrameBuffer, newCore);
    // }
    activeCore = newCore;
  }

  CAtomEmulator &emu = s_pThis->m_AtomEmulator[activeCore];

  // ------------------------------------------------------------
  // 3. Modifier states
  // ------------------------------------------------------------
  bool physShift = (ucModifiers & 0x22) != 0;
  bool physCtrl = (ucModifiers & 0x11) != 0;
  bool physAlt = (ucModifiers & 0x44) != 0;

  bool forceShift = false;
  bool suppressShift = false;

  for (int i = 0; i < 6; i++) {
    if (!RawKeys[i]) continue;
    TAtomKeyResult r = ConvertHIDToAtomKeyCustom(RawKeys[i], ucModifiers);
    if (r.bForceShift) forceShift = true;
    if (r.bSuppressShift) suppressShift = true;
  }

  bool effectiveShift = (physShift || forceShift) && !suppressShift;
  emu.SetShiftState(effectiveShift);
  emu.SetCtrlState(physCtrl);
  emu.SetReptState(physAlt);

  // ------------------------------------------------------------
  // 4. KeyDown detectie
  // ------------------------------------------------------------
  for (int i = 0; i < 6; i++) {
    uint8_t hid = RawKeys[i] & 0xFF;
    if (!hid) continue;

    bool alreadyPressed = false;
    for (int j = 0; j < 6; j++) {
      if (s_lastHID[j] == hid) alreadyPressed = true;
    }

    if (!alreadyPressed) {
      TAtomKeyResult r = ConvertHIDToAtomKeyCustom(hid, ucModifiers);
      if (r.atomKey) {
        // Zorg dat animatie gegarandeerd uit staat
        if (g_WelcomeAnim.IsPlaying(activeCore)) {
          g_WelcomeAnim.StopCore(activeCore);
          s_InitializedVRAM[activeCore] = false;
        }
        emu.KeyDown(r.atomKey);
        s_lastAtomKey[i] = r.atomKey;
        s_lastHID[i] = hid;
      }
    }
  }

  // ------------------------------------------------------------
  // 5. KeyUp detectie
  // ------------------------------------------------------------
  for (int i = 0; i < 6; i++) {
    uint8_t oldHID = s_lastHID[i];
    if (!oldHID) continue;

    bool stillPressed = false;
    for (int j = 0; j < 6; j++) {
      if (RawKeys[j] == oldHID) stillPressed = true;
    }

    if (!stillPressed) {
      uint8_t atomKey = s_lastAtomKey[i];
      if (atomKey) emu.KeyUp(atomKey);
      s_lastAtomKey[i] = 0;
      s_lastHID[i] = 0;
    }
  }

  // ------------------------------------------------------------
  // 6. Reset bij alle toetsen los
  // ------------------------------------------------------------
  bool allReleased = true;
  for (int i = 0; i < 6; i++) {
    if (RawKeys[i] != 0) allReleased = false;
  }

  if (allReleased) {
    emu.ClearKeyboard();
    memset(s_lastHID, 0, sizeof(s_lastHID));
    memset(s_lastAtomKey, 0, sizeof(s_lastAtomKey));
  }

  // Direct renderen forceren
  g_SkipCounter[activeCore] = 5;
  emu.ForceVRAMChanged();
}

void CAtomRunner::KeyboardRemovedHandler(CDevice *pDevice, void *pContext) {
  if (s_pThis != nullptr) {
    CLogger::Get()->Write(FromAtom, LogNotice, "USB Toetsenbord ontkoppeld.");
    s_pThis->m_pKeyboard = nullptr;
    s_pThis->m_bKeyboardReady = FALSE;
  }
}