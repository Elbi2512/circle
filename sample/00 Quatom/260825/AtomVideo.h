#pragma once

#include <stdint.h>
#include <stddef.h>
#include <circle/types.h>
#include <stdlib.h>
#include <circle/util.h>
#include <circle/string.h>

class CAtomVideo
{
public:
    CAtomVideo(void);
    ~CAtomVideo(void);

    void RenderFrame(const u8 *pRam, u8 nGfxMode, u8 nCss, bool bColourBoard = true);
    void RenderLine(int nLine, const u8 *pRam, u8 nGfxMode, u8 nCss);
    
    // Pointer naar de lokale 256x192 8-bit index-pixelbuffer
    const u8 *GetVRAM(void) const { return m_VRAM; }

    // Converteer 8-bit palette index naar 32-bit ARGB kleur voor Circle FrameBuffer
    static u32 GetColorARGB(u8 nPaletteIndex);

private:
    u8 m_VRAM[256 * 192]; // Off-screen buffer (256x192 pixels)
    int m_Sy;              // Scanline teller binnen karakter (0-11)
    int m_Cy;              // Karakterrij teller
    u16 m_Addr;            // Huidige MC6847 RAM offset

    static const u8 s_FontData[64 * 12];
    static const u32 s_PaletteARGB[9];
    static const u32 s_MonoPaletteARGB[9];
    static const int s_LinesPerRow[16];
    static const int s_BytesPerRow[16];
    static const u16 s_MaskPerRow[16];
};