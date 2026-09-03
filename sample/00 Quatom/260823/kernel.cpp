
#include "kernel.h"
#include <circle/fs/fat/fatfs.h>
#include <circle/fs/fat/fatdir.h>
#include <circle/util.h>
#include <circle/memory.h>
#include <circle/logger.h>
//#include <SDHCI.h>
#define DBG(fmt, ...) \
  CLogger::Get()->Write("DBG", LogDebug, fmt, ##__VA_ARGS__)

static inline u32 *GetFrameBufferPtr(CBcmFrameBuffer &fb) {
  return (u32 *)(uintptr_t)fb.GetBuffer();
}

static const char FromKernel[] = "kernel";

static volatile bool g_bFullscreenMode = false;
static volatile unsigned g_FullscreenCore = 0;

static unsigned g_SkipCounter[4] = { 0, 0, 0, 0 };

volatile unsigned CKernel::m_nActiveCore = 0;
static const u64 FRAME_TIME_US = 20000;
static const u64 MAX_CATCHUP_US = FRAME_TIME_US * 5;
static volatile unsigned g_nCoreLoad[4] = { 0, 0, 0, 0 };  // 0 - 100%
// CSDHCI sdio;

CKernel *CKernel::s_pThis = nullptr;
CWelcomeAnimation g_WelcomeAnim;

void RenderCoreQuadrant(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM);
//void DrawFocusBorders(CBcmFrameBuffer &frameBuffer, unsigned nActiveCore);
void RenderSingleCoreFullscreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM);
void BlitQuadrantToScreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u32 *pSourceBuffer);
void RenderCoreQuadrantBuffer(u32 *pDestBuffer, unsigned nCoreId, const u8 *pLocalVRAM);
// Per core een lokale buffer van 512x384 pixels (ARGB32)
// 512 * 384 * 4 bytes = 786.432 bytes per buffer (~786 KB per core)
// u32 m_LocalQuadrantBuffer[4][512 * 384];

CKernel::CKernel(void)
  : CMultiCoreSupport(CMemorySystem::Get()),
    m_DeviceNameService(),
    m_FrameBuffer(0, 0, 32),  // <-- Laat de resolutie dynamisch bepalen door de framebuffer driver
    m_Serial(&m_Interrupt),
    m_Timer(&m_Interrupt),
    m_Logger(m_Options.GetLogLevel(), &m_Timer),
    m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
    m_FileSystem(),  // leeglaten CFATFileSystem object, wordt later gemount
    m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
    m_pKeyboard(nullptr),
    m_ShutdownMode(ShutdownNone),
    m_bUSBOK(FALSE),
    m_bCoreInitDone(FALSE)

{
  s_pThis = this;
}

CKernel::~CKernel(void) {
  s_pThis = nullptr;
}

boolean CKernel::Initialize(void) {
  boolean bOK = TRUE;

  // 1. Hardware, Interrupts & Timers
  if (bOK)
    bOK = m_Interrupt.Initialize();
  if (bOK)
    bOK = m_Timer.Initialize();
  if (bOK)
    bOK = m_Serial.Initialize(115200);
  if (bOK)
    bOK = m_Logger.Initialize(&m_Serial);

  // 2. Beeldscherm & SD-kaart
  if (bOK)
    bOK = m_FrameBuffer.Initialize();
  if (bOK)
    bOK = m_EMMC.Initialize();
  //   if (bOK) {
  for (int i = 0; i < 4; i++) {
    m_LocalQuadrantBuffer[i] = new u32[512 * 384];
    if (!m_LocalQuadrantBuffer[i]) {
      m_Logger.Write(FromKernel, LogError, "Kan lokale buffer voor core %d niet alloceren!", i);
      //bOK = false;
      //continue;
    } else {
      u32 *pDest = m_LocalQuadrantBuffer[i];
      for (unsigned j = 0; j < 512 * 384; ++j) {
        *pDest++ = 0xFF080808;
      }
    }
  }
  //   }
  if (bOK) {
    // Positie rechts: bijvoorbeeld op x = 1040 (direct rechts van de Atom-vensters op x=1036)
    // en y = 24.
    m_pConsole = new CTextConsole(&m_FrameBuffer, 1040, 24, 0xFFFFFFFF, 0xFF080808);  // Groene tekst op donkere achtergrond
    m_pConsole->WriteString("Quatom Multi-Core Monitor Ready.\r\n");
    m_pConsole->WriteString("-----------------------------------------\r\n");
  }
  if (bOK) {
    static FATFS fatfs;
    FRESULT res = f_mount(&fatfs, "SD:", 1);
    bOK = (res == FR_OK);
  }
  if (bOK) {
    m_Logger.Write(FromKernel, LogNotice, "Bezig met laden van AtomMP4.bin...");
    if (!g_WelcomeAnim.Initialize("SD:/AtomMP4.bin")) {
      m_Logger.Write(FromKernel, LogError, "Kan AtomMP4.bin niet laden, maar we gaan door...");
    } else {
      m_Logger.Write(FromKernel, LogNotice, "AtomMP4.bin succesvol geladen!");
    }
  }
  // 3. USB Host Controller WEL OP TIJD INITIALISEREN (vóór emulators/cores)
  if (bOK) {
    bOK = m_USBHCI.Initialize();
  }

  // 4. Multi-Core ondersteuning starten
  if (bOK) {
    bOK = CMultiCoreSupport::Initialize();
  }
  if (bOK)
  m_pConsole->WriteString("Multicore started\r\n");

  // 5. Atom Emulator Init voor alle 4 de cores
  if (bOK) {
    for (int core = 0; core < 4; core++) {
      bOK = m_AtomEmulator[core].Initialize(core);
      if (bOK)
       m_Logger.Write(FromKernel, LogNotice, "--> Starten van AtomEmulator %d", core);
      // Koppel de hoofdklasse hier aan de emulator!
      m_AtomEmulator[core].SetKernel(this);
    }
  }

  // 6. Pas als ALLES klaar is, de slave-cores loslaten
  if (bOK) {
    // DrawFocusBorders(s_pThis->m_FrameBuffer, 0);
    m_bCoreInitDone = TRUE;
  }

  return bOK;
}

void DrawVUMeter(CBcmFrameBuffer &fb, int x, int y, int width, int height, unsigned pct, unsigned nCore) {
  if (!g_bTrueFullscreen) {
    // u32 *pBuffer = (u32 *)fb.GetBuffer();
    u32 *pBuffer = (u32 *)(uintptr_t)fb.GetBuffer();  // 64bit conversie aanpassing

    unsigned pitchWords = fb.GetPitch() / 4;

    if (pct > 100)
      pct = 100;

    int filledWidth = (width * (int)pct) / 100;
    // Kleur bepalen op basis van belasting
    u32 fillColor;

    if (pct < 60)
      fillColor = 0xFF00D000;  // Groen (BGRA)
    else if (pct < 85)
      fillColor = 0xFFFFA500;  // Oranje
    else
      fillColor = 0xFFFF2020;  // Rood

    u32 bgColor = 0xFF202020;      // Donkergrijs
    u32 borderColor = 0xFF808080;  // Lichtgrijs kader

    for (int dy = 0; dy < height; dy++) {
      int py = y + dy;
      for (int dx = 0; dx < width; dx++) {
        int px = x + dx;
        u32 color;
        // Buitenste rand (1 pixel)
        if (dx == 0 || dx == width - 1 || dy == 0 || dy == height - 1) {
          color = borderColor;
        }
        // Gevulde actieve balk
        else if (dx <= filledWidth) {
          color = fillColor;
        }
        // Lege achtergrond
        else {
          color = bgColor;
        }
        pBuffer[py * pitchWords + px] = color;
      }
    }
  }
}
/*
void CKernel::Run(unsigned nCore) {
  while (!m_bCoreInitDone) {
    asm volatile("dmb sy" ::: "memory");
  }
  m_Logger.Write(FromKernel, LogNotice, "--> Slave Core %d heeft de wachtlus verlaten!", nCore);

  unsigned coreIdx = nCore - 1;
  if (coreIdx < 3) {
    m_Logger.Write(FromKernel, LogNotice, "Core %d: Acorn Atom Emulator gestart...", nCore);

    u64 nNextFrameTime = m_Timer.GetClockTicks64();
    unsigned long frameCount = 0;

    while (m_ShutdownMode == ShutdownNone) {
      u64 nCurrentTime = m_Timer.GetClockTicks64();

      frameCount++;
      if (frameCount % 300000 == 0)  // Pas gelogd na 300k frames (ca. 1x per minuut)
      {
        DBG("Core %d stat: VRAMChanged=%d, Load=%u%%",
            coreIdx,
            m_AtomEmulator[coreIdx].HasVRAMChanged(),
            g_nCoreLoad[coreIdx]);
      }

      if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
        nNextFrameTime = nCurrentTime;

      const u64 tStart = m_Timer.GetClockTicks64();

      // 1. Emulatie‑update
      m_AtomEmulator[coreIdx].Update();

      // 2. VRAM‑status
      const bool bVRAM = m_AtomEmulator[coreIdx].HasVRAMChanged();
      bool bDoRender = false;

      // VRAM veranderd? Altijd renderen, anders pas na 10 frames (was 5) om CPU te sparen
      if (bVRAM) {
        bDoRender = true;
      } else {
        if (++g_SkipCounter[coreIdx] >= 5)
          bDoRender = true;
      }

      if (bDoRender) {
        g_SkipCounter[coreIdx] = 0;

        // 1. Render naar de lokale buffer van deze core
        RenderCoreQuadrantBuffer(m_LocalQuadrantBuffer[coreIdx], coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());

        // 2. Kopieer de lokale buffer naar de HDMI framebuffer (alleen als we niet in fullscreen zijn)
        if (!g_bFullscreenMode) {
          BlitQuadrantToScreen(m_FrameBuffer, coreIdx, m_LocalQuadrantBuffer[coreIdx]);
        } else if (g_bFullscreenMode && g_FullscreenCore == coreIdx) {
          // Voor fullscreen kun je desgewenst direct vanuit de lokale buffer schalen naar 4x
          RenderSingleCoreFullscreen(m_FrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
        }

        m_AtomEmulator[coreIdx].ResetVRAMChanged();

        // Console altijd NA de quadranten tekenen
       // if (s_pThis->m_pConsole && !g_bFullscreenMode) {
       //   s_pThis->m_pConsole->Refresh();
       // }
      }

      const u64 tWork = m_Timer.GetClockTicks64() - tStart;
      unsigned rawLoad = (unsigned)((tWork * 100) / FRAME_TIME_US);
      if (rawLoad > 100)
        rawLoad = 100;
      g_nCoreLoad[coreIdx] = (g_nCoreLoad[coreIdx] * 7 + rawLoad) / 8;

      nNextFrameTime += FRAME_TIME_US;
      while (m_Timer.GetClockTicks64() < nNextFrameTime) {
        asm volatile("wfe");
        //asm volatile("nop" ::: "memory");
      }
    }
  }
} */
/*
void CKernel::Run(unsigned nCore) {
  while (!m_bCoreInitDone) {
    asm volatile("dmb sy" ::
                   : "memory");
  }

  unsigned coreIdx = nCore - 1;
  if (coreIdx < 3) {
    m_Logger.Write(FromKernel, LogNotice, "Core %d: Acorn Atom Emulator gestart...", nCore);

    u64 nNextFrameTime = m_Timer.GetClockTicks64();
    unsigned long frameCount = 0;

    while (m_ShutdownMode == ShutdownNone) {
      u64 nCurrentTime = m_Timer.GetClockTicks64();

      frameCount++;
      if (frameCount % 300000 == 0)  // Pas gelogd na 300k frames (ca. 1x per minuut)
      {
        DBG("Core %d stat: VRAMChanged=%d, Load=%u%%",
            coreIdx,
            m_AtomEmulator[coreIdx].HasVRAMChanged(),
            g_nCoreLoad[coreIdx]);
      }

      if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
        nNextFrameTime = nCurrentTime;

      const u64 tStart = m_Timer.GetClockTicks64();

      // 1. Emulatie‑update
      m_AtomEmulator[coreIdx].Update();

      static u64 lastAnimTick = 0;
      u64 now = m_Timer.GetClockTicks64();
      if (g_WelcomeAnim.IsPlaying(coreIdx)) {
      if (now - lastAnimTick >= FRAME_TIME_US)  // dit deel voor de draaiende atom op openingsscherm
      {
        lastAnimTick = now;
        g_WelcomeAnim.UpdateAnimation();
      }
      // 2. Welkomstanimatie check per core (zodat cores 1, 2 en 3 ook hun animatie-frames tekenen)
      // if (g_WelcomeAnim.IsPlaying(coreIdx)) {
        // Welkomstanimatie vlag afdwingen zodat de render-stap triggert
       }

      // 3. VRAM‑status
      const bool bVRAM = m_AtomEmulator[coreIdx].HasVRAMChanged();
      bool bDoRender = false;

      // Als de animatie draait OF VRAM is veranderd OF skip-teller is bereikt -> renderen
      if (g_WelcomeAnim.IsPlaying(coreIdx) || bVRAM) {
        bDoRender = true;
      } else {
        if (++g_SkipCounter[coreIdx] >= 5)
          bDoRender = true;
      }

      if (bDoRender) {
        g_SkipCounter[coreIdx] = 0;

        // 1. Render naar de lokale buffer van deze core
        RenderCoreQuadrantBuffer(m_LocalQuadrantBuffer[coreIdx], coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());

        // 2. Kopieer de lokale buffer naar de HDMI framebuffer (alleen als we niet in fullscreen zijn)
        if (!g_bFullscreenMode) {
          BlitQuadrantToScreen(m_FrameBuffer, coreIdx, m_LocalQuadrantBuffer[coreIdx]);
        } else if (g_bFullscreenMode && g_FullscreenCore == coreIdx) {
          RenderSingleCoreFullscreen(m_FrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
        }

        m_AtomEmulator[coreIdx].ResetVRAMChanged();
      }

      const u64 tWork = m_Timer.GetClockTicks64() - tStart;
      unsigned rawLoad = (unsigned)((tWork * 100) / FRAME_TIME_US);
      if (rawLoad > 100)
        rawLoad = 100;
      g_nCoreLoad[coreIdx] = (g_nCoreLoad[coreIdx] * 7 + rawLoad) / 8;

      nNextFrameTime += FRAME_TIME_US;
      while (m_Timer.GetClockTicks64() < nNextFrameTime) {
        asm volatile("wfe");
      }
    }
  }
}

*/
void CKernel::Run(unsigned nCore)
{
  while (!m_bCoreInitDone)
  {
    asm volatile("dmb sy" ::
                     : "memory");
  }

  unsigned coreIdx = nCore - 1;
  if (coreIdx < 3)
  {
    m_Logger.Write(FromKernel, LogNotice, "Core %d: Acorn Atom Emulator gestart...", nCore);

    u64 nNextFrameTime = m_Timer.GetClockTicks64();
    unsigned long frameCount = 0; 

    while (m_ShutdownMode == ShutdownNone)
    {
      u64 nCurrentTime = m_Timer.GetClockTicks64();

      frameCount++;
      if (frameCount % 300000 == 0) // Pas gelogd na 300k frames (ca. 1x per minuut)
      {
        DBG("Core %d stat: VRAMChanged=%d, Load=%u%%",
            coreIdx,
            m_AtomEmulator[coreIdx].HasVRAMChanged(),
            g_nCoreLoad[coreIdx]);
      }

      if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US)
        nNextFrameTime = nCurrentTime;

      const u64 tStart = m_Timer.GetClockTicks64();

      // 1. Emulatie‑update
      m_AtomEmulator[coreIdx].Update();

      // 2. VRAM‑status
      const bool bVRAM = m_AtomEmulator[coreIdx].HasVRAMChanged();
      bool bDoRender = false;

      // VRAM veranderd? Altijd renderen, anders pas na 10 frames (was 5) om CPU te sparen
      if (bVRAM)
      {
        bDoRender = true;
      }
      else
      {
        if (++g_SkipCounter[coreIdx] >= 5)
          bDoRender = true;
      }

      if (bDoRender)
      {
        g_SkipCounter[coreIdx] = 0;

        if (g_bFullscreenMode)
        {
          if (g_FullscreenCore == coreIdx)
            RenderSingleCoreFullscreen(m_FrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
        }
        else
        {
          // RenderCoreQuadrant
          RenderCoreQuadrantBuffer(m_LocalQuadrantBuffer[coreIdx], coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
           // m_FrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
        }

        m_AtomEmulator[coreIdx].ResetVRAMChanged();
      }

      const u64 tWork = m_Timer.GetClockTicks64() - tStart;
      unsigned rawLoad = (unsigned)((tWork * 100) / FRAME_TIME_US);
      if (rawLoad > 100)
        rawLoad = 100;
      g_nCoreLoad[coreIdx] = (g_nCoreLoad[coreIdx] * 7 + rawLoad) / 8;

      nNextFrameTime += FRAME_TIME_US;
      while (m_Timer.GetClockTicks64() < nNextFrameTime)
      {
        asm volatile("wfe");
      }
    }
  }
}

TShutdownMode CKernel::Run(void) {
  m_Logger.Write(FromKernel, LogNotice, "Core 0 Run-loop gestart (USB, I/O Master & Emulator 3)...");

  u64 nNextFrameTime = m_Timer.GetClockTicks64();
  // unsigned long frameCountCore0 = 0;

  while (m_ShutdownMode == ShutdownNone) {
    // 1. USB keyboard detectie
    if (m_pKeyboard == nullptr) {
      CDevice *pDev = m_DeviceNameService.GetDevice("ukbd1", FALSE);
      if (pDev != nullptr) {
        m_pKeyboard = (CUSBKeyboardDevice *)pDev;
        m_pKeyboard->RegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw);
        m_pKeyboard->RegisterRemovedHandler(KeyboardRemovedHandler);
        m_Logger.Write(FromKernel, LogNotice, "SUCCESS: USB Toetsenbord gekoppeld op Core 0!");
        m_bKeyboardReady = TRUE;
      }
    }

    if (m_pKeyboard != nullptr) {
      m_pKeyboard->UpdateLEDs();
    }

    u64 nCurrentTime = m_Timer.GetClockTicks64();

    // frameCountCore0++;
    // if (frameCountCore0 % 300000 == 0) // Pas gelogd na 300k frames
    // {
    //   DBG("Core 0 stat: Actieve Core = %u, KeyboardReady = %d",
    //       m_nActiveCore, m_bKeyboardReady);
    // }

    if (nCurrentTime >= nNextFrameTime) {
      if (nCurrentTime > nNextFrameTime + MAX_CATCHUP_US) {
        nNextFrameTime = nCurrentTime;
      }
      const u64 tStart = m_Timer.GetClockTicks64();

      unsigned coreIdx = 3;  // core 0 index

      // Emulator 3 (core 0 index 3)
      m_AtomEmulator[coreIdx].Update();

      static u64 lastAnimTick = 0;
      u64 now = m_Timer.GetClockTicks64();

      if (now - lastAnimTick >= FRAME_TIME_US)  // dit deel voor de draaiende atom op openingsscherm
      {
        lastAnimTick = now;
        g_WelcomeAnim.UpdateAnimation();
      }

      const bool bVRAM = m_AtomEmulator[coreIdx].HasVRAMChanged();
      bool bDoRender = false;

      if (bVRAM) {
        bDoRender = true;
      } else {
        if (++g_SkipCounter[coreIdx] >= 5)  // Verhoogd naar 10 frames om belasting te drukken
          bDoRender = true;
      }

      if (bDoRender) {
        g_SkipCounter[coreIdx] = 0;

        // 1. Render naar de lokale buffer van deze core
        RenderCoreQuadrantBuffer(m_LocalQuadrantBuffer[coreIdx], coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());

        // 2. Kopieer de lokale buffer naar de HDMI framebuffer (alleen als we niet in fullscreen zijn)
        if (!g_bFullscreenMode) {
          BlitQuadrantToScreen(m_FrameBuffer, coreIdx, m_LocalQuadrantBuffer[coreIdx]);
        } else if (g_bFullscreenMode && g_FullscreenCore == coreIdx) {
          // Voor fullscreen kun je desgewenst direct vanuit de lokale buffer schalen naar 4x
          RenderSingleCoreFullscreen(m_FrameBuffer, coreIdx, m_AtomEmulator[coreIdx].GetVRAMPointer());
        }

        m_AtomEmulator[coreIdx].ResetVRAMChanged();
        // Console NA de quadranten tekenen
        //if (m_pConsole && !g_bFullscreenMode) {
        //  m_pConsole->Refresh();
        // }
      }

      const u64 tWork = m_Timer.GetClockTicks64() - tStart;
      unsigned rawLoad = (unsigned)((tWork * 100) / FRAME_TIME_US);
      if (rawLoad > 100)
        rawLoad = 100;
      g_nCoreLoad[coreIdx] = (g_nCoreLoad[coreIdx] * 7 + rawLoad) / 8;

      // VU‑meters elke 250 ms
      static u64 nLastVUTime = 0;
      const u64 nCurrTicks = m_Timer.GetClockTicks64();
      if (nCurrTicks - nLastVUTime >= 250000) {
        nLastVUTime = nCurrTicks;
        const u32 fbHeight = m_FrameBuffer.GetHeight();
        for (unsigned c = 0; c < 4; c++) {
          const int barX = (2 * 512) + 10;
          const int barY = fbHeight - 10 - ((4 - c) * 15);
          DrawVUMeter(m_FrameBuffer, barX, barY, 325, 10, g_nCoreLoad[c], s_pThis->m_nActiveCore);
        }
      }

      nNextFrameTime += FRAME_TIME_US;
    }

    asm volatile("nop");
  }

  return m_ShutdownMode;
}

struct TAtomKeyResult {
  uint8_t atomKey;
  bool bForceShift;
  bool bSuppressShift;
};

static TAtomKeyResult ConvertHIDToAtomKeyCustom(uint8_t hidCode, uint8_t ucModifiers) {
  TAtomKeyResult res = { 0, false, false };
  bool bShift = (ucModifiers & 0x22) != 0;  // L-Shift of R-Shift

  // 1. Letters 'A' t/m 'Z' (HID 0x04 t/m 0x1D)
  if (hidCode >= 0x04 && hidCode <= 0x1D) {
    res.atomKey = 'A' + (hidCode - 0x04);
    return res;
  }

  // 2. Cursor Pijltjestoetsen
  switch (hidCode) {
    case 0x4F:
      res.atomKey = 0x02;
      return res;  // Pijl RECHTS
    case 0x50:
      res.atomKey = 0x02;
      res.bForceShift = true;
      return res;  // Pijl LINKS  (Right + Shift)
    case 0x51:
      res.atomKey = 0x01;
      res.bForceShift = true;
      return res;  // Pijl OMLAAG (Up + Shift)
    case 0x52:
      res.atomKey = 0x01;
      return res;  // Pijl OMHOOG
  }

  // 3. Cijferrij '1' t/m '0' (HID 0x1E t/m 0x27)
  if (hidCode >= 0x1E && hidCode <= 0x27) {
    if (!bShift) {
      res.atomKey = (hidCode == 0x27) ? '0' : ('1' + (hidCode - 0x1E));
      return res;
    } else {
      switch (hidCode) {
        case 0x1F:  // Shift + 2 -> Atom '@'
          res.atomKey = '@';
          res.bSuppressShift = true;
          return res;

          //     case 0x23:              // Shift + 6 -> Atom '^'
          //         res.atomKey = 0x03; // ^ toets
          //         res.bSuppressShift = true;
          //         return res;
          //
          //     case 0x24: // Shift + 7 -> Atom '&'
          //         res.atomKey = '7';
          //         res.bSuppressShift = true;
          //         return res;

        case 0x25:                 // Shift + 8 -> Atom '*' (Op de Atom matrix is '*' gekoppeld aan Shift + ':')
          res.atomKey = ';';       // Basistoets ';' (Rij 2, Kolom 2)
          res.bForceShift = true;  // Met Shift geeft dit '*' op de Atom
          return res;

        case 0x26:            // Shift + 9 -> Atom '('
          res.atomKey = '8';  // Atom '(' zit op de matrix bij '8' met Shift
          res.bForceShift = true;
          return res;

        case 0x27:            // Shift + 0 -> Atom ')'
          res.atomKey = '9';  // Atom ')' zit op de matrix bij '9' met Shift
          res.bForceShift = true;
          return res;

        case 0x39:  // USB CAPS LOCK -> Acorn Atom Caps Lock / Matrix teken 0x03
          res.atomKey = 0x03;
          return res;

        default:  // Overige shift cijfers (1=!, 3=#, 4=$, 5=%)
          res.atomKey = (hidCode == 0x27) ? '0' : ('1' + (hidCode - 0x1E));
          return res;
      }
    }
  }

  // 4. Speciale symbolen en besturingstoetsen
  switch (hidCode) {
    case 0x2B:  // USB TAB -> Acorn Atom COPY toets
      res.atomKey = 0x09;
      return res;

    case 0x2E:  // USB '=' toets
      if (!bShift) {
        res.atomKey = '-';
        res.bForceShift = true;  // Shift + '-' = '='
      } else {
        res.atomKey = ';';
        res.bForceShift = true;  // Shift + ';' = '+'
      }
      return res;

    case 0x31:  // USB Backslash (ISO / US layout)
    case 0x32:  // USB Backslash (Alternatieve scancode / EU layout)
      res.atomKey = '\\';
      return res;

    case 0x34:  // USB Single Quote '\''
      res.atomKey = '\'';
      res.bForceShift = true;  // Shift + ''' = '"'
      return res;

    case 0x28:
      res.atomKey = '\r';
      return res;  // RETURN / ENTER
    case 0x29:
      res.atomKey = 0x1B;
      return res;  // ESCAPE
    case 0x2A:
      res.atomKey = 0x08;
      return res;  // BACKSPACE / DELETE
    case 0x2C:
      res.atomKey = ' ';
      return res;  // SPATIE
    case 0x2D:
      res.atomKey = '-';
      return res;
    case 0x2F:
      res.atomKey = '[';
      return res;
    case 0x30:
      res.atomKey = ']';
      return res;
    case 0x33:
      res.atomKey = ';';
      return res;
    case 0x36:
      res.atomKey = ',';
      return res;
    case 0x37:
      res.atomKey = '.';
      return res;
    case 0x38:
      res.atomKey = '/';
      return res;
    default:
      break;
  }

  return res;
}

void CKernel::KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]) {
  if (!s_pThis)
    return;

  // HID → Atom mapping per slot (0..5)
  static uint8_t s_lastHID[6] = { 0 };
  static uint8_t s_lastAtomKey[6] = { 0 };

  unsigned activeCore = s_pThis->m_nActiveCore;
  unsigned newCore = activeCore;

  // ------------------------------------------------------------
  // 1. Function keys (F1..F12) – core switching, fullscreen, reset
  // ------------------------------------------------------------
  for (int i = 0; i < 6; i++) {
    uint8_t hid = RawKeys[i] & 0xFF;

    if (!hid)
      continue;

    // F9 → toggle true fullscreen
    if (hid == 0x42) {
      g_bTrueFullscreen = !g_bTrueFullscreen;
      CLogger::Get()->Write("kernel", LogNotice,
                            "F9: True Fullscreen = %s", g_bTrueFullscreen ? "AAN" : "UIT");
      continue;
    }

    // F12 → hard reset active core
    if (hid == 0x45) {
      s_pThis->m_AtomEmulator[activeCore].Reset();
      CLogger::Get()->Write("kernel", LogNotice,
                            "HARDWARE RESET uitgevoerd voor Core %d via F12!", activeCore);
      continue;
    }

    // F1..F4 → quadrant mode + focus switch
    if (hid >= 0x3A && hid <= 0x3D) {
      newCore = hid - 0x3A;

      // --> Voeg dit toe:
      if (g_bFullscreenMode) {
        g_bFullscreenMode = false;

        // Forceer een volledige redraw voor alle cores
        for (int i = 0; i < 4; i++) {
          g_SkipCounter[i] = 5;
        }

        // Wis het volledige scherm naar zwart om 'rommel' te verwijderen
        if (s_pThis && (u32 *)(uintptr_t)s_pThis->m_FrameBuffer.GetBuffer()) {
          memset((void *)(uintptr_t)s_pThis->m_FrameBuffer.GetBuffer(), 0, s_pThis->m_FrameBuffer.GetSize());  // 64 bit
          // memset((void *)s_pThis->m_FrameBuffer.GetBuffer(), 0, s_pThis->m_FrameBuffer.GetSize());
        }
      }
      // <-- einde toevoeging

      g_WelcomeAnim.StopCore(newCore);
      //DrawFocusBorders(s_pThis->m_FrameBuffer, newCore);
      continue;
    }

    // F5..F8 → fullscreen mode
    if (hid >= 0x3E && hid <= 0x41) {
      g_bFullscreenMode = true;
      g_FullscreenCore = (hid - 0x3E);
      newCore = g_FullscreenCore;
      g_WelcomeAnim.StopCore(newCore);
      continue;
    }

    // Normale type‑toets → stop animatie voor actieve core
    if (hid < 0x3A || hid > 0x45) {
      g_WelcomeAnim.StopCore(activeCore);
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

    if (!g_bFullscreenMode)
      //DrawFocusBorders(s_pThis->m_FrameBuffer, newCore);

    activeCore = newCore;
  }

  CAtomEmulator &emu = s_pThis->m_AtomEmulator[activeCore];

  // ------------------------------------------------------------
  // 3. Modifier states (Shift, Ctrl, Repeat)
  // ------------------------------------------------------------
  bool physShift = (ucModifiers & 0x22) != 0;
  bool physCtrl = (ucModifiers & 0x11) != 0;
  bool physAlt = (ucModifiers & 0x44) != 0;

  bool forceShift = false;
  bool suppressShift = false;

  for (int i = 0; i < 6; i++) {
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
  for (int i = 0; i < 6; i++) {
    uint8_t hid = RawKeys[i] & 0xFF;

    if (!hid)
      continue;

    bool alreadyPressed = false;
    for (int j = 0; j < 6; j++)
      if (s_lastHID[j] == hid)
        alreadyPressed = true;

    if (!alreadyPressed) {
      TAtomKeyResult r = ConvertHIDToAtomKeyCustom(hid, ucModifiers);
      if (r.atomKey) {
        emu.KeyDown(r.atomKey);
        s_lastAtomKey[i] = r.atomKey;  // BEWAAR Atom‑key
        s_lastHID[i] = hid;            // BEWAAR HID‑key
      }
    }
  }

  // ------------------------------------------------------------
  // 5. KeyUp detectie (NOOIT opnieuw HID → Atom converteren!)
  // ------------------------------------------------------------
  for (int i = 0; i < 6; i++) {
    uint8_t oldHID = s_lastHID[i];
    if (!oldHID)
      continue;

    bool stillPressed = false;
    for (int j = 0; j < 6; j++)
      if (RawKeys[j] == oldHID)
        stillPressed = true;

    if (!stillPressed) {
      uint8_t atomKey = s_lastAtomKey[i];
      if (atomKey)
        emu.KeyUp(atomKey);

      s_lastAtomKey[i] = 0;
      s_lastHID[i] = 0;
    }
  }

  // ------------------------------------------------------------
  // 6. Alles los? → matrix volledig resetten
  // ------------------------------------------------------------
  bool allReleased = true;
  for (int i = 0; i < 6; i++)
    if (RawKeys[i] != 0)
      allReleased = false;

  if (allReleased) {
    emu.ClearKeyboard();
    memset(s_lastHID, 0, sizeof(s_lastHID));
    memset(s_lastAtomKey, 0, sizeof(s_lastAtomKey));
  }
  // --- VOEG DIT TOE OM DIRECT SCHERMREFRESH TE FORCEERDER NA EEN TOETSAANSLAG ---
  g_SkipCounter[activeCore] = 5;  // Zet de skip-teller op max zodat de render-loop meteen triggert
  // ----------------------------------------------------------------------------
  // m_bVRAMChanged = true;
  emu.ForceVRAMChanged();
}

void CKernel::KeyboardRemovedHandler(CDevice *pDevice, void *pContext) {
  if (s_pThis != nullptr) {
    s_pThis->m_Logger.Write(FromKernel, LogNotice, "USB Toetsenbord ontkoppeld.");
    s_pThis->m_pKeyboard = nullptr;
    s_pThis->m_bKeyboardReady = FALSE;  // Vlag weer omlaag!
  }
}

void ScanDirectory(const char *pPath, int nDepth = 0) {
  if (nDepth > 3)
    return;  // Niet te diep zoeken

  DIR Directory;
  FILINFO FileInfo;

  FRESULT Result = f_findfirst(&Directory, &FileInfo, pPath, "*");

  while (Result == FR_OK && FileInfo.fname[0]) {
    if (!(FileInfo.fattrib & (AM_HID | AM_SYS))) {
      char indent[32] = { 0 };
      for (int i = 0; i < nDepth; i++) {
        strcat(indent, "  ");
      }

      bool isDir = (FileInfo.fattrib & AM_DIR) != 0;

      if (isDir) {
        if (strcmp(FileInfo.fname, ".") != 0 && strcmp(FileInfo.fname, "..") != 0) {
          CLogger::Get()->Write("FatFs", LogNotice, "%s[DIR]  /%s", indent, FileInfo.fname);

          char subPath[256];
          strcpy(subPath, pPath);
          strcat(subPath, "/");
          strcat(subPath, FileInfo.fname);

          ScanDirectory(subPath, nDepth + 1);
        }
      } else {
        // Alleen ROM- en ATM-bestanden tonen om log-saturation te voorkomen
        bool isRelevant = (strstr(FileInfo.fname, ".ROM") != nullptr) || (strstr(FileInfo.fname, ".rom") != nullptr) || (strstr(FileInfo.fname, ".ATM") != nullptr) || (strstr(FileInfo.fname, ".atm") != nullptr);

        if (isRelevant) {
          CLogger::Get()->Write("FatFs", LogNotice, "%s[FILE] %-24s (%u bytes)",
                                indent, FileInfo.fname, (unsigned)FileInfo.fsize);
        }
      }
    }

    Result = f_findnext(&Directory, &FileInfo);
  }

  f_closedir(&Directory);
}

void CKernel::ListDirectory(void) {
  m_Logger.Write(FromKernel, LogNotice, "--- Inhoud van SD-kaart via FatFs ---");
  ScanDirectory("SD:");
}

// Palet op 16-byte grens uitgelijnd
static const u32 Palette8[9] __attribute__((aligned(16))) = {
  0xFF000000,  // 0: Zwart
  0xFF00FF00,  // 1: Groen
  0xFFFFFF00,  // 2: Geel
  0xFF0000FF,  // 3: Blauw
  0xFFFF0000,  // 4: Rood
  0xFFFFFFFF,  // 5: Wit
  0xFF00FFFF,  // 6: Cyaan
  0xFFFF00FF,  // 7: Magenta
  0xFF101010   // 8: Iets minder zwart
};
static const u32 Palette9[9] __attribute__((aligned(16))) = {
  0xFF101010,  // 8: Iets minder zwart
  0xFF00F700,  // 1: Groen
  0xFFFFF700,  // 2: Geel
  0xFF0000FF,  // 3: Blauw
  0xFFFF0000,  // 4: Rood
  0xFFFEFEFE,  // 5: Wit
  0xFF00F7F7,  // 6: Cyaan
  0xFFF700F7,  // 7: Magenta
  0xFF101010   // 8: Iets minder zwart
};

// Globaal (bovenaan bij de palettes plaatsen)
static u8 s_LastVRAM[4][256 * 192] = { 0 };
static bool s_InitializedVRAM[4] = { false };

void RenderCoreQuadrantBuffer(u32 *pDestBuffer, unsigned nCoreId, const u8 *pLocalVRAM) {
  if (!pLocalVRAM || !pDestBuffer)
    return;

  constexpr int SRC_W = 256;
  constexpr int SRC_H = 192;
  constexpr int DST_W = 512;  // 2× horizontale scaling

  // 1. Welkomstanimatie tekenen (ALTIJD volledig doorsturen, bypass line tracking!)
  if (g_WelcomeAnim.IsPlaying(nCoreId)) {
    g_WelcomeAnim.DrawCurrentFrameCentered(pDestBuffer, DST_W, 0, 0, nCoreId);
    
    // Reset ook direct de cache zodat de normale Atom-emulatie straks fris start
    s_InitializedVRAM[nCoreId] = false; 
    
    CKernel::Get()->GetEmulator(nCoreId)->ResetVRAMChanged();
    return;
  }

  u8 *pLastVRAM = s_LastVRAM[nCoreId];

  // Eerste keer: volledige redraw en cache vullen
  bool forceFullRedraw = !s_InitializedVRAM[nCoreId];
  if (forceFullRedraw) {
    memcpy(pLastVRAM, pLocalVRAM, SRC_W * SRC_H);
    s_InitializedVRAM[nCoreId] = true;
  }

  // Loop door alle 192 scanlines voor normale Atom emulatie
  for (int y = 0; y < SRC_H; y++) {
    const u8 *pSrcRow = &pLocalVRAM[y * SRC_W];
    u8 *pLastRow = &pLastVRAM[y * SRC_W];

    // Vergelijk de lijn met de vorige staat
    bool lineChanged = forceFullRedraw || (memcmp(pSrcRow, pLastRow, SRC_W) != 0);

    if (!lineChanged)
      continue;

    // Update onze referentie voor deze lijn
    memcpy(pLastRow, pSrcRow, SRC_W);

    u32 *pDstRow1 = pDestBuffer + ((2 * y + 0) * DST_W);
    u32 *pDstRow2 = pDestBuffer + ((2 * y + 1) * DST_W);

    for (int x = 0; x < SRC_W; x++) {
      u32 P = CAtomVideo::GetColorARGB(pSrcRow[x]);

      // Donkere achtergrond iets lichter maken voor cores 1 en 2
      if (nCoreId == 1 || nCoreId == 2) {
        if (P == Palette8[0])
          P = Palette8[8];
      }

      // Hard 2× scaling: 1 bronpixel → 2×2 blok
      pDstRow1[2 * x + 0] = P;
      pDstRow1[2 * x + 1] = P;
      pDstRow2[2 * x + 0] = P;
      pDstRow2[2 * x + 1] = P;
    }
  }
}

// 64‑bit veilige quadrant‑blit naar HDMI‑framebuffer
void BlitQuadrantToScreen(CBcmFrameBuffer &frameBuffer,
                          unsigned nCoreId,
                          const u32 *pSourceBuffer) {
  if (!pSourceBuffer)
    return;

  const u32 *pFB = GetFrameBufferPtr(frameBuffer);
  if (!pFB)
    return;

  const int fbW = (int)frameBuffer.GetWidth();
  const int fbH = (int)frameBuffer.GetHeight();
  const int pitch = (int)frameBuffer.GetPitch() / 4;  // in 32‑bit woorden

  constexpr int QW = 512;
  constexpr int QH = 384;

  // Vaste quadrant posities op basis van nCoreId (0=linksboven, 1=rechtsboven, 2=linksonder, 3=rechtsonder)
  int baseX = (nCoreId & 1) ? 512 : 0;
  int baseY = (nCoreId & 2) ? 384 : 0;

  // Veiligheidscheck tegen buiten bereik schrijven
  if (baseX + QW > fbW || baseY + QH > fbH)
    return;

  // Blit de 512x384 lokale buffer naar het juiste quadrant op de HDMI framebuffer
  for (int y = 0; y < QH; ++y) {
    const u32 *pSrcRow = pSourceBuffer + (y * QW);
    u32 *pDstRow = (u32 *)pFB + (baseY + y) * pitch + baseX;

    for (int x = 0; x < QW; ++x) {
      pDstRow[x] = pSrcRow[x];
    }
  }
}
/*
// 64‑bit veilige fullscreen‑render van één core
void RenderSingleCoreFullscreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM) {
  if (!pLocalVRAM || !frameBuffer.GetBuffer())
    return;

  constexpr int SRC_W = 256;
  constexpr int SRC_H = 192;

  const u32 fbWidth = frameBuffer.GetWidth();
  const u32 fbHeight = frameBuffer.GetHeight();
  const u32 pitchWords = frameBuffer.GetPitch() / 4;

  u32 *pFB = GetFrameBufferPtr(frameBuffer);

  // Eenvoudige 4x scaling (256x192 → ~1024x768)
  const int scale = 4;
  const int dstW = SRC_W * scale;
  const int dstH = SRC_H * scale;

  const int offsetX = (int)((fbWidth - dstW) / 2);
  const int offsetY = (int)((fbHeight - dstH) / 2);

  for (int y = 0; y < SRC_H; y++) {
    const u8 *pSrcRow = &pLocalVRAM[y * SRC_W];

    for (int x = 0; x < SRC_W; x++) {
      u32 P = CAtomVideo::GetColorARGB(pSrcRow[x]);

      int dstX0 = offsetX + x * scale;
      int dstY0 = offsetY + y * scale;

      for (int dy = 0; dy < scale; dy++) {
        int py = dstY0 + dy;
        if (py < 0 || py >= (int)fbHeight)
          continue;

        u32 *pDstRow = pFB + py * pitchWords;

        for (int dx = 0; dx < scale; dx++) {
          int px = dstX0 + dx;
          if (px < 0 || px >= (int)fbWidth)
            continue;

          pDstRow[px] = P;
        }
      }
    }
  }
}

*/

void RenderSingleCoreFullscreen(CBcmFrameBuffer &frameBuffer, unsigned nCoreId, const u8 *pLocalVRAM) {
  if (!pLocalVRAM || !frameBuffer.GetBuffer())
    return;

  constexpr int SRC_W = 256;
  constexpr int SRC_H = 192;

  const u32 fbWidth = frameBuffer.GetWidth();
  const u32 fbHeight = frameBuffer.GetHeight();
  const u32 pitchWords = frameBuffer.GetPitch() / 4;

  u32 *pFB = GetFrameBufferPtr(frameBuffer);

  // Schaling (bijv. 4x voor 1024x768)
  const int scale = 4;
  const int dstW = SRC_W * scale;
  const int dstH = SRC_H * scale;

  int startX = 0;
  int startY = 0;

  if (g_bTrueFullscreen) {
    // F9: Altijd netjes centreren op het scherm
    if (fbWidth > (unsigned)dstW)  startX = (fbWidth - dstW) / 2;
    if (fbHeight > (unsigned)dstH) startY = (fbHeight - dstH) / 2;

    // Wis de randen buiten het gecentreerde scherm naar zwart
    for (unsigned y = 0; y < fbHeight; y++) {
      u32 *pRow = pFB + y * pitchWords;
      for (unsigned x = 0; x < fbWidth; x++) {
        if (x < (unsigned)startX || x >= (unsigned)(startX + dstW) ||
            y < (unsigned)startY || y >= (unsigned)(startY + dstH)) {
          pRow[x] = 0xFF000000;
        }
      }
    }
  } else {
    // F5 t/m F8: Standaard links beginnen (geen centrering)
    startX = 0;
    startY = 0;
  }

  // Render het opgeschaalde beeld
  for (int y = 0; y < SRC_H; y++) {
    const u8 *pSrcRow = &pLocalVRAM[y * SRC_W];

    for (int x = 0; x < SRC_W; x++) {
      u32 P = CAtomVideo::GetColorARGB(pSrcRow[x]);

      if (nCoreId == 1 || nCoreId == 2) {
        if (P == Palette8[0])
          P = Palette8[8];
      }

      int dstX0 = startX + x * scale;
      int dstY0 = startY + y * scale;

      for (int dy = 0; dy < scale; dy++) {
        int py = dstY0 + dy;
        if (py < 0 || py >= (int)fbHeight)
          continue;

        u32 *pDstRow = pFB + py * pitchWords;

        for (int dx = 0; dx < scale; dx++) {
          int px = dstX0 + dx;
          if (px < 0 || px >= (int)fbWidth)
            continue;

          pDstRow[px] = P;
        }
      }
    }l
  }
}
/*
void DrawFocusBorders(CBcmFrameBuffer &frameBuffer, unsigned nActiveCore) {
  // 64‑bit veilige pointer naar framebuffer
  u32 *pFB = (u32 *)(uintptr_t)frameBuffer.GetBuffer();
  if (!pFB)
    return;

  const u32 fbWidth = frameBuffer.GetWidth();
  const u32 fbHeight = frameBuffer.GetHeight();
  const u32 pitchWords = frameBuffer.GetPitch() / 4;

  // Quadrant‑afmetingen (zoals je lokale buffers: 512x384)
  const int quadW = 512;
  const int quadH = 384;

  const u32 borderNormal = 0xFF404040;  // grijs
  const u32 borderActive = 0xFFFFFF00;  // geel

  for (unsigned core = 0; core < 4; ++core) {
    int baseX = (core % 2) * quadW;
    int baseY = (core / 2) * quadH;

    u32 color = (core == nActiveCore) ? borderActive : borderNormal;

    // Boven- en onderrand
    for (int x = 0; x < quadW && baseX + x < (int)fbWidth; ++x) {
      int tx = baseX + x;
      int ty = baseY;
      int by = baseY + quadH - 1;

      if (ty >= 0 && ty < (int)fbHeight)
        pFB[ty * pitchWords + tx] = color;
      if (by >= 0 && by < (int)fbHeight)
        pFB[by * pitchWords + tx] = color;
    }

    // Links- en rechterrand
    for (int y = 0; y < quadH && baseY + y < (int)fbHeight; ++y) {
      int ly = baseY + y;
      int lx = baseX;
      int rx = baseX + quadW - 1;

      if (lx >= 0 && lx < (int)fbWidth)
        pFB[ly * pitchWords + lx] = color;
      if (rx >= 0 && rx < (int)fbWidth)
        pFB[ly * pitchWords + rx] = color;
    }
  }
} */