#ifndef _textconsole_h
#define _textconsole_h

#include <circle/bcmframebuffer.h>
#include <circle/types.h>
#include <stdarg.h>
#include <circle/util.h>



class CTextConsole
{
public:
    static constexpr int FONT_W = 8;
    static constexpr int FONT_H = 16;
    static constexpr int COLS = 41; // 41 * 8  = 328 pixels
    static constexpr int ROWS = 43; // 43 * 16 = 688 pixels

    CTextConsole(CBcmFrameBuffer *pFB, int nStartX, int nStartY,
                 u32 fgColor = 0xFFFFFFFF, u32 bgColor = 0xFF101010);
    ~CTextConsole();

    void Clear();
    void WriteChar(char ch);
    void WriteString(const char *pStr);
    void WriteFormat(const char *pFormat, ...) __attribute__((format(printf, 2, 3)));

private:
    volatile unsigned m_nLock = 0;
    void Lock()
    {
        while (__sync_lock_test_and_set(&m_nLock, 1))
        {
            asm volatile("yield");
        }
    }
    void Unlock()
    {
        __sync_lock_release(&m_nLock);
    }
    void ScrollUp();
    void RenderChar(int col, int row, char ch);
    void ClearRow(int row);

    CBcmFrameBuffer *m_pFB;
    int m_nStartX;
    int m_nStartY;
    u32 m_nFgColor;
    u32 m_nBgColor;

    int m_nCursorX;
    int m_nCursorY;

    char m_ScreenBuffer[ROWS][COLS];
};

#endif