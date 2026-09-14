#include "kernel.h"
#include <circle/memory.h>
#include <circle/string.h>
#include <circle/koptions.h>
#include <circle/time.h>
#include <circle/devicenameservice.h>
#include <circle/net/ntpdaemon.h>
#include <circle/gpiopin.h>

#define DRIVE          "SD:"
#define FIRMWARE_PATH  DRIVE "/firmware/"
#define CONFIG_FILE    DRIVE "/wpa_supplicant.conf"
#define JUMPER_PIN     4

static const char FromKernel[] = "kernel";

CKernel *CKernel::s_pThis = nullptr;

CKernel::CKernel(void)
:   m_FrameBuffer(0, 0, 32),
    m_Serial(&m_Interrupt),
    m_Timer(&m_Interrupt),
    m_Logger(m_Options.GetLogLevel(), &m_Timer),
    m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
    m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
   // m_WLAN(FIRMWARE_PATH),
    m_Net(0, 0, 0, 0, DEFAULT_HOSTNAME, NetDeviceTypeWLAN),
    m_WPASupplicant(CONFIG_FILE),
    m_BeebRunner(&m_FrameBuffer, &m_Timer, &m_DeviceNameService, CMemorySystem::Get()),
    m_bJumperPresent(FALSE)
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
 //   CGPIOPin jumperPin(JUMPER_PIN, GPIOModeInputPullUp);

    // 1. Hardware, Interrupts, Serial & Logger
    if (bOK) bOK = m_Interrupt.Initialize();
    if (bOK) bOK = m_Timer.Initialize();
    if (bOK) bOK = m_Serial.Initialize(115200);
    if (bOK) bOK = m_Logger.Initialize(&m_Serial);

    // 2. Beeldscherm & EMMC Storage
    if (bOK) bOK = m_FrameBuffer.Initialize();
    if (bOK) bOK = m_EMMC.Initialize();

    // 3. SD Kaart Mounten via FatFs
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

    // 5. Jumper controleren
    m_bJumperPresent = 1;

    // 6. Wi-Fi & Netwerk stack (indien jumper niet geplaatst is)
    if (!m_bJumperPresent)
    {
        // if (bOK) bOK = m_WLAN.Initialize();
        // if (bOK) bOK = m_Net.Initialize(FALSE);
        // if (bOK) bOK = m_WPASupplicant.Initialize();
    }

    // 7. Initialiseer Beeb Runner (laadt ROMs, disk image en start multi-core)
    if (bOK)
    {
        bOK = m_BeebRunner.Initialize();
        if (!bOK)
        {
            m_Logger.Write(FromKernel, LogError, "Initialisatie van CBeebRunner mislukt!");
        }
    }

    return bOK;
}

TShutdownMode CKernel::Run(void)
{
    m_Logger.Write(FromKernel, LogNotice, "BBC Micro Bare-metal Kernel — Build: " __DATE__ " " __TIME__);

    if (!m_bJumperPresent)
    {
        m_Logger.Write(FromKernel, LogNotice, "Wachten op Wi-Fi en IP-adres via DHCP...");

        unsigned nRetries = 0;
        while (!m_Net.IsRunning())
        {
            m_Scheduler.MsSleep(100);
            nRetries++;

            if (nRetries % 20 == 0)
            {
                m_Logger.Write(FromKernel, LogNotice, "Wachten op verbinding... (%u sec)", nRetries / 10);
   //             m_WLAN.DumpStatus();
            }

            if (nRetries >= 300) // 30 seconden time-out
            {
                m_Logger.Write(FromKernel, LogWarning, "Wi-Fi time-out bereikt. Doorgaan...");
                break;
            }
        }

        if (m_Net.IsRunning())
        {
            CString IPString;
            m_Net.GetConfig()->GetIPAddress()->Format(&IPString);
            m_Logger.Write(FromKernel, LogNotice, "Wi-Fi verbonden! IP-adres: %s", (const char *)IPString);

            m_Timer.SetTimeZone(120); // CEST (+2)
            m_Logger.Write(FromKernel, LogNotice, "Tijd synchroniseren via NTP...");
            new CNTPDaemon("nl.pool.ntp.org", &m_Net);

            for (unsigned nNtpWait = 0; nNtpWait < 50; nNtpWait++)
            {
                m_Scheduler.MsSleep(100);
                if (m_Timer.GetTime() > 100000)
                {
                    m_Logger.Write(FromKernel, LogNotice, "NTP tijd succesvol gesynchroniseerd!");
                    break;
                }
            }
        }
    }

    // Start de Beeb Runner loop op Core 0 (Video + USB Master, Core 1 draait 6502 CPU)
    m_Logger.Write(FromKernel, LogNotice, "Start BeebRunner...");
    m_BeebRunner.Run(0);

    return ShutdownHalt;
}