#ifndef _kernel_h
#define _kernel_h

#include <circle/types.h>
#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/bcmframebuffer.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/fs/fat/fatfs.h>
#include <circle/multicore.h>
#include <circle/net/netsubsystem.h>
#include <circle/sched/scheduler.h> // <-- Toevoegen
//#include <wlan/bcm4343.h>
//#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>
// #include <wpa_supplicant/wpasupplicant.h>
//#include <emmc.h>
#include <SDCard/emmc.h>

#include "atom.h"

enum TShutdownMode
{
    ShutdownNone,
    ShutdownReboot,
    ShutdownHalt
};

class CKernel
{
public:
    CKernel(void);
    ~CKernel(void);

    boolean Initialize(void);
    TShutdownMode Run(void);

    static CKernel *Get(void) { return s_pThis; }
    CFATFileSystem *GetFileSystem(void) { return &m_FileSystem; }
    unsigned GetCurrentCore(void) { return CMultiCoreSupport::ThisCore(); }
    CAtomEmulator *GetEmulator(unsigned nCore) { return m_AtomRunner.GetEmulator(nCore); }
    CTextConsole *GetConsole(void) { return m_AtomRunner.GetConsole(); }
    CNetSubSystem *GetNet(void) { return &m_Net; }
    CBcm4343Device *GetWLAN(void) { return &m_WLAN; }

private:
  private:
    CActLED            m_ActLED;
    CKernelOptions     m_Options;
    CDeviceNameService m_DeviceNameService;
    CBcmFrameBuffer    m_FrameBuffer;
    CSerialDevice      m_Serial;
    CInterruptSystem   m_Interrupt;
    CTimer             m_Timer;
    CLogger            m_Logger;
    CScheduler         m_Scheduler; // <-- Toevoegen vóór apparaten/netwerk
    CEMMCDevice        m_EMMC;
    CFATFileSystem     m_FileSystem;
    CUSBHCIDevice      m_USBHCI;

    CBcm4343Device     m_WLAN;
    CNetSubSystem      m_Net;
    CWPASupplicant     m_WPASupplicant;

    CAtomRunner        m_AtomRunner;

    static CKernel    *s_pThis;
};

#endif