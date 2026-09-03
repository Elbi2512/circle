#ifndef _atom_h
#define _atom_h

#ifndef ARM_ALLOW_MULTI_CORE
#define ARM_ALLOW_MULTI_CORE
#endif

#include <circle/types.h>
#include <circle/multicore.h>
#include <circle/timer.h>
#include <circle/bcmframebuffer.h>
#include <circle/devicenameservice.h>
#include <circle/usb/usbkeyboard.h>
#include <fatfs/ff.h>

#include "atomemulator.h"
#include "AtomVideo.h"
#include "textconsole.h"

class CWelcomeAnimation
{
public:
    CWelcomeAnimation();
    ~CWelcomeAnimation();

    bool Initialize(const char *pBinPath);
    bool IsPlaying(unsigned coreId) const;
    void StopCore(unsigned coreId);
    void UpdateAnimation();
    void DrawCurrentFrameCentered(u32 *pFB, u32 pitch, u32 startX, u32 startY, unsigned coreId);

    // Controleert of er nog minimaal 1 core animeert
    bool IsAnyPlaying(void) const
    {
        for (int i = 0; i < 4; i++) {
            if (m_IsPlaying[i]) return true;
        }
        return false;
    }

    // Nette cleanup van de ~23 MB aan framebuffer RAM
    void Free(void)
    {
        if (m_pFrames != nullptr)
        {
            // Zorg eerst dat alle vlaggen uit staan
            for (int i = 0; i < 4; i++) {
                m_IsPlaying[i] = false;
            }

            // Geheugenbarrière zodat alle cores 'zien' dat m_IsPlaying false is
            asm volatile("dmb sy" ::: "memory");

            delete[] m_pFrames;
            m_pFrames = nullptr;
        }
    }

private:
    u32 * volatile m_pFrames; // volatile toevoegen voor multi-core zichtbaarheid
    int m_TotalFrames;
    int m_SubFrameCounter;
    int m_CurrentFrame[4];
    int m_FrameDelay[4];
    volatile bool m_IsPlaying[4];
};

class CAtomRunner : public CMultiCoreSupport
{
public:
    CAtomRunner(CBcmFrameBuffer *pFrameBuffer, CTimer *pTimer,
                CDeviceNameService *pDeviceNameService, CMemorySystem *pMemorySystem);
    ~CAtomRunner(void);

    boolean Initialize(void);
    using CMultiCoreSupport::Run;
    virtual void Run(unsigned nCore) override;

    static CAtomRunner *Get(void) { return s_pThis; }
    CAtomEmulator *GetEmulator(unsigned nCore)
    {
        return (nCore < 4) ? &m_AtomEmulator[nCore] : &m_AtomEmulator[0];
    }
    CTextConsole *GetConsole(void) { return m_pConsole; }

    static void KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    static void KeyboardRemovedHandler(CDevice *pDevice, void *pContext);
    static volatile unsigned m_nActiveCore;

private:
    void ProcessEmulatorFrame(unsigned coreIdx); // <-- Gemeenschappelijke logica

    CBcmFrameBuffer    *m_pFrameBuffer;
    CTimer             *m_pTimer;
    CDeviceNameService *m_pDeviceNameService;
    CTextConsole       *m_pConsole;

    CAtomEmulator       m_AtomEmulator[4];
    u32                *m_LocalQuadrantBuffer[4];
    CUSBKeyboardDevice *volatile m_pKeyboard;
    volatile boolean    m_bKeyboardReady;
    volatile boolean    m_bCoreInitDone;
    volatile boolean    m_bShutdown;

    static CAtomRunner *s_pThis;
};

#endif