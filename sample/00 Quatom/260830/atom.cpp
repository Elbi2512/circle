#include "atom.h"
#include <circle/logger.h>
#include <circle/util.h>

#define DBG(fmt, ...) CLogger::Get()->Write("DBG", LogDebug, fmt, ##__VA_ARGS__)

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
    0xFF101010};

static u8 s_LastVRAM[4][256 * 192] = {0};
static bool s_InitializedVRAM[4] = {false};

static inline u32 *GetFrameBufferPtr(CBcmFrameBuffer &fb)
{
  return (u32 *)(uintptr_t)fb.GetBuffer();
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

  const int gap = 2; // Tussenruimte tussen de balkjes in pixels
  int subW = (width - gap) / 2;
  int subH = (height - gap) / 2;

  if (subW <= 4 || subH <= 2)
    return;

  // Teken 4 kwadranten binnen het opgegeven (x, y, width, height) blok:
  // c=0: Kwadrant 1 (linksboven)   c=1: Kwadrant 2 (rechtsboven)
  // c=2: Kwadrant 3 (linksonder)   c=3: Kwadrant 4 (rechtsonder)
  for (unsigned c = 0; c < 4; c++)
  {
    bool bHighlight = (nCore == c);
    unsigned val = (pct[c] > 100) ? 100 : pct[c];

    // Positie binnen het 2x2 raster
    int qX = x + ((c & 1) ? (subW + gap) : 0);
    int qY = y + ((c >= 2) ? (subH + gap) : 0);

    int filledWidth = (subW * (int)val) / 100;

    // Kleurbepaling
    u32 fillColor;
    if (bHighlight)
    {
      fillColor = (val < 60)   ? 0xFF00FF00  // Fel groen
                  : (val < 85) ? 0xFFFFFF00  // Fel geel
                               : 0xFFFF0000; // Fel rood
    }
    else
    {
      fillColor = (val < 60)   ? 0xFF007F00  // Gedimd groen
                  : (val < 85) ? 0xFF7F7F00  // Gedimd geel
                               : 0xFF7f0000; // Gedimd rood
    }

    u32 bgColor = 0xFF181818;
    u32 borderColor = bHighlight ? 0xFFFFFFFF : 0xFF7F7F7F;
    int borderSize = bHighlight ? 2 : 1; // Dikkere rand voor actieve core

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
        // Buitenste rand van het balkje
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

static void RenderCoreQuadrantBuffer(u32 *pDestBuffer, unsigned nCoreId, const u8 *pLocalVRAM)
{
  if (!pLocalVRAM || !pDestBuffer)
    return;

  constexpr int SRC_W = 256;
  constexpr int SRC_H = 192;
  constexpr int DST_W = 512;

  if (g_WelcomeAnim.IsPlaying(nCoreId))
  {
    g_WelcomeAnim.DrawCurrentFrameCentered(pDestBuffer, DST_W, 0, 0, nCoreId);
    s_InitializedVRAM[nCoreId] = false;
    CAtomRunner::Get()->GetEmulator(nCoreId)->ResetVRAMChanged();
    return;
  }

  u8 *pLastVRAM = s_LastVRAM[nCoreId];
  bool forceFullRedraw = !s_InitializedVRAM[nCoreId];
  if (forceFullRedraw)
  {
    memcpy(pLastVRAM, pLocalVRAM, SRC_W * SRC_H);
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

    u32 *pDstRow1 = pDestBuffer + ((2 * y + 0) * DST_W);
    u32 *pDstRow2 = pDestBuffer + ((2 * y + 1) * DST_W);

    for (int x = 0; x < SRC_W; x++)
    {
      u32 P = CAtomVideo::GetColorARGB(pSrcRow[x]);
      if ((nCoreId == 1 || nCoreId == 2) && P == Palette8[0])
      {
        P = Palette8[8];
      }
      pDstRow1[2 * x + 0] = P;
      pDstRow1[2 * x + 1] = P;
      pDstRow2[2 * x + 0] = P;
      pDstRow2[2 * x + 1] = P;
    }
  }
}

#include <string.h>

static void BlitQuadrantToScreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u32 *pSourceBuffer)
{
  if (!pSourceBuffer)
    return;

  u32 *pFB = GetFrameBufferPtr(frameBuffer);
  if (!pFB)
    return;

  const int fbW = (int)frameBuffer.GetWidth();
  const int fbH = (int)frameBuffer.GetHeight();
  const int pitch = (int)frameBuffer.GetPitch() / 4;

  constexpr int QW = 512;
  constexpr int QH = 384;

  // Exacte 2x2 grid-offsets zonder tussenruimte/offsets voor borders
  // Core 0: (0,   0)    | Core 1: (512,   0)
  // Core 2: (0, 384)    | Core 3: (512, 384)
  const int baseX = (nCoreId & 1) ? QW : 0;
  const int baseY = (nCoreId & 2) ? QH : 0;

  if (baseX + QW > fbW || baseY + QH > fbH)
    return;

  for (int y = 0; y < QH; ++y)
  {
    const u32 *pSrcRow = pSourceBuffer + (y * QW);
    u32 *pDstRow = pFB + (baseY + y) * pitch + baseX;

    // Snelle geheugenkopie per scanline (512 pixels * 4 bytes = 2048 bytes)
    memcpy(pDstRow, pSrcRow, QW * sizeof(u32));
  }
}

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

  const int scale = 4;
  const int dstW = SRC_W * scale;
  const int dstH = SRC_H * scale;

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
        if (x < (unsigned)startX || x >= (unsigned)(startX + dstW) || y < (unsigned)startY || y >= (unsigned)(startY + dstH))
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

      int dstX0 = startX + x * scale;
      int dstY0 = startY + y * scale;

      for (int dy = 0; dy < scale; dy++)
      {
        int py = dstY0 + dy;
        if (py < 0 || py >= (int)fbHeight)
          continue;

        u32 *pDstRow = pFB + py * pitchWords;
        for (int dx = 0; dx < scale; dx++)
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

// ------------------------------------------------------------
// Keyboard translation
// ------------------------------------------------------------
struct TAtomKeyResult
{
  uint8_t atomKey;
  bool bForceShift;
  bool bSuppressShift;
};

static TAtomKeyResult ConvertHIDToAtomKeyCustom(uint8_t hidCode, uint8_t ucModifiers)
{
  TAtomKeyResult res = {0, false, false};

  // bShift: true als één van de Shift-keys actief is (links/rechts)
  bool bShift = (ucModifiers & 0x22) != 0;

  // -------------------------------------------------------------------------
  // 1. Letters A t/m Z (HID 0x04 t/m 0x1D)
  //
  // Atom: in "teletype mode" standaard uppercase; SHIFT kan lowercase geven,
  // maar dat wordt elders afgehandeld. Hier mappen we direct naar 'A'..'Z'.
  // -------------------------------------------------------------------------
  if (hidCode >= 0x04 && hidCode <= 0x1D)
  {
    res.atomKey = 'A' + (hidCode - 0x04);
    return res;
  }

  // -------------------------------------------------------------------------
  // 2. Cijfertoetsen 1 t/m 0 (HID 0x1E t/m 0x27)
  //
  // Zonder Shift: 1 2 3 4 5 6 7 8 9 0
  // Met Shift:    Atom-specifieke symbolen, gemapt op de Atom-matrix:
  //   Shift+1 -> !  (via Shift+1 op Atom)
  //   Shift+2 -> @  (via @-toets zonder Shift)
  //   Shift+3 -> #  (via Shift+3)
  //   Shift+4 -> $  (via Shift+4)
  //   Shift+5 -> %  (via Shift+5)
  //   Shift+6 -> TAB (speciaal: doorgeven 0x09)
  //   Shift+7 -> &  (via Shift+6)
  //   Shift+8 -> *  (via Shift+: op Atom)
  //   Shift+9 -> (  (via Shift+8)
  //   Shift+0 -> )  (via Shift+9)
  // -------------------------------------------------------------------------
  if (hidCode >= 0x1E && hidCode <= 0x27)
  {
    if (!bShift)
    {
      // Onge-shift: normale cijfers
      res.atomKey = (hidCode == 0x27) ? '0' : ('1' + (hidCode - 0x1E));
      return res;
    }
    else
    {
      // Shift-variant: Atom-matrix emulatie
      switch (hidCode)
      {
      case 0x1E: // Shift + 1 -> ! (Atom: Shift+1)
        res.atomKey = '1';
        res.bForceShift = true;
        return res;

      case 0x1F: // Shift + 2 -> @ (Atom: @-toets zonder Shift)
        res.atomKey = '@';
        res.bSuppressShift = true;
        return res;

      case 0x20: // Shift + 3 -> # (Atom: Shift+3)
        res.atomKey = '3';
        res.bForceShift = true;
        return res;

      case 0x21: // Shift + 4 -> $ (Atom: Shift+4)
        res.atomKey = '4';
        res.bForceShift = true;
        return res;

      case 0x22: // Shift + 5 -> % (Atom: Shift+5)
        res.atomKey = '5';
        res.bForceShift = true;
        return res;

      // ★ Speciaal: Shift + 6 moet TAB (0x09) doorgeven
      case 0x23:                   // HID 6
        res.atomKey = 0x09;        // Horizontal Tab
        res.bSuppressShift = true; // geen Atom-Shift nodig
        return res;

      case 0x24: // Shift + 7 -> & (Atom: Shift+6)
        res.atomKey = '6';
        res.bForceShift = true;
        return res;

        // ★ Speciaal: Shift + 8 moet * geven via Atom Shift+:
      case 0x25:
        res.atomKey = ':';
        res.bForceShift = true; // laat matrix SHIFT activeren
        return res;

      case 0x26: // Shift + 9 -> ( (Atom: Shift+8)
        res.atomKey = '8';
        res.bForceShift = true;
        return res;

      case 0x27: // Shift + 0 -> ) (Atom: Shift+9)
        res.atomKey = '9';
        res.bForceShift = true;
        return res;
      }
    }
  }

  // -------------------------------------------------------------------------
  // 3. Leestekens en symbolen
  //
  // Hier mappen we USB-punctuatie naar Atom-matrixposities en -gedrag.
  // -------------------------------------------------------------------------
  switch (hidCode)
  {
  // Spatiebalk (HID 0x2C)
  case 0x2C:
    res.atomKey = ' ';
    return res;

  // USB - / _ (HID 0x2D)
  //
  // Onge-shift: '-' (Atom: -)
  // Shift:      '_' (Atom: via Shift+P)
  case 0x2D:
    if (!bShift)
    {
      res.atomKey = '-';
      res.bSuppressShift = true;
    }
    else
    {
      res.atomKey = 'P'; // Atom: Shift+P = '_'
      res.bForceShift = true;
    }
    return res;

  // USB = / + (HID 0x2E)
  //
  // Onge-shift: '=' (Atom: via Shift+-)
  // Shift:      '+' (Atom: via Shift+;)
  case 0x2E:
    if (!bShift)
    {
      res.atomKey = '-'; // Atom: Shift+- = '='
      res.bForceShift = true;
    }
    else
    {
      res.atomKey = ';'; // Atom: Shift+; = '+'
      res.bForceShift = true;
    }
    return res;

    // USB ; / : (HID 0x33)
    //
    // Atom heeft aparte matrixposities voor ; en :
    // Onge-shift: ';'
    // Shift:      '+' (via Shift+;)
  case 0x33: // HID ;
    if (!bShift)
    {
      res.atomKey = ';'; // onge-shift: ;
      res.bSuppressShift = true;
    }
    else
    {
      res.atomKey = ':'; // SHIFT ; → :
      res.bSuppressShift = true;
    }
    return res;

  // USB ` (linksboven, HID 0x35)
  //
  // We gebruiken deze als Atom ':'-toets:
  // Onge-shift: ':'  (Atom: :)
  // Shift:      '*'  (Atom: Shift+:)
  case 0x35:
    if (!bShift)
    {
      res.atomKey = ':'; // : onge-shift
      res.bSuppressShift = true;
    }
    else
    {
      res.atomKey = ':'; // : met Shift -> '*'
      res.bForceShift = true;
    }
    return res;

  // USB ' / " (HID 0x34)
  //
  // Atom:
  //   '  via Shift+7
  //   "  via Shift+2
  case 0x34:
    if (!bShift)
    {
      res.atomKey = '7'; // Shift+7 = '
      res.bForceShift = true;
    }
    else
    {
      res.atomKey = '2'; // Shift+2 = "
      res.bForceShift = true;
    }
    return res;

  // USB [ / { (HID 0x2F)
  //
  // Atom: [ onge-shift, { via inverse/variant; hier gewoon '[' zonder Shift.
  case 0x2F:
    res.atomKey = '[';
    res.bSuppressShift = true;
    return res;

  // USB ] / } (HID 0x30)
  //
  // Atom: ] onge-shift, } via inverse/variant; hier gewoon ']' zonder Shift.
  case 0x30:
    res.atomKey = ']';
    res.bSuppressShift = true;
    return res;

  // USB \ / | (HID 0x31 / 0x32)
  //
  // Atom: '\' onge-shift, '|' via inverse; hier '\' zonder Shift.
  case 0x31:
  case 0x32:
    res.atomKey = '\\';
    res.bSuppressShift = true;
    return res;

  // USB , / < (HID 0x36)
  //
  // Onge-shift: ','  (Atom: ,)
  // Shift:      '<'  (Atom: Shift+,)
  case 0x36:
    res.atomKey = ',';
    if (bShift)
      res.bForceShift = true;
    else
      res.bSuppressShift = true;
    return res;

  // USB . / > (HID 0x37)
  //
  // Onge-shift: '.'  (Atom: .)
  // Shift:      '>'  (Atom: Shift+.)
  case 0x37:
    res.atomKey = '.';
    if (bShift)
      res.bForceShift = true;
    else
      res.bSuppressShift = true;
    return res;

  // USB / / ? (HID 0x38)
  //
  // Onge-shift: '/'  (Atom: /)
  // Shift:      '?'  (Atom: Shift+/)
  case 0x38:
    res.atomKey = '/';
    if (bShift)
      res.bForceShift = true;
    else
      res.bSuppressShift = true;
    return res;

  // -----------------------------------------------------------------------
  // 4. Navigatie & besturing (Enter, Escape, Backspace, Tab, Lock)
  // -----------------------------------------------------------------------
  case 0x28:            // Enter
    res.atomKey = '\r'; // CR (0x0D)
    return res;

  case 0x29:            // Escape
    res.atomKey = 0x1B; // ESC
    return res;

  case 0x2A:            // Backspace
    res.atomKey = 0x08; // BS
    return res;

  case 0x2B:            // Tab
    res.atomKey = 0x09; // HT
    return res;

  case 0x39:            // Lock (Atom LOCK)
    res.atomKey = 0x03; // CTRL-CODE voor LOCK (zoals Atom OS)
    return res;

  // -----------------------------------------------------------------------
  // 5. Pijltjestoetsen (cursor keys)
  //
  // Atom gebruikt control-codes voor cursorbeweging:
  //   0x02: horizontale beweging (links/rechts)
  //   0x01: verticale beweging (omhoog/omlaag)
  // SHIFT bepaalt richting.
  // -----------------------------------------------------------------------
  case 0x4F: // Right
    res.atomKey = 0x02;
    res.bSuppressShift = true; // rechts zonder Shift
    return res;

  case 0x50: // Left
    res.atomKey = 0x02;
    res.bForceShift = true; // links met Shift
    return res;

  case 0x51: // Down
    res.atomKey = 0x01;
    res.bForceShift = true; // omlaag met Shift
    return res;

  case 0x52: // Up
    res.atomKey = 0x01;
    res.bSuppressShift = true; // omhoog zonder Shift
    return res;

  default:
    break;
  }

  // Onbekende HID-code: geen toets
  return res;
}

// ------------------------------------------------------------
// CWelcomeAnimation implementatie
// ------------------------------------------------------------
CWelcomeAnimation::CWelcomeAnimation()
    : m_pFrames(nullptr), m_TotalFrames(210), m_SubFrameCounter(0)
{
  for (int i = 0; i < 4; i++)
  {
    m_IsPlaying[i] = true;
    m_CurrentFrame[i] = 0;
    m_FrameDelay[i] = 0;
  }
}

CWelcomeAnimation::~CWelcomeAnimation()
{
  Free();
}

bool CWelcomeAnimation::Initialize(const char *pBinPath)
{
  FIL file;
  FRESULT res = f_open(&file, pBinPath, FA_READ | FA_OPEN_EXISTING);
  if (res != FR_OK)
    return false;

  UINT fileSize = f_size(&file);
  if (fileSize == 0)
  {
    f_close(&file);
    return false;
  }

  m_pFrames = new u32[fileSize / sizeof(u32)];
  if (!m_pFrames)
  {
    f_close(&file);
    return false;
  }

  UINT bytesRead = 0;
  FRESULT readRes = f_read(&file, m_pFrames, fileSize, &bytesRead);
  f_close(&file);

  if (readRes != FR_OK || bytesRead != fileSize)
  {
    delete[] m_pFrames;
    m_pFrames = nullptr;
    return false;
  }

  asm volatile("dsb sy" ::
                   : "memory");
  return true;
}

bool CWelcomeAnimation::IsPlaying(unsigned coreId) const
{
  return (coreId < 4) ? m_IsPlaying[coreId] : false;
}

void CWelcomeAnimation::StopCore(unsigned coreId)
{
  if (coreId < 4)
  {
    m_IsPlaying[coreId] = false;
    asm volatile("dmb sy" ::
                     : "memory");

    if (!IsAnyPlaying())
    {
      // CLogger::Get()->Write(FromAtom, LogError, "Free");
      Free();
    }
  }
}

void CWelcomeAnimation::UpdateAnimation()
{
  m_SubFrameCounter++;
  if (m_SubFrameCounter >= 2)
  {
    m_SubFrameCounter = 0;

    for (int i = 0; i < 4; i++)
    {
      if (!g_WelcomeAnim.IsPlaying(i))
        //(!m_IsPlaying[i])
        continue;

      m_FrameDelay[i]++;
      if (m_FrameDelay[i] > i)
      {
        m_FrameDelay[i] = 0;
        if (i == 0 || i == 3)
        {
          m_CurrentFrame[i]++;
          if (m_CurrentFrame[i] >= m_TotalFrames)
            m_CurrentFrame[i] = 0;
        }
        else
        {
          m_CurrentFrame[i]--;
          if (m_CurrentFrame[i] < 0)
            m_CurrentFrame[i] = m_TotalFrames - 1;
        }
      }
    }
  }
}

void CWelcomeAnimation::DrawCurrentFrameCentered(u32 *pFB, u32 pitch, u32 startX, u32 startY, unsigned coreId)
{
  if (!m_pFrames || coreId >= 4)
  {
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

  for (int y = 0; y < FRAME_H; y++)
  {
    u32 *pDstRow = pFB + ((startY + offsetY + y) * pitch) + startX + offsetX;
    const u32 *pSrcRow = &pSrcFrame[y * FRAME_W];

    for (int x = 0; x < FRAME_W; x++)
    {
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
      m_bShutdown(FALSE)
{
  s_pThis = this;
  for (int i = 0; i < 4; i++)
  {
    m_LocalQuadrantBuffer[i] = nullptr;
  }
}

CAtomRunner::~CAtomRunner(void)
{
  for (int i = 0; i < 4; i++)
  {
    delete[] m_LocalQuadrantBuffer[i];
  }
  delete m_pConsole;
  s_pThis = nullptr;
}

boolean CAtomRunner::Initialize(void)
{
  boolean bOK = TRUE;

  for (int i = 0; i < 4; i++)
  {
    m_LocalQuadrantBuffer[i] = new u32[512 * 384];
    if (!m_LocalQuadrantBuffer[i])
    {
      CLogger::Get()->Write(FromAtom, LogError, "Kan buffer voor core %d niet alloceren!", i);
      bOK = FALSE;
    }
    else
    {
      u32 *pDest = m_LocalQuadrantBuffer[i];
      for (unsigned j = 0; j < 512 * 384; ++j)
      {
        *pDest++ = 0xFF080808;
      }
    }
  }

  if (bOK)
  {
    m_pConsole = new CTextConsole(m_pFrameBuffer, 1040, 24, 0xFFFFFFFF, 0xFF080808);
    m_pConsole->WriteString("Quatom Multi-Core Monitor Ready.\r\n");
    m_pConsole->WriteString("-----------------------------------------\r\n");
  }

  if (bOK)
  {
    CLogger::Get()->Write(FromAtom, LogNotice, "Bezig met laden van AtomMP4.bin...");
    if (!g_WelcomeAnim.Initialize("SD:/AtomMP4.bin"))
    {
      CLogger::Get()->Write(FromAtom, LogError, "Kan AtomMP4.bin niet laden, we gaan door...");
    }
    else
    {
      CLogger::Get()->Write(FromAtom, LogNotice, "AtomMP4.bin succesvol geladen!");
    }
  }

  if (bOK)
  {
    bOK = CMultiCoreSupport::Initialize();
  }

  if (bOK)
  {
    m_pConsole->WriteString("Multicore started\r\n");
    for (int core = 0; core < 4; core++)
    {
      bOK = m_AtomEmulator[core].Initialize(core);
      if (bOK)
      {
        CLogger::Get()->Write(FromAtom, LogNotice, "--> Starten van AtomEmulator %d", core);
      }
    }
  }

  if (bOK)
  {
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
  if (rawLoad > 100)
    rawLoad = 100;
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

  // ------------------------------------------------------------
  // 1. Function keys (F1..F12)
  // ------------------------------------------------------------
  for (int i = 0; i < 6; i++)
  {
    uint8_t hid = RawKeys[i] & 0xFF;
    if (!hid)
      continue;

    if (hid == 0x42)
    {
      g_bTrueFullscreen = !g_bTrueFullscreen;
      CLogger::Get()->Write(FromAtom, LogNotice, "F9: True Fullscreen = %s", g_bTrueFullscreen ? "AAN" : "UIT");
      continue;
    }

    if (hid == 0x45)
    {
      s_pThis->m_AtomEmulator[activeCore].Reset();
      CLogger::Get()->Write(FromAtom, LogNotice, "HARDWARE RESET uitgevoerd voor Core %d via F12!", activeCore);
      continue;
    }

    // F1..F4 -> Quadrant mode & core focus
    if (hid >= 0x3A && hid <= 0x3D)
    {
      newCore = hid - 0x3A;
      if (g_bFullscreenMode)
      {
        g_bFullscreenMode = false;
        for (int c = 0; c < 4; c++)
          g_SkipCounter[c] = 5;
        if (s_pThis && (u32 *)(uintptr_t)s_pThis->m_pFrameBuffer->GetBuffer())
        {
          memset((void *)(uintptr_t)s_pThis->m_pFrameBuffer->GetBuffer(), 0, s_pThis->m_pFrameBuffer->GetSize());
        }
      }
      if (g_WelcomeAnim.IsPlaying(newCore))
      {
        g_WelcomeAnim.StopCore(newCore);
        CLogger::Get()->Write(FromAtom, LogError, "g_WelcomeAnim.StopCore(newCore) %d", newCore);
      }

      s_InitializedVRAM[newCore] = false; // Forceer redraw van Atom VRAM
      // DrawFocusBorders(*s_pThis->m_pFrameBuffer, newCore);
      continue;
    }

    // F5..F8 -> Fullscreen mode
    if (hid >= 0x3E && hid <= 0x41)
    {
      g_bFullscreenMode = true;
      g_FullscreenCore = (hid - 0x3E);
      newCore = g_FullscreenCore;
      g_WelcomeAnim.StopCore(newCore);
      s_InitializedVRAM[newCore] = false;
      continue;
    }

    // Normale toets -> stop direct de animatie op de actieve core
    if (hid < 0x3A || hid > 0x45)
    {
      if (g_WelcomeAnim.IsPlaying(activeCore))
      {
        g_WelcomeAnim.StopCore(activeCore);
        s_InitializedVRAM[activeCore] = false; // Forceer hertekenen van VRAM ipv animatie
      }
    }
  }

  // ------------------------------------------------------------
  // 2. Core switch uitvoeren
  // ------------------------------------------------------------
  if (newCore != activeCore)
  {
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

  for (int i = 0; i < 6; i++)
  {
    if (!RawKeys[i])
      continue;
    TAtomKeyResult r = ConvertHIDToAtomKeyCustom(RawKeys[i], ucModifiers);
    if (r.bForceShift)
      forceShift = true;
    if (r.bSuppressShift)
      suppressShift = true;
  }

  bool effectiveShift = (physShift || forceShift) && !suppressShift;
  emu.SetShiftState(effectiveShift);
  emu.SetCtrlState(physCtrl);
  emu.SetReptState(physAlt);

  // ------------------------------------------------------------
  // 4. KeyDown detectie
  // ------------------------------------------------------------
  for (int i = 0; i < 6; i++)
  {
    uint8_t hid = RawKeys[i] & 0xFF;
    if (!hid)
      continue;

    bool alreadyPressed = false;
    for (int j = 0; j < 6; j++)
    {
      if (s_lastHID[j] == hid)
        alreadyPressed = true;
    }

    if (!alreadyPressed)
    {
      TAtomKeyResult r = ConvertHIDToAtomKeyCustom(hid, ucModifiers);
      if (r.atomKey)
      {
        // Zorg dat animatie gegarandeerd uit staat
        if (g_WelcomeAnim.IsPlaying(activeCore))
        {
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
  for (int i = 0; i < 6; i++)
  {
    uint8_t oldHID = s_lastHID[i];
    if (!oldHID)
      continue;

    bool stillPressed = false;
    for (int j = 0; j < 6; j++)
    {
      if (RawKeys[j] == oldHID)
        stillPressed = true;
    }

    if (!stillPressed)
    {
      uint8_t atomKey = s_lastAtomKey[i];
      if (atomKey)
        emu.KeyUp(atomKey);
      s_lastAtomKey[i] = 0;
      s_lastHID[i] = 0;
    }
  }

  // ------------------------------------------------------------
  // 6. Reset bij alle toetsen los
  // ------------------------------------------------------------
  bool allReleased = true;
  for (int i = 0; i < 6; i++)
  {
    if (RawKeys[i] != 0)
      allReleased = false;
  }

  if (allReleased)
  {
    emu.ClearKeyboard();
    memset(s_lastHID, 0, sizeof(s_lastHID));
    memset(s_lastAtomKey, 0, sizeof(s_lastAtomKey));
  }

  // Direct renderen forceren
  // hier nog een keer checken of we in mode 0 zitten, want dat scheelt tijd denk ik
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