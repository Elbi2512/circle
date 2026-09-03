#include "atom.h"
#include <circle/logger.h>
#include <circle/util.h>

#define DBG(fmt, ...) CLogger::Get()->Write("DBG", LogDebug, fmt, ##__VA_ARGS__)

static void RenderCoreQuadrantDirect(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM);
static void RenderSingleCoreFullscreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM);
static void RenderSingleCoreFullscreenText(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pTextVRAM);

static const char FromAtom[] = "atom";
volatile bool g_bFullscreenMode = false;
static volatile unsigned g_FullscreenCore = 0;
volatile bool g_bTrueFullscreen = false;
static unsigned g_SkipCounter[4] = {0, 0, 0, 0};
static const u64 FRAME_TIME_US = 20000;
static const u64 MAX_CATCHUP_US = FRAME_TIME_US * 5;
static volatile unsigned g_nCoreLoad[4] = {0, 0, 0, 0};

CWelcomeAnimation g_WelcomeAnim;
CAtomRunner *CAtomRunner::s_pThis = nullptr;
volatile unsigned CAtomRunner::m_nActiveCore = 0;

static const u32 Palette8[9] __attribute__((aligned(16))) = {
    0xFF000000, 0xFF00FF00, 0xFFFFFF00, 0xFF0000FF,
    0xFFFF0000, 0xFFFFFFFF, 0xFF00FFFF, 0xFFFF00FF,
    0xFF202010};

static u8 s_LastVRAM[4][256 * 192];
static bool s_InitializedVRAM[4] = {false, false, false, false};

static inline u32 *GetFrameBufferPtr(CBcmFrameBuffer &fb)
{
  return (u32 *)(uintptr_t)fb.GetBuffer();
}

static void ClearEntireHDMIFrameBuffer(CBcmFrameBuffer &frameBuffer)
{
  u32 *pFB = GetFrameBufferPtr(frameBuffer);
  if (!pFB)
    return;

  size_t totalBytes = frameBuffer.GetSize();
  if (totalBytes > 0)
  {
    memset(pFB, 0, totalBytes);
  }
}

static void DrawVUMeter(CBcmFrameBuffer &fb, int x, int y, int width, int height, const volatile unsigned pct[4], unsigned nCore)
{
  if (g_bTrueFullscreen)
    return;

  u32 *pBuffer = GetFrameBufferPtr(fb);
  if (!pBuffer)
    return;

  unsigned pitchWords = fb.GetPitch() / 4;
  unsigned screenW = fb.GetWidth();
  unsigned screenH = fb.GetHeight();

  const int gap = 2;
  int subW = (width - gap) / 2;
  int subH = (height - gap) / 2;

  if (subW <= 4 || subH <= 2)
    return;

  for (unsigned c = 0; c < 4; c++)
  {
    bool bHighlight = (nCore == c);
    unsigned val = (pct[c] > 100) ? 100 : pct[c];

    int qX = x + ((c & 1) ? (subW + gap) : 0);
    int qY = y + ((c >= 2) ? (subH + gap) : 0);

    int filledWidth = (subW * (int)val) / 100;

    u32 fillColor;
    if (bHighlight)
    {
      fillColor = (val < 60)   ? 0xFF00FF00
                  : (val < 85) ? 0xFFFFFF00
                               : 0xFFFF0000;
    }
    else
    {
      fillColor = (val < 60)   ? 0xFF007F00
                  : (val < 85) ? 0xFF7F7F00
                               : 0xFF7f0000;
    }

    u32 bgColor = 0xFF181818;
    // bool bCoreTurbo = s_pThis ? s_pThis->GetEmulator(c)->IsTurbo() : false;
    bool bCoreTurbo = CAtomRunner::Get() ? CAtomRunner::Get()->GetEmulator(c)->IsTurbo() : false;
    
    // Randkleur: Cyaan bij Turbo, Wit bij actief normaal, Grijs bij inactief
    u32 borderColor = bHighlight ? (bCoreTurbo ? 0xFF00FFFF : 0xFFFFFFFF) : 0xFF7F7F7F;
    int borderSize = bHighlight ? 2 : 1;
    // u32 borderColor = bHighlight ? 0xFFFFFFFF : 0xFF7F7F7F;
    // int borderSize = bHighlight ? 2 : 1;

    for (int dy = 0; dy < subH; dy++)
    {
      int py = qY + dy;
      if (py < 0 || py >= (int)screenH)
        continue;

      for (int dx = 0; dx < subW; dx++)
      {
        int px = qX + dx;
        if (px < 0 || px >= (int)screenW)
          continue;

        u32 color;
        if (dx < borderSize || dx >= (subW - borderSize) || dy < borderSize || dy >= (subH - borderSize))
        {
          color = borderColor;
        }
        else if (dx <= filledWidth)
        {
          color = fillColor;
        }
        else
        {
          color = bgColor;
        }
        pBuffer[py * pitchWords + px] = color;
      }
    }
  }
}

void CAtomRunner::InvalidateQuadrantVRAM(unsigned coreId)
{
  if (coreId < 4)
  {
    memset(s_LastVRAM[coreId], 0xFF, sizeof(s_LastVRAM[coreId]));
    s_InitializedVRAM[coreId] = false;
  }
}

// ------------------------------------------------------------
// Keyboard translation
// ------------------------------------------------------------
struct TAtomKeyResult
{
  uint8_t atomKey;
  bool bForceShift;
  bool bSuppressShift;
  bool bForceCtrl;
};

static TAtomKeyResult ConvertHIDToAtomKeyCustom(uint8_t hidCode, uint8_t ucModifiers)
{
  TAtomKeyResult res = {0, false, false, false};
  bool bShift = (ucModifiers & 0x22) != 0;

  if (hidCode >= 0x04 && hidCode <= 0x1D)
  {
    res.atomKey = 'A' + (hidCode - 0x04);
    return res;
  }

  if (hidCode >= 0x1E && hidCode <= 0x27)
  {
    if (!bShift)
    {
      res.atomKey = (hidCode == 0x27) ? '0' : ('1' + (hidCode - 0x1E));
      res.bSuppressShift = true;
      return res;
    }
    else
    {
      switch (hidCode)
      {
      case 0x1E:
        res.atomKey = '1';
        res.bForceShift = true;
        return res;
      case 0x1F:
        res.atomKey = '@';
        res.bSuppressShift = true;
        return res;
      case 0x20:
        res.atomKey = '3';
        res.bForceShift = true;
        return res;
      case 0x21:
        res.atomKey = '4';
        res.bForceShift = true;
        return res;
      case 0x22:
        res.atomKey = '5';
        res.bForceShift = true;
        return res;
      case 0x23:
        res.atomKey = 0x09;
        res.bSuppressShift = true;
        return res;
      case 0x24:
        res.atomKey = '6';
        res.bForceShift = true;
        return res;
      case 0x25:
        res.atomKey = ':';
        res.bForceShift = true;
        res.bSuppressShift = false;
        return res;
      case 0x26:
        res.atomKey = '8';
        res.bForceShift = true;
        return res;
      case 0x27:
        res.atomKey = '9';
        res.bForceShift = true;
        return res;
      }
    }
  }

  switch (hidCode)
  {
  case 0x2C:
    res.atomKey = ' ';
    return res;
  case 0x2D:
    if (!bShift)
    {
      res.atomKey = '-';
      res.bSuppressShift = true;
    }
    else
    {
      res.atomKey = 'P';
      res.bForceShift = true;
    }
    return res;
  case 0x2E:
    if (!bShift)
    {
      res.atomKey = '-';
      res.bForceShift = true;
    }
    else
    {
      res.atomKey = ';';
      res.bForceShift = true;
    }
    return res;
  case 0x28:
    res.atomKey = '\r';
    return res;
  case 0x29:
    res.atomKey = 0x1B;
    return res;
  case 0x2A:
    res.atomKey = 0x08;
    return res;
  case 0x2B:
    res.atomKey = 0x09;
    return res;
  case 0x2F:
    res.atomKey = '[';
    res.bSuppressShift = true;
    return res;
  case 0x30:
    res.atomKey = ']';
    res.bSuppressShift = true;
    return res;
  case 0x31:
  case 0x32:
    res.atomKey = '\\';
    res.bSuppressShift = true;
    return res;
  case 0x33:
    res.atomKey = bShift ? ':' : ';';
    res.bSuppressShift = true;
    return res;
  case 0x34:
    if (!bShift)
    {
      res.atomKey = '7';
      res.bForceShift = true;
    }
    else
    {
      res.atomKey = '2';
      res.bForceShift = true;
    }
    return res;
  case 0x35:
    res.atomKey = 'L';
    res.bForceCtrl = true;
    res.bSuppressShift = true;
    return res;
  case 0x46:
    res.atomKey = bShift ? 'C' : 'B';
    res.bForceCtrl = true;
    res.bSuppressShift = true;
    return res;
  case 0x36:
    res.atomKey = ',';
    if (bShift)
      res.bForceShift = true;
    else
      res.bSuppressShift = true;
    return res;
  case 0x37:
    res.atomKey = '.';
    if (bShift)
      res.bForceShift = true;
    else
      res.bSuppressShift = true;
    return res;
  case 0x38:
    res.atomKey = '/';
    if (bShift)
      res.bForceShift = true;
    else
      res.bSuppressShift = true;
    return res;
  case 0x39:
    res.atomKey = 0x03;
    return res;
  case 0x4F:
    res.atomKey = 0x02;
    res.bSuppressShift = true;
    return res;
  case 0x50:
    res.atomKey = 0x02;
    res.bForceShift = true;
    return res;
  case 0x51:
    res.atomKey = 0x01;
    res.bForceShift = true;
    return res;
  case 0x52:
    res.atomKey = 0x01;
    res.bSuppressShift = true;
    return res;
  default:
    break;
  }

  return res;
}

// ------------------------------------------------------------
// CWelcomeAnimation Implementatie
// ------------------------------------------------------------
static const unsigned ANIM_FRAME_COUNT = 168;
static const unsigned ANIM_FRAME_W = 256;
static const unsigned ANIM_FRAME_H = 142;
static const size_t ANIM_FRAME_PIXELS = ANIM_FRAME_W * ANIM_FRAME_H;

CWelcomeAnimation::CWelcomeAnimation()
    : m_pFrames(nullptr), m_TotalFrames(ANIM_FRAME_COUNT), m_SubFrameCounter(0)
{
  for (int i = 0; i < 4; i++)
  {
    m_IsPlaying[i] = true;
    m_FrameDelay[i] = 0;

    // Cores 0 en 3 beginnen bij 0 en tellen omhoog (+1)
    // Cores 1 en 2 beginnen bij max-1 (167) en tellen omlaag (-1)
    if (i == 0 || i == 3)
    {
      m_CurrentFrame[i] = 0;
      m_FrameDir[i] = 1;
    }
    else
    {
      m_CurrentFrame[i] = m_TotalFrames - 1;
      m_FrameDir[i] = -1;
    }
  }
}

CWelcomeAnimation::~CWelcomeAnimation()
{
  Free();
}

void CWelcomeAnimation::Free(void)
{
  if (m_pFrames != nullptr)
  {
    for (int i = 0; i < 4; i++)
    {
      m_IsPlaying[i] = false;
    }

    asm volatile("dmb sy" ::: "memory");

    delete[] m_pFrames;
    m_pFrames = nullptr;
  }
}

bool CWelcomeAnimation::IsPlaying(unsigned coreId) const
{
  return (coreId < 4) ? m_IsPlaying[coreId] : false;
}

bool CWelcomeAnimation::IsAnyPlaying(void) const
{
  for (int i = 0; i < 4; i++)
  {
    if (m_IsPlaying[i])
      return true;
  }
  return false;
}

void DecodeAndProcessFrame(const uint8_t *bmpData, uint32_t *pTargetFrame, unsigned width, unsigned height)
{
  if (bmpData[0] != 'B' || bmpData[1] != 'M')
  {
    return;
  }

  uint32_t dataOffset = *(const uint32_t *)(bmpData + 10);
  uint16_t bpp = *(const uint16_t *)(bmpData + 28);
  int32_t bmpHeight = *(const int32_t *)(bmpData + 22);
  bool isBottomUp = (bmpHeight > 0);

  size_t rowStrideBytes = ((width * bpp + 31) / 32) * 4;
  const uint8_t *pPixelBase = bmpData + dataOffset;

  for (unsigned y = 0; y < height; ++y)
  {
    unsigned srcY = isBottomUp ? (height - 1 - y) : y;
    const uint8_t *srcRow = pPixelBase + (srcY * rowStrideBytes);
    uint32_t *dstRow = pTargetFrame + (y * width);

    if (bpp == 24)
    {
      for (unsigned x = 0; x < width; ++x)
      {
        uint8_t b = srcRow[x * 3 + 0];
        uint8_t g = srcRow[x * 3 + 1];
        uint8_t r = srcRow[x * 3 + 2];
        dstRow[x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
      }
    }
    else if (bpp == 32)
    {
      for (unsigned x = 0; x < width; ++x)
      {
        uint8_t b = srcRow[x * 4 + 0];
        uint8_t g = srcRow[x * 4 + 1];
        uint8_t r = srcRow[x * 4 + 2];
        uint8_t a = srcRow[x * 4 + 3];
        dstRow[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
      }
    }
  }
}

bool CWelcomeAnimation::Initialize(const char *pBinPath)
{
  Free();

  FIL file;
  if (f_open(&file, pBinPath, FA_READ | FA_OPEN_EXISTING) != FR_OK)
  {
    return false;
  }

  const size_t TEMP_BUF_SIZE = 160 * 1024;
  uint8_t *pTempBmp = new uint8_t[TEMP_BUF_SIZE];
  if (!pTempBmp)
  {
    f_close(&file);
    return false;
  }

  UINT bytesRead = 0;
  if (f_read(&file, pTempBmp, 54, &bytesRead) != FR_OK || bytesRead < 54)
  {
    delete[] pTempBmp;
    f_close(&file);
    return false;
  }

  if (pTempBmp[0] != 'B' || pTempBmp[1] != 'M')
  {
    delete[] pTempBmp;
    f_close(&file);
    return false;
  }

  uint32_t singleFrameSize = *(const uint32_t *)(pTempBmp + 2);
  if (singleFrameSize == 0 || singleFrameSize > TEMP_BUF_SIZE)
  {
    delete[] pTempBmp;
    f_close(&file);
    return false;
  }

  f_lseek(&file, 0);

  m_pFrames = new u32[ANIM_FRAME_COUNT * ANIM_FRAME_PIXELS];
  if (!m_pFrames)
  {
    delete[] pTempBmp;
    f_close(&file);
    return false;
  }

  unsigned loadedFrames = 0;
  for (unsigned f = 0; f < ANIM_FRAME_COUNT; ++f)
  {
    bytesRead = 0;
    FRESULT res = f_read(&file, pTempBmp, singleFrameSize, &bytesRead);
    if (res != FR_OK || bytesRead != singleFrameSize)
    {
      break;
    }

    uint32_t *pTarget = m_pFrames + (f * ANIM_FRAME_PIXELS);
    DecodeAndProcessFrame(pTempBmp, pTarget, ANIM_FRAME_W, ANIM_FRAME_H);
    loadedFrames++;
  }

  f_close(&file);
  delete[] pTempBmp;

  if (loadedFrames == 0)
  {
    Free();
    return false;
  }

  m_TotalFrames = loadedFrames;
  asm volatile("dsb sy" ::: "memory");
  return true;
}

void CWelcomeAnimation::StopCore(unsigned coreId)
{
  if (coreId < 4)
  {
    m_IsPlaying[coreId] = false;
    asm volatile("dmb sy" ::: "memory");

    if (CAtomRunner::Get() != nullptr)
    {
      CAtomRunner::Get()->GetEmulator(coreId)->ForceFullRedraw();
    }

    if (!IsAnyPlaying())
    {
      Free();
    }
  }
}

void CWelcomeAnimation::UpdateAnimation(void)
{
  m_SubFrameCounter++;
  if (m_SubFrameCounter >= 2)
  {
    m_SubFrameCounter = 0;

    for (int i = 0; i < 4; i++)
    {
      if (!m_IsPlaying[i])
        continue;

      m_FrameDelay[i]++;
      if (m_FrameDelay[i] > i)
      {
        m_FrameDelay[i] = 0;

        // Stap in de huidige richting
        m_CurrentFrame[i] += m_FrameDir[i];

        // Bovenste grens bereikt: 168 frames betekent index 0 t/m 167
        if (m_CurrentFrame[i] >= m_TotalFrames)
        {
          m_CurrentFrame[i] = m_TotalFrames - 2; // Terug naar 166 (frame 167 is getoond)
          m_FrameDir[i] = -1;                    // Draai om: loop omlaag
        }
        // Onderste grens bereikt
        else if (m_CurrentFrame[i] < 0)
        {
          m_CurrentFrame[i] = 1; // Terug naar 1 (frame 0 is getoond)
          m_FrameDir[i] = 1;     // Draai om: loop omhoog
        }
      }
    }
  }
}

void CWelcomeAnimation::DrawCurrentFrameCentered(u32 *pFB, u32 pitch, u32 startX, u32 startY, unsigned coreId)
{
  if (!m_pFrames || coreId >= 4)
  {
    return;
  }

  constexpr int WIN_W = 512;
  constexpr int WIN_H = 384;
  constexpr int offsetX = (WIN_W - ANIM_FRAME_W) / 2;
  constexpr int offsetY = (WIN_H - ANIM_FRAME_H) / 2;

  int frameIdx = m_CurrentFrame[coreId];
  const u32 *pSrcFrame = &m_pFrames[frameIdx * ANIM_FRAME_PIXELS];
  const size_t rowBytes = ANIM_FRAME_W * sizeof(u32);

  for (unsigned y = 0; y < ANIM_FRAME_H; y++)
  {
    u32 *pDstRow = pFB + ((startY + offsetY + y) * pitch) + startX + offsetX;
    const u32 *pSrcRow = &pSrcFrame[y * ANIM_FRAME_W];
    memcpy(pDstRow, pSrcRow, rowBytes);
  }
}

// ------------------------------------------------------------
// CAtomRunner Implementatie
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
      m_bShutdown(FALSE)
{
  s_pThis = this;
}

CAtomRunner::~CAtomRunner(void)
{
  delete m_pConsole;
  s_pThis = nullptr;
}

CAtomRunner *CAtomRunner::Get(void)
{
  return s_pThis;
}

CAtomEmulator *CAtomRunner::GetEmulator(unsigned nCore)
{
  return (nCore < 4) ? &m_AtomEmulator[nCore] : &m_AtomEmulator[0];
}

CTextConsole *CAtomRunner::GetConsole(void)
{
  return m_pConsole;
}

CBcmFrameBuffer *CAtomRunner::GetFrameBuffer(void) const
{
  return m_pFrameBuffer;
}

boolean CAtomRunner::Initialize(void)
{
  boolean bOK = TRUE;

  for (int c = 0; c < 4; c++)
  {
    memset(s_LastVRAM[c], 0x01, sizeof(s_LastVRAM[c]));
    s_InitializedVRAM[c] = false;
  }

  m_pConsole = new CTextConsole(m_pFrameBuffer, 1040, 24, 0xFFFFFFFF, 0xFF080808);
  if (m_pConsole != NULL)
  {
    bOK = TRUE;
    m_pConsole->WriteString("Quatom Multi-Core Monitor Ready.\r\n");
    m_pConsole->WriteString("-----------------------------------------\r\n");
  }

  if (bOK)
  {
    CLogger::Get()->Write(FromAtom, LogNotice, "Bezig met laden van AtomMP5.bin...");
    if (!g_WelcomeAnim.Initialize("SD:/AtomMP5.bin"))
    {
      CLogger::Get()->Write(FromAtom, LogError, "Kan AtomMP5.bin niet laden, we gaan door...");
    }
    else
    {
      CLogger::Get()->Write(FromAtom, LogNotice, "AtomMP5.bin succesvol geladen!");
    }
  }

  if (bOK)
  {
    bOK = CMultiCoreSupport::Initialize();

    if (bOK)
    {
      m_pConsole->WriteString("Multicore started\r\n");
      for (int core = 0; core < 4; core++)
      {
        if (bOK)
        {
          bOK = m_AtomEmulator[core].Initialize(core);
          if (bOK)
          {
            CLogger::Get()->Write(FromAtom, LogNotice, "--> Starten van AtomEmulator %d", core);

            for (int f = 0; f < 5; f++)
            {
              m_AtomEmulator[core].Exec6502(312, 64);
            }
          }
        }
      }

      if (m_pFrameBuffer != nullptr && m_pFrameBuffer->GetBuffer() != 0)
      {
        u32 *pBuffer = (u32 *)(uintptr_t)m_pFrameBuffer->GetBuffer();
        int pitchWords = (int)(m_pFrameBuffer->GetPitch() / 4);

        for (int core = 0; core < 4; core++)
        {
          int baseX = (core & 1) ? 512 : 0;
          int baseY = (core & 2) ? 384 : 0;

          const uint8_t *pVRAM = m_AtomEmulator[core].GetRAM() + 0x8000;
          const uint8_t *pFont = m_AtomEmulator[core].GetVIDEO().GetFontData();

          // Bepaal achtergrondkleur: grijs als dit het actieve venster is
          u32 textBgColor = (core == (int)m_nActiveCore) ? 0xFF202020 : 0xFF000000;

          for (int row = 0; row < 16; row++)
          {
            CAtomVideoDirect::RenderTextRowDirect(
                pBuffer, pitchWords, baseX, baseY + (row * 24),
                &pVRAM[row * 32], pFont, textBgColor);
          }

          m_AtomEmulator[core].ForceFullRedraw();
        }
      }
    }
  }

  if (bOK)
  {
    m_bCoreInitDone = TRUE;
  }

  return bOK;
}

void CAtomRunner::Run(unsigned nCore)
{
  if (nCore > 0)
  {
    while (!m_bCoreInitDone)
    {
      asm volatile("dmb sy" ::: "memory");
    }

    unsigned coreIdx = nCore - 1;
    if (coreIdx < 3)
    {
      CLogger::Get()->Write(FromAtom, LogNotice, "Core %d: Acorn Atom Emulator gestart...", nCore);

      u64 nNextFrameTime = m_pTimer->GetClockTicks64();

      while (!m_bShutdown)
      {
        u64 nCurrentTime = m_pTimer->GetClockTicks64();

        if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
          nNextFrameTime = nCurrentTime;

        ProcessEmulatorFrame(coreIdx);

        nNextFrameTime += FRAME_TIME_US;
        while (m_pTimer->GetClockTicks64() < nNextFrameTime)
        {
          asm volatile("yield");
        }
      }
    }
    return;
  }

  CLogger::Get()->Write(FromAtom, LogNotice, "Core 0 Run-loop gestart (USB, I/O Master & Emulator 3)...");
  u64 nNextFrameTime = m_pTimer->GetClockTicks64();
  static u64 nLastLEDTime = 0;

  while (!m_bShutdown)
  {
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

    u64 nCurrentTime = m_pTimer->GetClockTicks64();
    if (m_pKeyboard != nullptr && (nCurrentTime - nLastLEDTime >= 100000))
    {
      nLastLEDTime = nCurrentTime;
      m_pKeyboard->UpdateLEDs();
    }

    if (nCurrentTime >= nNextFrameTime)
    {
      if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
      {
        nNextFrameTime = nCurrentTime;
      }

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

      ProcessEmulatorFrame(3);

      static u64 nLastVUTime = 0;
      if (nCurrentTime - nLastVUTime >= 250000)
      {
        nLastVUTime = nCurrentTime;
        const int barX = (2 * 512) + 10;
        const int barY = m_pFrameBuffer->GetHeight() - 50;
        DrawVUMeter(*m_pFrameBuffer, barX, barY, 325, 40, g_nCoreLoad, s_pThis->m_nActiveCore);
      }

      nNextFrameTime += FRAME_TIME_US;
    }
    asm volatile("yield");
  }
}

void CAtomRunner::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6])
{
  if (!s_pThis)
    return;

  static uint8_t s_lastHID[6] = {0};
  static uint8_t s_lastAtomKey[6] = {0};

  unsigned activeCore = s_pThis->m_nActiveCore;
  unsigned newCore = activeCore;

  for (int i = 0; i < 6; i++)
  {
    uint8_t hid = RawKeys[i] & 0xFF;
    if (!hid)
      continue;

    if (hid == 0x42) // F9
    {
      g_bTrueFullscreen = !g_bTrueFullscreen;
      g_bFullscreenMode = g_bTrueFullscreen;

      ClearEntireHDMIFrameBuffer(*s_pThis->m_pFrameBuffer);

      if (g_bFullscreenMode)
      {
        g_FullscreenCore = activeCore;
        if (g_WelcomeAnim.IsPlaying(activeCore))
        {
          g_WelcomeAnim.StopCore(activeCore);
        }
        s_InitializedVRAM[activeCore] = false;
        s_pThis->m_AtomEmulator[activeCore].ForceFullRedraw();
      }
      else
      {
        for (int c = 0; c < 4; c++)
        {
          g_SkipCounter[c] = 5;
          s_InitializedVRAM[c] = false;
          memset(s_LastVRAM[c], 0x01, sizeof(s_LastVRAM[c]));
          s_pThis->m_AtomEmulator[c].ForceFullRedraw();
        }
      }
      continue;
    }
// F11 -> Toggle Turbo Mode (1 MHz <-> 5 MHz) voor de momenteel actieve core
    if (hid == 0x44)
    {
      s_pThis->m_AtomEmulator[activeCore].ToggleTurbo();
      bool bNowTurbo = s_pThis->m_AtomEmulator[activeCore].IsTurbo();

      CLogger::Get()->Write("KeyDebug", LogNotice, 
          "F11: Turbo Mode op Core %u = %s (%s)", 
          activeCore, 
          bNowTurbo ? "AAN (5 MHz)" : "UIT (1 MHz)",
          bNowTurbo ? "Warp Speed" : "Standaard");
      continue;
    }

    // F12 -> Hardware Reset actieve core
    if (hid == 0x45) // F12
    {
      s_pThis->m_AtomEmulator[activeCore].Reset();
      s_pThis->m_AtomEmulator[activeCore].ForceFullRedraw();
      CLogger::Get()->Write("KeyDebug", LogNotice, "F12: Reset uitgevoerd op Core %d", activeCore);
      continue;
    }

    if (hid >= 0x3A && hid <= 0x3D) // F1..F4
    {
      newCore = hid - 0x3A;
      if (g_bFullscreenMode)
      {
        g_bFullscreenMode = false;
        ClearEntireHDMIFrameBuffer(*s_pThis->m_pFrameBuffer);
      }
      for (int c = 0; c < 4; c++)
      {
        g_SkipCounter[c] = 5;
        s_InitializedVRAM[c] = false;
        memset(s_LastVRAM[c], 0x01, sizeof(s_LastVRAM[c]));
        s_pThis->m_AtomEmulator[c].ForceFullRedraw();
      }

      if (g_WelcomeAnim.IsPlaying(newCore))
      {
        g_WelcomeAnim.StopCore(newCore);
      }
      continue;
    }

    if (hid >= 0x3E && hid <= 0x41) // F5..F8
    {
      g_bFullscreenMode = true;
      ClearEntireHDMIFrameBuffer(*s_pThis->m_pFrameBuffer);
      g_FullscreenCore = (hid - 0x3E);
      newCore = g_FullscreenCore;
      g_WelcomeAnim.StopCore(newCore);
      s_InitializedVRAM[newCore] = false;
      s_pThis->m_AtomEmulator[newCore].ForceFullRedraw();
      continue;
    }
  }

  if (newCore != activeCore)
  {
    unsigned oldCore = activeCore;

    s_pThis->m_AtomEmulator[activeCore].ClearKeyboard();
    memset(s_lastHID, 0, sizeof(s_lastHID));
    memset(s_lastAtomKey, 0, sizeof(s_lastAtomKey));
    s_pThis->m_nActiveCore = newCore;
    activeCore = newCore;

    // Forceer redraw van zowel het oude als het nieuwe kwadrant
    s_InitializedVRAM[oldCore] = false;
    s_InitializedVRAM[newCore] = false;
    s_pThis->m_AtomEmulator[oldCore].ForceFullRedraw();
    s_pThis->m_AtomEmulator[newCore].ForceFullRedraw();
  }

  CAtomEmulator &emu = s_pThis->m_AtomEmulator[activeCore];

  bool physShift = (ucModifiers & 0x22) != 0;
  bool physCtrl = (ucModifiers & 0x11) != 0;
  bool physAlt = (ucModifiers & 0x44) != 0;

  bool forceShift = false;
  bool suppressShift = false;
  bool forceCtrl = false;

  for (int i = 0; i < 6; i++)
  {
    if (!RawKeys[i])
      continue;
    TAtomKeyResult r = ConvertHIDToAtomKeyCustom(RawKeys[i], ucModifiers);
    if (r.bForceShift)
      forceShift = true;
    if (r.bSuppressShift)
      suppressShift = true;
    if (r.bForceCtrl)
      forceCtrl = true;
  }

  bool effectiveShift = (physShift || forceShift) && !suppressShift;
  bool effectiveCtrl = physCtrl || forceCtrl;

  emu.SetShiftState(effectiveShift);
  emu.SetCtrlState(effectiveCtrl);
  emu.SetReptState(physAlt);

  for (int i = 0; i < 6; i++)
  {
    uint8_t hid = RawKeys[i] & 0xFF;
    if (!hid || (hid >= 0x3A && hid <= 0x45))
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
      TAtomKeyResult r = ConvertHIDToAtomKeyCustom(hid, ucModifiers);
      if (r.atomKey)
      {
        if (g_WelcomeAnim.IsPlaying(activeCore))
        {
          g_WelcomeAnim.StopCore(activeCore);
          s_InitializedVRAM[activeCore] = false;
          emu.ForceFullRedraw();
        }

        emu.KeyDown(r.atomKey);
        s_lastAtomKey[i] = r.atomKey;
        s_lastHID[i] = hid;
      }
    }
  }

  for (int i = 0; i < 6; i++)
  {
    uint8_t oldHID = s_lastHID[i];
    if (!oldHID)
      continue;

    bool stillPressed = false;
    for (int j = 0; j < 6; j++)
    {
      if ((RawKeys[j] & 0xFF) == oldHID)
      {
        stillPressed = true;
        break;
      }
    }

    if (!stillPressed)
    {
      uint8_t atomKey = s_lastAtomKey[i];
      if (atomKey)
      {
        emu.KeyUp(atomKey);
      }
      s_lastAtomKey[i] = 0;
      s_lastHID[i] = 0;
    }
  }

  bool anyPressed = false;
  for (int i = 0; i < 6; i++)
  {
    if (RawKeys[i] != 0)
    {
      anyPressed = true;
      break;
    }
  }

  if (!anyPressed)
  {
    emu.ClearKeyboard();
    memset(s_lastHID, 0, sizeof(s_lastHID));
    memset(s_lastAtomKey, 0, sizeof(s_lastAtomKey));
  }

  g_SkipCounter[activeCore] = 5;
  emu.ForceVRAMChanged();
}

void CAtomRunner::KeyboardRemovedHandler(CDevice *pDevice, void *pContext)
{
  if (s_pThis != nullptr)
  {
    CLogger::Get()->Write(FromAtom, LogNotice, "USB Toetsenbord ontkoppeld.");
    s_pThis->m_pKeyboard = nullptr;
    s_pThis->m_bKeyboardReady = FALSE;
  }
}

// ------------------------------------------------------------
// Fullscreen Text Mode (Mode 0) Renderer
// ------------------------------------------------------------
static void RenderSingleCoreFullscreenText(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pTextVRAM)
{
  if (!pTextVRAM || !frameBuffer.GetBuffer())
    return;

  u32 *pFB = GetFrameBufferPtr(frameBuffer);
  if (!pFB)
    return;

  const u32 pitchWords = frameBuffer.GetPitch() / 4;
  const u32 fbWidth = frameBuffer.GetWidth();
  const u32 fbHeight = frameBuffer.GetHeight();

  const int scaleX = 4;
  const int scaleY = 4;
  const int dstW = 256 * scaleX;
  const int dstH = 192 * scaleY;

  int startX = 0;
  int startY = 0;

  if (g_bTrueFullscreen)
  {
    if (fbWidth > (unsigned)dstW)
      startX = (fbWidth - dstW) / 2;
    if (fbHeight > (unsigned)dstH)
      startY = (fbHeight - dstH) / 2;

    for (unsigned y = 0; y < fbHeight; y++)
    {
      u32 *pRow = pFB + y * pitchWords;
      for (unsigned x = 0; x < fbWidth; x++)
      {
        if (x < (unsigned)startX || x >= (unsigned)(startX + dstW) ||
            y < (unsigned)startY || y >= (unsigned)(startY + dstH))
        {
          pRow[x] = 0xFF000000;
        }
      }
    }
  }

  const u8 *pFont = CAtomRunner::Get()->GetEmulator(nCoreId)->GetVIDEO().GetFontData();

  for (int row = 0; row < 16; row++)
  {
    for (int col = 0; col < 32; col++)
    {
      u8 rawByte = pTextVRAM[row * 32 + col];
      int charBaseX = startX + (col * 8 * scaleX);
      int charBaseY = startY + (row * 12 * scaleY);

      if (rawByte & 0x40)
      {
        u32 blockColor = (rawByte & 0x80) ? Palette8[4] : Palette8[2];
        u32 bgColor = (nCoreId == 1 || nCoreId == 2) ? Palette8[0] : Palette8[9];

        for (int cy = 0; cy < 12; cy++)
        {
          bool bLeft = false, bRight = false;
          if (cy < 4)
          {
            bLeft = (rawByte & 0x20) != 0;
            bRight = (rawByte & 0x10) != 0;
          }
          else if (cy < 8)
          {
            bLeft = (rawByte & 0x08) != 0;
            bRight = (rawByte & 0x04) != 0;
          }
          else
          {
            bLeft = (rawByte & 0x02) != 0;
            bRight = (rawByte & 0x01) != 0;
          }

          u32 colLeft = bLeft ? blockColor : bgColor;
          u32 colRight = bRight ? blockColor : bgColor;

          for (int dy = 0; dy < scaleY; dy++)
          {
            int py = charBaseY + (cy * scaleY) + dy;
            if (py < 0 || py >= (int)fbHeight)
              continue;

            u32 *pDstRow = pFB + py * pitchWords;

            for (int dx = 0; dx < (4 * scaleX); dx++)
            {
              int px = charBaseX + dx;
              if (px >= 0 && px < (int)fbWidth)
                pDstRow[px] = colLeft;
            }
            for (int dx = (4 * scaleX); dx < (8 * scaleX); dx++)
            {
              int px = charBaseX + dx;
              if (px >= 0 && px < (int)fbWidth)
                pDstRow[px] = colRight;
            }
          }
        }
      }
      else
      {
        u8 fontIndex = rawByte & 0x3F;
        const u8 *pCharGlyph = &pFont[fontIndex * 12];
        bool bInvert = (rawByte & 0x80) != 0;

        for (int cy = 0; cy < 12; cy++)
        {
          u8 glyphBits = pCharGlyph[cy];
          if (bInvert)
            glyphBits = ~glyphBits;

          for (int dy = 0; dy < scaleY; dy++)
          {
            int py = charBaseY + (cy * scaleY) + dy;
            if (py < 0 || py >= (int)fbHeight)
              continue;

            u32 *pDstRow = pFB + py * pitchWords;

            for (int cx = 0; cx < 8; cx++)
            {
              bool bPixelOn = (glyphBits & (0x80 >> cx)) != 0;
              u32 color = bPixelOn ? 0xFF00FF00 : 0xFF000000;

              for (int dx = 0; dx < scaleX; dx++)
              {
                int px = charBaseX + (cx * scaleX) + dx;
                if (px >= 0 && px < (int)fbWidth)
                {
                  pDstRow[px] = color;
                }
              }
            }
          }
        }
      }
    }
  }
}

// ------------------------------------------------------------
// Fullscreen Graphic Mode (Mode 1..4) Renderer
// ------------------------------------------------------------
static void RenderSingleCoreFullscreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM)
{
  if (!pLocalVRAM || !frameBuffer.GetBuffer())
    return;

  constexpr int SRC_W = 256;
  constexpr int SRC_H = 192;

  const u32 fbWidth = frameBuffer.GetWidth();
  const u32 fbHeight = frameBuffer.GetHeight();
  const u32 pitchWords = frameBuffer.GetPitch() / 4;
  u32 *pFB = GetFrameBufferPtr(frameBuffer);

  const int scaleX = 4;
  const int scaleY = 4;
  const int dstW = SRC_W * scaleX;
  const int dstH = SRC_H * scaleY;

  int startX = 0;
  int startY = 0;

  if (g_bTrueFullscreen)
  {
    if (fbWidth > (unsigned)dstW)
      startX = (fbWidth - dstW) / 2;
    if (fbHeight > (unsigned)dstH)
      startY = (fbHeight - dstH) / 2;

    for (unsigned y = 0; y < fbHeight; y++)
    {
      u32 *pRow = pFB + y * pitchWords;
      for (unsigned x = 0; x < fbWidth; x++)
      {
        if (x < (unsigned)startX || x >= (unsigned)(startX + dstW) ||
            y < (unsigned)startY || y >= (unsigned)(startY + dstH))
        {
          pRow[x] = 0xFF000000;
        }
      }
    }
  }

  for (int y = 0; y < SRC_H; y++)
  {
    const u8 *pSrcRow = &pLocalVRAM[y * SRC_W];

    for (int x = 0; x < SRC_W; x++)
    {
      u32 P = CAtomVideo::GetColorARGB(pSrcRow[x]);
      if ((nCoreId == 1 || nCoreId == 2) && P == Palette8[0])
      {
        P = Palette8[8];
      }

      int dstX0 = startX + x * scaleX;
      int dstY0 = startY + y * scaleY;

      for (int dy = 0; dy < scaleY; dy++)
      {
        int py = dstY0 + dy;
        if (py < 0 || py >= (int)fbHeight)
          continue;

        u32 *pDstRow = pFB + py * pitchWords;
        for (int dx = 0; dx < scaleX; dx++)
        {
          int px = dstX0 + dx;
          if (px < 0 || px >= (int)fbWidth)
            continue;
          pDstRow[px] = P;
        }
      }
    }
  }
}

static void RenderCoreQuadrantDirect(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM)
{
  if (!pLocalVRAM)
    return;

  u32 *pFB = GetFrameBufferPtr(frameBuffer);
  if (!pFB)
    return;

  constexpr int SRC_W = 256;
  constexpr int SRC_H = 192;
  constexpr int QW = 512;

  const int pitchWords = (int)(frameBuffer.GetPitch() / 4);
  const int baseX = (nCoreId & 1) ? QW : 0;
  const int baseY = (nCoreId & 2) ? 384 : 0;

  if (g_WelcomeAnim.IsPlaying(nCoreId))
  {
    u32 *pQuadStart = pFB + (baseY * pitchWords) + baseX;
    g_WelcomeAnim.DrawCurrentFrameCentered(pQuadStart, pitchWords, 0, 0, nCoreId);
    s_InitializedVRAM[nCoreId] = false;
    CAtomRunner::Get()->GetEmulator(nCoreId)->ResetVRAMChanged();
    return;
  }

  u8 *pLastVRAM = s_LastVRAM[nCoreId];
  bool forceFullRedraw = !s_InitializedVRAM[nCoreId];
  if (forceFullRedraw)
  {
    s_InitializedVRAM[nCoreId] = true;
  }

  for (int y = 0; y < SRC_H; y++)
  {
    const u8 *pSrcRow = &pLocalVRAM[y * SRC_W];
    u8 *pLastRow = &pLastVRAM[y * SRC_W];

    bool lineChanged = forceFullRedraw || (memcmp(pSrcRow, pLastRow, SRC_W) != 0);
    if (!lineChanged)
      continue;

    memcpy(pLastRow, pSrcRow, SRC_W);

    u32 *pDstRow1 = pFB + ((baseY + (y * 2 + 0)) * pitchWords) + baseX;
    u32 *pDstRow2 = pFB + ((baseY + (y * 2 + 1)) * pitchWords) + baseX;

    bool bIsActiveCore = (!g_bFullscreenMode && nCoreId == CAtomRunner::m_nActiveCore);
    u32 activeTextBg = 0xFF222222; // Subtiel donkergrijs

    for (int x = 0; x < SRC_W; x++)
    {
      u32 P = CAtomVideo::GetColorARGB(pSrcRow[x]);

      // Als het zwart is en deze core is actief in window mode
      if (P == Palette8[0])
      {
        if (bIsActiveCore)
        {
          P = activeTextBg;
        }
        else if (nCoreId == 1 || nCoreId == 2)
        {
          P = Palette8[8];
        }
      }

      pDstRow1[x * 2 + 0] = P;
      pDstRow1[x * 2 + 1] = P;
      pDstRow2[x * 2 + 0] = P;
      pDstRow2[x * 2 + 1] = P;
    }
  }
}

// ------------------------------------------------------------
// ProcessEmulatorFrame
// ------------------------------------------------------------
void CAtomRunner::ProcessEmulatorFrame(unsigned coreIdx)
{
  const u64 tStart = m_pTimer->GetClockTicks64();

  m_AtomEmulator[coreIdx].Update();

  u8 gfxMode = m_AtomEmulator[coreIdx].GetGfxMode();
  bool bAnimPlaying = g_WelcomeAnim.IsPlaying(coreIdx);

  if (g_bFullscreenMode)
  {
    if (g_FullscreenCore == coreIdx)
    {
      const bool bVRAM = m_AtomEmulator[coreIdx].HasVRAMChanged();
      if (bVRAM || (++g_SkipCounter[coreIdx] >= 5))
      {
        g_SkipCounter[coreIdx] = 0;

        if (gfxMode == 0)
        {
          RenderSingleCoreFullscreenText(*m_pFrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetRAM() + 0x8000);
        }
        else
        {
          RenderSingleCoreFullscreen(*m_pFrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
        }
        m_AtomEmulator[coreIdx].ResetVRAMChanged();
      }
    }
  }
  else
  {
    if (bAnimPlaying || gfxMode > 0)
    {
      const bool bVRAM = m_AtomEmulator[coreIdx].HasVRAMChanged();
      if (bAnimPlaying || bVRAM || (++g_SkipCounter[coreIdx] >= 5))
      {
        g_SkipCounter[coreIdx] = 0;
        RenderCoreQuadrantDirect(*m_pFrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
        m_AtomEmulator[coreIdx].ResetVRAMChanged();
      }
    }
  }

  const u64 tWork = m_pTimer->GetClockTicks64() - tStart;
  unsigned rawLoad = (unsigned)((tWork * 100) / FRAME_TIME_US);
  if (rawLoad > 100)
    rawLoad = 100;
  g_nCoreLoad[coreIdx] = (g_nCoreLoad[coreIdx] * 7 + rawLoad) / 8;
}