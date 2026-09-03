#ifndef _beeb_h
#define _beeb_h

#ifndef ARM_ALLOW_MULTI_CORE
#define ARM_ALLOW_MULTI_CORE
#endif

#include <circle/types.h>
#include <circle/multicore.h>
#include <circle/timer.h>
#include <circle/bcmframebuffer.h>
#include <circle/devicenameservice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/logger.h>
#include <fatfs/ff.h>

#include "bbc_machine.h"
#include "sn76489.h"
#include "roms.h"

#define BBC_BUF_W              640
#define BBC_BUF_H              480
#define BBC_SECTORS_PER_TRACK   10
#define BBC_SECTOR_SIZE        256
#define BBC_TRACKS              80

typedef struct {
    FIL     file;
    bool    isOpen;
    bool    is_dsd;
    bool    read_only;
} bbc_circle_disk_ctx_t;

struct BbcKeyPos {
    uint8_t row;
    uint8_t col;
};

class CBeebRunner : public CMultiCoreSupport
{
public:
    CBeebRunner(CBcmFrameBuffer *pFrameBuffer, CTimer *pTimer,
                CDeviceNameService *pDeviceNameService, CMemorySystem *pMemorySystem);
    virtual ~CBeebRunner(void);

    boolean Initialize(void);
    using CMultiCoreSupport::Run;
    virtual void Run(unsigned nCore) override;

    static CBeebRunner *Get(void) { return s_pThis; }
    CBcmFrameBuffer *GetFrameBuffer(void) const { return m_pFrameBuffer; }

    static void KeyStatusHandlerRaw(unsigned char ucModifiers, const unsigned char RawKeys[6]);
    static void KeyboardRemovedHandler(CDevice *pDevice, void *pContext);

private:
    bool MountSDDiskImage(void);
    void RenderBBCFrame(void);

    // Disk callbacks voor bbc_machine
    static int DiskReadSector(void *user_ctx, uint8_t drive, uint8_t track, uint8_t sector,
                              uint8_t side, uint8_t density, uint8_t *buf, uint16_t *len);
    static int DiskWriteSector(void *user_ctx, uint8_t drive, uint8_t track, uint8_t sector,
                               uint8_t side, uint8_t density, bool deleted, const uint8_t *buf, uint16_t len);
    static void DiskSeek(void *user_ctx, uint8_t drive, uint8_t track);

    static BbcKeyPos ConvertHIDToBBCKey(uint8_t hidCode);

    CBcmFrameBuffer             *m_pFrameBuffer;
    CTimer                      *m_pTimer;
    CDeviceNameService          *m_pDeviceNameService;
    CUSBKeyboardDevice *volatile m_pKeyboard;

    volatile boolean             m_bKeyboardReady;
    volatile boolean             m_bCoreInitDone;
    volatile boolean             m_bShutdown;

    bbc_machine_t                m_Machine;
    bbc_circle_disk_ctx_t        m_Disk[2];
    uint8_t                      m_RowBuf[BBC_BUF_W];

    static CBeebRunner          *s_pThis;
};

#endif