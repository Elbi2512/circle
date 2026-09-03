#ifndef _ATOM_H
#define _ATOM_H

#ifndef ARM_ALLOW_MULTI_CORE
#define ARM_ALLOW_MULTI_CORE
#endif

// ------------------------------------------------------------
// Circle & System Includes
// ------------------------------------------------------------
#include <circle/types.h>
#include <circle/multicore.h>
#include <circle/timer.h>
#include <circle/bcmframebuffer.h>
#include <circle/devicenameservice.h>
#include <circle/usb/usbkeyboard.h>
#include <fatfs/ff.h>

// ------------------------------------------------------------
// Project Includes
// ------------------------------------------------------------
#include "atomemulator.h"
#include "AtomVideo.h"
#include "textconsole.h"

// ============================================================
// CWelcomeAnimation
// Beheert de boot-animatie vanuit BMP/bin voor alle 4 cores
// ============================================================
class CWelcomeAnimation
{
public:
    CWelcomeAnimation();
    ~CWelcomeAnimation();

    bool Initialize(const char *pBinPath);
    void Free(void);

    bool IsPlaying(unsigned coreId) const;
    bool IsAnyPlaying(void) const;
    void StopCore(unsigned coreId);

    void UpdateAnimation(void);
    void DrawCurrentFrameCentered(u32 *pFB, u32 pitch, u32 startX, u32 startY, unsigned coreId);

private:
    u32 *volatile m_pFrames;
    int           m_TotalFrames;
    int           m_SubFrameCounter;
    int           m_CurrentFrame[4];
    int           m_FrameDelay[4];
    int           m_FrameDir[4];    // <-- Richting per core: +1 (vooruit) of -1 (achteruit)
    volatile bool m_IsPlaying[4];
};

// ============================================================
// CAtomRunner
// Multi-core manager en coördinator voor de emulatie
// ============================================================
class CAtomRunner : public CMultiCoreSupport
{
public:
    CAtomRunner(CBcmFrameBuffer *pFrameBuffer, CTimer *pTimer,
                CDeviceNameService *pDeviceNameService, CMemorySystem *pMemorySystem);
    ~CAtomRunner(void);

    // Lifecycle & Execution
    boolean Initialize(void);
    using CMultiCoreSupport::Run;
    virtual void Run(unsigned nCore) override;

    // Getters & Accessors
    static CAtomRunner *Get(void);
    CAtomEmulator      *GetEmulator(unsigned nCore);
    CTextConsole       *GetConsole(void);
    CBcmFrameBuffer    *GetFrameBuffer(void) const;

    // Redraw helpers
    void InvalidateQuadrantVRAM(unsigned coreId);

    // USB Keyboard callbacks & State
    static void KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    static void KeyboardRemovedHandler(CDevice *pDevice, void *pContext);
    static volatile unsigned m_nActiveCore;

private:
    void ProcessEmulatorFrame(unsigned coreIdx);

    // Hardware & Subsystemen
    CBcmFrameBuffer    *m_pFrameBuffer;
    CTimer             *m_pTimer;
    CDeviceNameService *m_pDeviceNameService;
    CTextConsole       *m_pConsole;
    CAtomEmulator       m_AtomEmulator[4];
    CUSBKeyboardDevice *volatile m_pKeyboard;

    // Multi-core status flags
    volatile boolean    m_bKeyboardReady;
    volatile boolean    m_bCoreInitDone;
    volatile boolean    m_bShutdown;

    static CAtomRunner *s_pThis;
};

#endif // _ATOM_H