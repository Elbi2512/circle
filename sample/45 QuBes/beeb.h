//
// beeb.h
//
#ifndef _beeb_h
#define _beeb_h

#ifndef ARM_ALLOW_MULTI_CORE
#define ARM_ALLOW_MULTI_CORE
#endif

// USB HID Keyboard Usage IDs (Standard)
#define VK_A            0x04
#define VK_B            0x05
#define VK_C            0x06
#define VK_D            0x07
#define VK_E            0x08
#define VK_F            0x09
#define VK_G            0x0A
#define VK_H            0x0B
#define VK_I            0x0C
#define VK_J            0x0D
#define VK_K            0x0E
#define VK_L            0x0F
#define VK_M            0x10
#define VK_N            0x11
#define VK_O            0x12
#define VK_P            0x13
#define VK_Q            0x14
#define VK_R            0x15
#define VK_S            0x16
#define VK_T            0x17
#define VK_U            0x18
#define VK_V            0x19
#define VK_W            0x1A
#define VK_X            0x1B
#define VK_Y            0x1C
#define VK_Z            0x1D

#define VK_1            0x1E
#define VK_2            0x1F
#define VK_3            0x20
#define VK_4            0x21
#define VK_5            0x22
#define VK_6            0x23
#define VK_7            0x24
#define VK_8            0x25
#define VK_9            0x26
#define VK_0            0x27

#define VK_RETURN       0x28
#define VK_ESCAPE       0x29
#define VK_BACKSPACE    0x2A
#define VK_TAB          0x2B
#define VK_SPACE        0x2C
#define VK_MINUS        0x2D    
#define VK_EQUALS       0x2E    
#define VK_LBRACKET     0x2F    
#define VK_RBRACKET     0x30    
#define VK_BACKSLASH    0x31    
#define VK_SEMICOLON    0x33    
#define VK_COLON        0x34    
#define VK_AT           0x35
#define VK_COMMA        0x36    
#define VK_PERIOD       0x37    
#define VK_SLASH        0x38    
#define VK_CAPSLOCK     0x39

#define VK_F1           0x3A
#define VK_F2           0x3B
#define VK_F3           0x3C
#define VK_F4           0x3D
#define VK_F5           0x3E
#define VK_F6           0x3F
#define VK_F7           0x40
#define VK_F8           0x41
#define VK_F9           0x42
#define VK_F10          0x43
#define VK_F11          0x44
#define VK_F12          0x45

#define VK_LCTRL        0xE0
#define VK_LSHIFT       0xE1
#define VK_LALT         0xE2
#define VK_LGUI         0xE3
#define VK_RCTRL        0xE4
#define VK_RSHIFT       0xE5
#define VK_RALT         0xE6
#define VK_RGUI         0xE7

#define VK_RIGHT        0x4F
#define VK_LEFT         0x50
#define VK_DOWN         0x51
#define VK_UP           0x52

#define VK_DELETE       0x4C
#define VK_INSERT       0x49
#define VK_HOME         0x4A
#define VK_END          0x4D
#define VK_PAGEUP       0x4B
#define VK_PAGEDOWN     0x4E

#include <circle/types.h>
#include <circle/multicore.h>
#include <circle/timer.h>
#include <circle/bcmframebuffer.h>
#include <circle/devicenameservice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/logger.h>
#include <fatfs/ff.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "bbc_machine.h"
#include "bbc_cpu.h"
#include "bbc_memory.h"
#include "roms.h"
#ifdef __cplusplus
}
#endif

#define BBC_BUF_W               640
#define BBC_BUF_H               480
#define BBC_SECTORS_PER_TRACK    10
#define BBC_SECTOR_SIZE         256
#define BBC_TRACKS               80

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

    // Disk callbacks
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