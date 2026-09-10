//
// kernel.h
//
#ifndef _kernel_h
#define _kernel_h

#include <circle/types.h>

#ifndef DEPTH
#define DEPTH 32
#endif

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/logger.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/input/mouse.h>
#include <circle/memory.h>
#include "mandelbrot.h"

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

private:
    void MouseEventHandler(TMouseEvent Event, unsigned nButtons, unsigned nPosX, unsigned nPosY, int nWheelMove);
    static void MouseEventStub(TMouseEvent Event, unsigned nButtons, unsigned nPosX, unsigned nPosY, int nWheelMove);
    static void MouseRemovedHandler(CDevice *pDevice, void *pContext);
    void DrawLine(int nPosX1, int nPosY1, int nPosX2, int nPosY2, TScreenColor Color);
    void DrawXORRect(int x0, int y0, int x1, int y1);

private:
    CActLED m_ActLED;
    CKernelOptions m_Options;
    CDeviceNameService m_DeviceNameService;
    CScreenDevice m_Screen;
    CSerialDevice m_Serial;
    CInterruptSystem m_Interrupt;
    CTimer m_Timer;
    CLogger m_Logger;
    CUSBHCIDevice m_USBHCI;
    CMemorySystem m_Memory;

    CMandelbrotCalculator m_Mandelbrot;

    CMouseDevice *m_pMouse;
    int m_nPendingScrollY;
    int m_nPendingScrollX;
    int m_nPosX;
    int m_nPosY;
    unsigned m_nAnchorX;
    unsigned m_nAnchorY;
    int m_nLastRectX0;
    int m_nLastRectY0;
    int m_nLastRectX1;
    int m_nLastRectY1;
    boolean m_bSelecting;

    volatile TShutdownMode m_ShutdownMode;

    static CKernel *s_pThis;
};

#endif