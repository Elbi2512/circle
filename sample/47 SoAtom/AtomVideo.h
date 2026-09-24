#pragma once

#include <stdint.h>
#include <stddef.h>
#include <circle/types.h>

// Een rij van 8 Atom-pixels opgeschaald naar 16 HDMI-pixels (512 pixels breed)
// 16 pixels * 4 bytes = 64 bytes = 8 x uint64_t per font-byte
struct TPreUpscaledFontRow
{
    u64 dualPixels[8]; // 8 paren van elk 2 identieke 32-bit ARGB pixels
};

class CAtomVideoDirect
{
public:
    static void InitializeLUT(u32 fgColor, u32 bgColor);
    static void RenderTextRowDirect(u32 *pFrameBuffer, int pitchWords, int screenX, int screenY,
                                    const u8 *pVRAMRow, const u8 *pFontData, int nCss, u32 bgColor = 0xFF000000);

private:
    static TPreUpscaledFontRow s_FontLUT[256];
    static bool s_bLUTInitialized;
};

class CAtomVideo
{
public:
    CAtomVideo(void);
    ~CAtomVideo(void);

    void RenderLine(int nLine, const u8 *pRam, u8 nGfxMode, u8 nCss);

    // Getters
    const u8 *GetVRAM(void) const { return m_VRAM; }
    static const u8 *GetFontData(void) { return s_FontData; }
    static u32 GetColorARGB(u8 nPaletteIndex);

private:
    u8 m_VRAM[256 * 192]; // Lokale frame/scanlijn buffer

    static const u8 s_FontData[64 * 12];
    static const u32 s_PaletteARGB[9];
    static const int s_LinesPerRow[16];
    static const int s_BytesPerRow[16];
};