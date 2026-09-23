#include "kernel.h"
#include <circle/memory.h>
#include <circle/string.h>

#define DRIVE          "SD:"
#define FIRMWARE_PATH  DRIVE "/firmware/"
#define CONFIG_FILE    DRIVE "/wpa_supplicant.conf"

static const char FromKernel[] = "kernel";

CKernel *CKernel::s_pThis = nullptr;

CKernel::CKernel(void)
:   m_FrameBuffer(0, 0, 32),
    m_Serial(&m_Interrupt),
    m_Timer(&m_Interrupt),
    m_Logger(LogDebug, &m_Timer), // Logger geforceerd op LogDebug voor alle netwerk/WPA details
    m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
    m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
    m_WLAN(FIRMWARE_PATH),
    m_Net(0, 0, 0, 0, DEFAULT_HOSTNAME, NetDeviceTypeWLAN),
    m_WPASupplicant(CONFIG_FILE),
    m_AtomRunner(&m_FrameBuffer, &m_Timer, &m_DeviceNameService, CMemorySystem::Get())
{
    s_pThis = this;
    m_ActLED.Blink(5);
}

CKernel::~CKernel(void)
{
    s_pThis = nullptr;
}

boolean CKernel::Initialize(void)
{
    boolean bOK = TRUE;

    // 1. Hardware, Interrupts & Timers
    if (bOK) bOK = m_Interrupt.Initialize();
    if (bOK) bOK = m_Timer.Initialize();
    if (bOK) bOK = m_Serial.Initialize(115200);
    if (bOK) bOK = m_Logger.Initialize(&m_Serial);

    // 2. Beeldscherm & EMMC Storage
    if (bOK) bOK = m_FrameBuffer.Initialize();
    if (bOK) bOK = m_EMMC.Initialize();

    // 3. SD Kaart Mounten
    if (bOK)
    {
        static FATFS fatfs;
        FRESULT res = f_mount(&fatfs, DRIVE, 1);
        bOK = (res == FR_OK);
        if (!bOK)
        {
            m_Logger.Write(FromKernel, LogError, "Kan SD-kaart niet mounten op %s (FRESULT=%d)!", DRIVE, res);
        }
    }

    // 4. USB Host Controller
    if (bOK)
    {
        bOK = m_USBHCI.Initialize();
    }

    // 5. Wi-Fi Chip, WPA Supplicant & Netwerk
    if (bOK)
    {
        m_Logger.Write(FromKernel, LogNotice, "1/3 WLAN Initialize...");
        bOK = m_WLAN.Initialize();
    }

    if (bOK)
    {
        m_Logger.Write(FromKernel, LogNotice, "2/3 WPA Supplicant Initialize...");
        bOK = m_WPASupplicant.Initialize();
    }

    if (bOK)
    {
        m_Logger.Write(FromKernel, LogNotice, "3/3 NetSubSystem Initialize...");
        // Laat NetSubSystem initialiseren ongeacht link status op dit moment
        m_Net.Initialize();
    }

    // 6. Multi-Core Runner
    m_Logger.Write(FromKernel, LogNotice, "Initialiseren Atom Runner...");
    if (bOK)
    {
        bOK = m_AtomRunner.Initialize();
    }

    m_Logger.Write(FromKernel, LogNotice, "CKernel::Initialize() gereed (status=%d)", bOK);
    return bOK;
}

TShutdownMode CKernel::Run(void)
{
    m_Logger.Write(FromKernel, LogNotice, "--> CKernel::Run() bereikt! Start scheduler loop...");

    unsigned nRetries = 0;
    while (!m_Net.IsRunning())
    {
        m_Scheduler.MsSleep(100);
        nRetries++;

        if (nRetries % 20 == 0)
        {
            m_Logger.Write(FromKernel, LogNotice, "Wi-Fi polling poging %u...", nRetries / 20);
            m_WLAN.DumpStatus();
        }

        if (nRetries >= 200)
        {
            m_Logger.Write(FromKernel, LogWarning, "Wi-Fi start overgeslagen na timeout.");
            break;
        }
    }

    if (m_Net.IsRunning())
    {
        CString IPString;
        m_Net.GetConfig()->GetIPAddress()->Format(&IPString);
        m_Logger.Write(FromKernel, LogNotice, "Wi-Fi verbonden! IP-adres: %s", (const char *)IPString);
    }

    m_Logger.Write(FromKernel, LogNotice, "Start AtomRunner loop op Core 0...");
    m_AtomRunner.Run(0);

    return ShutdownHalt;
}