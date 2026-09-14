#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/usb/usbhcidevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>
#include <circle/net/netsubsystem.h>
#include <circle/types.h>
#include <circle/fs/fat/fatfs.h>

#include "beeb.h"

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CKernel
{
public:
    CKernel(void);
    ~CKernel(void);

    boolean Initialize(void);
    TShutdownMode Run(void);

    static CKernel *Get(void) { return s_pThis; }

private:
    CActLED             m_ActLED;
    CKernelOptions      m_Options;
    CDeviceNameService  m_DeviceNameService;
    CBcmFrameBuffer     m_FrameBuffer;
    CExceptionHandler   m_ExceptionHandler;
    CInterruptSystem    m_Interrupt;
    CSerialDevice       m_Serial;
    CTimer              m_Timer;
    CLogger             m_Logger;
    CEMMCDevice         m_EMMC;
    CUSBHCIDevice       m_USBHCI;
    CScheduler          m_Scheduler;
 //   CWLANAdapter        m_WLAN;
    CNetSubSystem       m_Net;
    CWPASupplicant      m_WPASupplicant;

    // BBC Micro Runner (Multi-Core)
    CBeebRunner         m_BeebRunner;

    boolean             m_bJumperPresent;

    static CKernel     *s_pThis;
};

#endif