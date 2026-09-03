#ifndef _kernel_h
#define _kernel_h
#ifndef ARM_ALLOW_MULTI_CORE
#define ARM_ALLOW_MULTI_CORE
#endif

#pragma once

#include <circle/types.h>
#include <circle/multicore.h>
#include <circle/sysconfig.h>
#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/bcmframebuffer.h>

#include <emmc.h>
#include <circle/fs/fat/fatfs.h>
#include <fatfs/ff.h>
// #include <circle/fs/fat/fatdir.h>
#include <circle/util.h>

#include "atomemulator.h"
#include "AtomVideo.h"
#include "textconsole.h"



enum TShutdownMode
{
    ShutdownNone,
    ShutdownReboot,
    ShutdownHalt
};

// extern volatile bool g_bTrueFullscreen;

class CKernel : public CMultiCoreSupport
{
public:
    CKernel(void);
    ~CKernel(void);
    CKernel *m_pKernel; // Bewaar een pointer naar de hoofdklasse
    CTextConsole *GetConsole(void) { return m_pConsole; }
    boolean Initialize(void);
    using CMultiCoreSupport::Run;
    TShutdownMode Run(void);

    static CKernel *Get(void) { return s_pThis; }
    CAtomEmulator *GetEmulator(unsigned nCore)
    {
        if (nCore < 4)
        {
            return &m_AtomEmulator[nCore];
        }
        return &m_AtomEmulator[0];
    }
    unsigned GetCurrentCore(void)
    {
        return CMultiCoreSupport::ThisCore(); // Geeft 0, 1, 2 of 3 terug
    }
    CFATFileSystem *GetFileSystem() { return &m_FileSystem; }
    static void KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    void ListDirectory(void);
    static volatile unsigned m_nActiveCore; // Of als non-static afhankelijk van je opzet
    virtual void Run(unsigned nCore) override;
    static void KeyboardRemovedHandler(CDevice *pDevice, void *pContext);

private:
    CActLED m_ActLED;
    CKernelOptions m_Options;
    CDeviceNameService m_DeviceNameService;
    CBcmFrameBuffer m_FrameBuffer;
    CSerialDevice m_Serial;
    // CInterruptSystem m_InterruptSystem;
    CTextConsole *m_pConsole;
    CInterruptSystem m_Interrupt;
    CTimer m_Timer;
    CLogger m_Logger;

    // CEMMCDevice        m_EMMC;
    // CPartitionManager  m_PartitionManager; // <-- Toevoegen tussen EMMC en FAT
    // CFATFileSystem     m_FileSystem;

    CEMMCDevice m_EMMC;
    CFATFileSystem m_FileSystem;
    CAtomEmulator m_AtomEmulator[4];
    u32 *m_LocalQuadrantBuffer[4];
    CUSBHCIDevice m_USBHCI;
    CUSBKeyboardDevice *volatile m_pKeyboard;
    volatile boolean m_bKeyboardReady; // <-- DEZE REGEL TOEVOEGEN

    volatile TShutdownMode m_ShutdownMode;
    static CKernel *s_pThis;

    boolean m_bUSBOK;
    volatile boolean m_bCoreInitDone; // Sync-vlag voor de overige cores
};

class CWelcomeAnimation
{
public:
    CWelcomeAnimation() : m_pFrames(nullptr), m_TotalFrames(210), m_SubFrameCounter(0)
    {
        for (int i = 0; i < 4; i++)
        {
            m_IsPlaying[i] = true;
            m_CurrentFrame[i] = 0;
            m_FrameDelay[i] = 0; // Wordt dynamisch per core ingesteld
        }
    }
    ~CWelcomeAnimation() { delete[] m_pFrames; }

    bool Initialize(const char *pBinPath)
    {
        FIL file;
        FRESULT res = f_open(&file, pBinPath, FA_READ | FA_OPEN_EXISTING);
        if (res != FR_OK)
        {
            return false;
        }

        // Bepaal de exacte bestandsgrootte in bytes
        UINT fileSize = f_size(&file);

        // Verwachte grootte voor 210 frames van 256x109 x 4 bytes = 23.506.560 bytes
        // (Of 240 frames als je er inmiddels 240 hebt: 240 * 111936 = 26.864.640 bytes)
        // UINT expectedSize = m_TotalFrames * (256 * 109 * sizeof(u32));

        if (fileSize == 0)
        {
            f_close(&file);
            return false;
        }

        // Reserveer geheugen
        m_pFrames = new u32[fileSize / sizeof(u32)];
        if (!m_pFrames)
        {
            f_close(&file);
            return false;
        }

        // Lees het bestand in delen of in zijn geheel in
        UINT bytesRead = 0;
        FRESULT readRes = f_read(&file, m_pFrames, fileSize, &bytesRead);

        // Sluit het bestand direct netjes af
        f_close(&file);

        // Controleer of alles daadwerkelijk is ingelezen
        if (readRes != FR_OK || bytesRead != fileSize)
        {
            delete[] m_pFrames;
            m_pFrames = nullptr;
            return false;
        }

        // Zorg voor een geheugenbarrière zodat alle CPU-cores direct 'zien'
        // dat de data volledig in RAM staat voordat ze mogen starten
        asm volatile("dsb sy" ::: "memory");

        return true;
    }

    bool IsPlaying(unsigned coreId) const
    {
        if (coreId < 4)
            return m_IsPlaying[coreId];
        return false;
    }

    void StopCore(unsigned coreId)
    {
        if (coreId < 4)
            m_IsPlaying[coreId] = false;
    }

    // Update de animatie met de gevraagde vertraging per core
    void UpdateAnimation()
    {
        m_SubFrameCounter++;
        if (m_SubFrameCounter >= 2) // Algemene tempo-regeling (om de 2 ticks)
        {
            m_SubFrameCounter = 0;

            for (int i = 0; i < 4; i++)
            {
                if (!m_IsPlaying[i])
                    continue;

                // Core i vertraagd per core-index (Core 0 = 0 extra vertraging, Core 1 = 1, Core 2 = 2, Core 3 = 3)
                m_FrameDelay[i]++;
                if (m_FrameDelay[i] > i)
                {
                    m_FrameDelay[i] = 0;

                    // Richting behouden: Core 0 en 3 vooruit (1 -> 210), Core 1 en 2 achteruit (210 -> 1)
                    if (i == 0 || i == 3)
                    {
                        m_CurrentFrame[i]++;
                        if (m_CurrentFrame[i] >= m_TotalFrames)
                        {
                            m_CurrentFrame[i] = 0;
                        }
                    }
                    else
                    {
                        m_CurrentFrame[i]--;
                        if (m_CurrentFrame[i] < 0)
                        {
                            m_CurrentFrame[i] = m_TotalFrames - 1;
                        }
                    }
                }
            }
        }
    }

    void DrawCurrentFrameCentered(u32 *pFB, u32 pitch, u32 startX, u32 startY, unsigned coreId)
    {
        if (!m_pFrames || coreId >= 4)
            return;

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

                if (alpha > 10)
                {
                    pDstRow[x] = pixel;
                }
                else
                {
                    pDstRow[x] = 0xFF000000; // Zwarte achtergrond
                }
            }
        }
    }

private:
    u32 *m_pFrames;
    int m_TotalFrames;
    int m_SubFrameCounter;
    int m_CurrentFrame[4];
    int m_FrameDelay[4];
    bool m_IsPlaying[4];
};
// Teken het frame 1-op-1 (niet vergroot) en exact in het midden van het window (512x384)
extern CWelcomeAnimation g_WelcomeAnim;
#endif