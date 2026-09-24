#include "kernel.h"
#include <circle/memory.h>
#include <circle/string.h>
#include <circle/koptions.h>
#include <circle/time.h>
#include <circle/devicenameservice.h>
#include <circle/net/ntpdaemon.h>
#include <circle/gpiopin.h>

#define DRIVE "SD:"
#define FIRMWARE_PATH DRIVE "/firmware/"
#define CONFIG_FILE DRIVE "/wpa_supplicant.conf"

static const char FromKernel[] = "kernel";

CKernel *CKernel::s_pThis = nullptr;

CKernel::CKernel(void)
    : m_FrameBuffer(0, 0, 32),
      m_Serial(&m_Interrupt),
      m_Timer(&m_Interrupt),
      m_Logger(m_Options.GetLogLevel(), &m_Timer),
      m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
      m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
      m_WLAN(FIRMWARE_PATH),
      m_Net(0, 0, 0, 0, DEFAULT_HOSTNAME, NetDeviceTypeWLAN),
      m_WPASupplicant(CONFIG_FILE),
      m_AtomRunner(&m_FrameBuffer, &m_Timer, &m_DeviceNameService, CMemorySystem::Get()),
      m_bJumperPresent(FALSE)
{
    s_pThis = this;
    m_AtomRunner.SetKernel(this);
    m_ActLED.Blink(5);
}

CKernel::~CKernel(void)
{
    s_pThis = nullptr;
}

boolean CKernel::Initialize(void)
{
    boolean bOK = TRUE;

    // 1. Hardware, Interrupts, Serial & Logger
    if (bOK)
        bOK = m_Interrupt.Initialize();
    if (bOK)
        bOK = m_Timer.Initialize();
    if (bOK)
        bOK = m_Serial.Initialize(115200);
    if (bOK)
        bOK = m_Logger.Initialize(&m_Serial);

    // 2. Beeldscherm & EMMC Storage
    if (bOK)
        bOK = m_FrameBuffer.Initialize();
    if (bOK)
        bOK = m_EMMC.Initialize();

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

    FILINFO fno;
    if (f_stat(CONFIG_FILE, &fno) == FR_OK) // DRIVE verwijderd!
    {
        m_Logger.Write(FromKernel, LogNotice, "wifi config file found");
        m_bJumperPresent = FALSE; // FALSE betekent: geen jumper / wel Wi-Fi starten
        bOK = m_WLAN.Initialize();
        if (bOK)
            bOK = m_Net.Initialize(FALSE);
        if (bOK)
            bOK = m_WPASupplicant.Initialize();
    }
    else
    {
        m_Logger.Write(FromKernel, LogNotice, "wifi config file not found");
        m_bJumperPresent = TRUE; // Negeer Wi-Fi in Run()
    }

    // 7. Initialiseer Atom Runner direct zodat USB Keyboard hooks op tijd geregistreerd zijn!
    if (bOK)
    {
        bOK = m_AtomRunner.Initialize();
    }

    return bOK;
}

TShutdownMode CKernel::Run(void)
{
    m_Logger.Write(FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

    if (!m_bJumperPresent)
    {
        m_Logger.Write(FromKernel, LogNotice, "Wachten op Wi-Fi en IP-adres via DHCP...");
        m_nWifiStatus = 1;
        unsigned nRetries = 0;
        while (!m_Net.IsRunning())
        {
            m_Scheduler.MsSleep(100);
            nRetries++;

            if (nRetries % 20 == 0)
            {
                m_Logger.Write(FromKernel, LogNotice, "Wachten op verbinding... (%u sec)", nRetries / 10);
                m_WLAN.DumpStatus();
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
            m_nWifiStatus = 0;
            m_Timer.SetTimeZone(120); // CEST (+2)
            m_Logger.Write(FromKernel, LogNotice, "Tijd synchroniseren via NTP (pool.ntp.org)...");
            new CNTPDaemon("nl.pool.ntp.org", &m_Net);

            for (unsigned nNtpWait = 0; nNtpWait < 50; nNtpWait++)
            {
                m_Scheduler.MsSleep(100);
                if (m_Timer.GetTime() > 100000)
                {
                    m_Logger.Write(FromKernel, LogNotice, "NTP tijd succesvoll gesynchroniseerd!");
                    break;
                }
            }
        }
    }

    // Start multi-core emulatie (Core 0 handelt emulator 0 + USB I/O af)
    m_Logger.Write(FromKernel, LogNotice, "Start AtomRunner...");
    m_AtomRunner.Run(0);

    return ShutdownHalt;
}