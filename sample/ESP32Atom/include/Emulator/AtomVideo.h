#pragma once

#include <stdint.h>
#include <stddef.h>
#include "PlatformTypes.h"

// Een rij van 8 Atom-pixels opgeschaald naar 16 HDMI-pixels (512 pixels breed)
// 16 pixels * 4 bytes = 64 bytes = 8 x uint64_t per font-byte
struct TPreUpscaledFontRow
{
    uint64_t dualPixels[8]; // 8 paren van elk 2 identieke 32-bit ARGB pixels
};

class CAtomVideoDirect
{
public:
    static void InitializeLUT(uint32_t fgColor, uint32_t bgColor);
    static void RenderTextRowDirect(uint32_t *pFrameBuffer, int pitchWords, int screenX, int screenY,
                                    const EmulatorByte *pVRAMRow, const EmulatorByte *pFontData, int nCss, uint32_t bgColor = 0xFF000000);

private:
    static TPreUpscaledFontRow s_FontLUT[256];
    static bool s_bLUTInitialized;
};

class CAtomVideo
{
public:
    CAtomVideo(void);
    ~CAtomVideo(void);

    void RenderLine(int nLine, const EmulatorByte *pRam, EmulatorByte nGfxMode, EmulatorByte nCss);

    // Getters
    const EmulatorByte *GetVRAM(void) const { return m_VRAM; }
    static const EmulatorByte *GetFontData(void) { return s_FontData; }
    static uint32_t GetColorARGB(EmulatorByte nPaletteIndex);

private:
    EmulatorByte m_VRAM[256 * 192]; // Lokale frame/scanlijn buffer

    static const EmulatorByte s_FontData[64 * 12];
    static const uint32_t s_PaletteARGB[9];
    static const int s_LinesPerRow[16];
    static const int s_BytesPerRow[16];
};