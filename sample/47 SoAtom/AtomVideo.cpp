#include "AtomVideo.h"
#include <circle/util.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Statische Data & Palette Definities
// ---------------------------------------------------------------------------
TPreUpscaledFontRow CAtomVideoDirect::s_FontLUT[256];
bool CAtomVideoDirect::s_bLUTInitialized = false;

// 8-bit Atom Palette index naar 32-bit ARGB (Circle FrameBuffer formaat)
const u32 CAtomVideo::s_PaletteARGB[9] = {
    0xFF000000, // 0: Zwart
    0xFF00FF00, // 1: Groen
    0xFFFFFF00, // 2: Geel
    0xFF0000FF, // 3: Blauw
    0xFFFF0000, // 4: Rood
    0xFFFFFFFF, // 5: Wit / Buff
    0xFF00FFFF, // 6: Cyaan
    0xFFFF00FF, // 7: Magenta
    0xFFFF8000  // 8: Oranje (SP4 mode)
};

// Scanlines per unieke VRAM-rij om altijd exact 192 scanlines te vullen:
// Even modi (0, 2, 4, 6, 8, 10, 12, 14): Tekst/Semigraphics (16 rijen x 12 = 192)
// Mode 1:  64x64   (64 x 3 = 192)
// Mode 3:  128x64  (64 x 3 = 192)
// Mode 5:  64x96   (96 x 2 = 192)  [Gecorrigeerd: was 3]
// Mode 7:  128x96  (96 x 2 = 192)
// Mode 9:  64x192  (192 x 1 = 192) [Gecorrigeerd: was 2]
// Mode 11: 128x192 (192 x 1 = 192)
// Mode 13: 128x192 (192 x 1 = 192)
// Mode 15: 256x192 (192 x 1 = 192)
const int CAtomVideo::s_LinesPerRow[16] = {
    12, 3, 12, 3, 12, 2, 12, 2, 12, 1, 12, 1, 12, 1, 12, 1};

// Aantal bytes per scanlinerij in Atom RAM:
// Mode 1, 3, 5, 7, 9, 11 gebruiken 16 bytes per rij.
// Tekstmodi, Mode 13 en Mode 15 gebruiken 32 bytes per rij.
const int CAtomVideo::s_BytesPerRow[16] = {
    32, 16, 32, 16, 32, 16, 32, 16, 32, 16, 32, 16, 32, 32, 32, 32};

// Embedded MC6847 Karaktergenerator Font (64 karakters x 12 scanlines)
const u8 CAtomVideo::s_FontData[64 * 12] = {
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x02, 0x1a, 0x2a, 0x2a, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x14, 0x22, 0x22, 0x3e, 0x22, 0x22, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x12, 0x12, 0x1c, 0x12, 0x12, 0x3c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x20, 0x20, 0x20, 0x22, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x12, 0x12, 0x12, 0x12, 0x12, 0x3c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x20, 0x20, 0x3c, 0x20, 0x20, 0x3e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x20, 0x20, 0x3c, 0x20, 0x20, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1e, 0x20, 0x20, 0x26, 0x22, 0x22, 0x1e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x3e, 0x22, 0x22, 0x22, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x22, 0x22, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x36, 0x2a, 0x2a, 0x22, 0x22, 0x22, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x32, 0x2a, 0x26, 0x22, 0x22, 0x22, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x22, 0x22, 0x22, 0x22, 0x22, 0x3e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x22, 0x22, 0x3c, 0x20, 0x20, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x22, 0x22, 0x2a, 0x24, 0x1a, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x22, 0x22, 0x3c, 0x28, 0x24, 0x22, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x10, 0x08, 0x04, 0x22, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x14, 0x14, 0x08, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x2a, 0x2a, 0x36, 0x22, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x22, 0x14, 0x08, 0x14, 0x22, 0x22, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x22, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x38, 0x20, 0x20, 0x20, 0x20, 0x20, 0x38, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x20, 0x10, 0x08, 0x04, 0x02, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x1c, 0x2a, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x10, 0x3e, 0x10, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x14, 0x14, 0x36, 0x00, 0x36, 0x14, 0x14, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x1e, 0x20, 0x1c, 0x02, 0x3c, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x32, 0x32, 0x04, 0x08, 0x10, 0x26, 0x26, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x10, 0x28, 0x28, 0x10, 0x2a, 0x24, 0x1a, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x10, 0x20, 0x20, 0x20, 0x10, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x1c, 0x3e, 0x1c, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x3e, 0x08, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x10, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x02, 0x04, 0x08, 0x10, 0x20, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x24, 0x24, 0x24, 0x24, 0x24, 0x18, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x02, 0x1c, 0x20, 0x20, 0x3e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x02, 0x0c, 0x02, 0x22, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x0c, 0x14, 0x3e, 0x04, 0x04, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x20, 0x3c, 0x02, 0x02, 0x22, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x20, 0x20, 0x3c, 0x22, 0x22, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x02, 0x04, 0x08, 0x10, 0x20, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x22, 0x1c, 0x22, 0x22, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x22, 0x1e, 0x02, 0x02, 0x1c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x08, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x24, 0x04, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00};

// ---------------------------------------------------------------------------
// CAtomVideoDirect Implementatie
// ---------------------------------------------------------------------------
void CAtomVideoDirect::InitializeLUT(u32 fgColor, u32 bgColor)
{
    u64 dualFG = ((u64)fgColor << 32) | fgColor;
    u64 dualBG = ((u64)bgColor << 32) | bgColor;

    for (int pattern = 0; pattern < 256; pattern++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            bool bPixelOn = (pattern & (0x80 >> bit)) != 0;
            s_FontLUT[pattern].dualPixels[bit] = bPixelOn ? dualFG : dualBG;
        }
    }
    s_bLUTInitialized = true;
}

void CAtomVideoDirect::RenderTextRowDirect(u32 *pFrameBuffer, int pitchWords, int screenX, int screenY,
                                           const u8 *pVRAMRow, const u8 *pFontData, int nCss, u32 bgColor)
{
    (void)nCss;
    const u32 fgColor = 0xFF00FF00; // Groen

    const u64 dualFG = ((u64)fgColor << 32) | fgColor;
    const u64 dualBG = ((u64)bgColor << 32) | bgColor;

    // 1 Karakterrij = 12 scanlines (24 HDMI-scanlijnen bij 2x opschaling)
    for (int charLine = 0; charLine < 12; charLine++)
    {
        u32 *pDst32_Line1 = pFrameBuffer + ((screenY + (2 * charLine + 0)) * pitchWords) + screenX;
        u32 *pDst32_Line2 = pFrameBuffer + ((screenY + (2 * charLine + 1)) * pitchWords) + screenX;

        u64 *pDst64_Line1 = (u64 *)pDst32_Line1;
        u64 *pDst64_Line2 = (u64 *)pDst32_Line2;

        int dstOffset = 0;

        for (int col = 0; col < 32; col++)
        {
            u8 rawByte = pVRAMRow[col];

            // 1. Semigraphics 3x2 Cell Matrix (Bit 6 = 1)
            if (rawByte & 0x40)
            {
                u32 blockColor = (rawByte & 0x80) ? CAtomVideo::GetColorARGB(4) : CAtomVideo::GetColorARGB(2);
                u64 semiFG = ((u64)blockColor << 32) | blockColor;
                u64 semiBG = dualBG;

                bool bLeft = false;
                bool bRight = false;

                if (charLine < 4)
                {
                    bLeft = (rawByte & 0x20) != 0;
                    bRight = (rawByte & 0x10) != 0;
                }
                else if (charLine < 8)
                {
                    bLeft = (rawByte & 0x08) != 0;
                    bRight = (rawByte & 0x04) != 0;
                }
                else
                {
                    bLeft = (rawByte & 0x02) != 0;
                    bRight = (rawByte & 0x01) != 0;
                }

                u64 leftDual = bLeft ? semiFG : semiBG;
                u64 rightDual = bRight ? semiFG : semiBG;

                for (int i = 0; i < 4; i++)
                {
                    pDst64_Line1[dstOffset + i] = leftDual;
                    pDst64_Line2[dstOffset + i] = leftDual;
                }
                for (int i = 4; i < 8; i++)
                {
                    pDst64_Line1[dstOffset + i] = rightDual;
                    pDst64_Line2[dstOffset + i] = rightDual;
                }
            }
            // 2. Alfanumeriek (Bit 6 = 0)
            else
            {
                u8 fontIndex = rawByte & 0x3F;
                u8 fontByte = pFontData[fontIndex * 12 + charLine];

                if (rawByte & 0x80)
                {
                    fontByte = ~fontByte;
                }

                // Genereer de 8 pixelparen direct met de actuele achtergrondkleur
                for (int bit = 0; bit < 8; bit++)
                {
                    bool bPixelOn = (fontByte & (0x80 >> bit)) != 0;
                    u64 dp = bPixelOn ? dualFG : dualBG;
                    pDst64_Line1[dstOffset + bit] = dp;
                    pDst64_Line2[dstOffset + bit] = dp;
                }
            }

            dstOffset += 8;
        }
    }
}

// ---------------------------------------------------------------------------
// CAtomVideo Implementatie
// ---------------------------------------------------------------------------
CAtomVideo::CAtomVideo(void)
{
    memset(m_VRAM, 0, sizeof(m_VRAM));
}

CAtomVideo::~CAtomVideo(void)
{
}

u32 CAtomVideo::GetColorARGB(u8 nPaletteIndex)
{
    if (nPaletteIndex > 8)
    {
        nPaletteIndex = 0;
    }
    return s_PaletteARGB[nPaletteIndex];
}

void CAtomVideo::RenderLine(int line, const u8 *pRam, u8 nGfxMode, u8 nCss)
{
    if (line < 0 || line >= 192 || !pRam)
    {
        return;
    }

    static const int textcol[4] = {0, 1, 0, 8};
    static const int semigrcol[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const int grcol[4] = {0, 1, 0, 5};

    nGfxMode &= 0x0F;

    // 1. Bepaal de verticale schaalfactor (1x, 2x of 3x)
    int scaleY = 1;
    switch (nGfxMode)
    {
    // 64-lijnen modi (3x herhalen naar 192 scanlijnen)
    case 1:
    case 11:
        scaleY = 3;
        break;

    // 96-lijnen modi (2x herhalen naar 192 scanlijnen: o.a. Mode 3 Snapper en Mode 7)
    case 3:
    case 5:
    case 7:
    case 9:
    case 13:
        scaleY = 2;
        break;

    // 192-lijnen modi (Tekstmodi en Mode 13, 15): 1:1 weergave
    default:
        scaleY = 1;
        break;
    }

    // Grenscontrole op het aantal binnenkomende bronlijnen
    if (line >= (192 / scaleY))
    {
        return;
    }

    // 2. Bereken VRAM bronadres en doelbuffer
    int bytesPerRow = s_BytesPerRow[nGfxMode];
    uint16_t lineAddr;

    if ((nGfxMode & 1) == 0)
    {
        // Even modi: Tekst / Semigraphics (16 karakterrijen x 12 scanlines)
        int row = line / 12;
        lineAddr = 0x8000 + (row * bytesPerRow);
    }
    else
    {
        // Grafische modi: 1 rij per bronlijn
        lineAddr = 0x8000 + (line * bytesPerRow);
    }

    u8 *pLineBuf = &m_VRAM[(line * scaleY) * 256];

    // 3. Render de pixels naar de eerste scanline van het blok
    switch (nGfxMode)
    {
    case 0:
    case 2:
    case 4:
    case 6:
    case 8:
    case 10:
    case 12:
    case 14:
    {
        int sy = line % 12;
        for (int x = 0; x < 256; x += 8)
        {
            u8 chr = pRam[lineAddr + (x >> 3)];

            if (chr & 0x40)
            {
                u8 temp = chr;
                chr <<= ((sy >> 2) << 1);
                chr = (chr >> 4) & 3;

                int col = (chr & 2) ? semigrcol[(temp >> 6) | (nCss << 1)] : 0;
                pLineBuf[x + 0] = pLineBuf[x + 1] = pLineBuf[x + 2] = pLineBuf[x + 3] = (u8)col;

                col = (chr & 1) ? semigrcol[(temp >> 6) | (nCss << 1)] : 0;
                pLineBuf[x + 4] = pLineBuf[x + 5] = pLineBuf[x + 6] = pLineBuf[x + 7] = (u8)col;
            }
            else
            {
                int fontIdx = ((chr & 0x3F) * 12) + sy;
                bool bInvert = (chr & 0x80) != 0;

                for (int xx = 0; xx < 8; xx++)
                {
                    u8 bit = (s_FontData[fontIdx] >> (xx ^ 7)) & 1;
                    if (bInvert)
                        bit ^= 1;
                    pLineBuf[x + xx] = (u8)textcol[bit | nCss];
                }
            }
        }
        break;
    }

    case 1:
    case 5:
    case 9:
        // 64 pixels breed, 4 kleuren (16 bytes/rij -> 4x horizontaal opgerekt)
        for (int x = 0; x < 256; x += 16)
        {
            //     u8 *pLineBuf = &m_VRAM[((line * 2) * scaleY) * 256];
            u8 temp = pRam[lineAddr + (x >> 4)];
            for (int xx = 0; xx < 16; xx += 4)
            {
                int col = semigrcol[(temp >> 6) | (nCss << 1)];
                pLineBuf[x + xx + 0] = (u8)col;
                pLineBuf[x + xx + 1] = (u8)col;
                pLineBuf[x + xx + 2] = (u8)col;
                pLineBuf[x + xx + 3] = (u8)col;
                temp <<= 2;
            }
        }
        break;

    case 3:
    case 7:
    case 11:
    {
        // 128 pixels breed, 2 kleuren (16 bytes/rij -> 2x horizontaal opgerekt)
        // u8 *pLineBuf = &m_VRAM[((line * 2) * scaleY) * 256];
        for (int x = 0; x < 256; x += 16)
        {
            u8 temp = pRam[lineAddr + (x >> 4)];
            for (int xx = 0; xx < 16; xx += 2)
            {
                int col = (temp & 0x80) ? grcol[nCss | 1] : grcol[nCss];
                pLineBuf[x + xx + 0] = (u8)col;
                pLineBuf[x + xx + 1] = (u8)col;
                temp <<= 1;
            }
        }
        break;
    }
    case 13:
        // 128 pixels breed, 4 kleuren (32 bytes/rij -> 2x horizontaal opgerekt naar 256)
        for (int x = 0; x < 256; x += 8)
        {
            u8 temp = pRam[lineAddr + (x >> 3)];
            for (int xx = 0; xx < 8; xx += 2)
            {
                int col = semigrcol[(temp >> 6) | (nCss << 1)];
                pLineBuf[x + xx + 0] = (u8)col;
                pLineBuf[x + xx + 1] = (u8)col;
                temp <<= 2;
            }
        }
        break;

    case 15:
        // 256 pixels breed, 2 kleuren (32 bytes/rij -> 1:1)
        for (int x = 0; x < 256; x += 8)
        {
            u8 temp = pRam[lineAddr + (x >> 3)];
            for (int xx = 0; xx < 8; xx++)
            {
                int col = (temp & 0x80) ? grcol[nCss | 1] : grcol[nCss];
                pLineBuf[x + xx] = (u8)col;
                temp <<= 1;
            }
        }
        break;
    }
    // 4. Dupliceer de zojuist berekende scanline
    if (scaleY == 2)
    {
        memcpy(pLineBuf + 256, pLineBuf, 256);
    }
    else if (scaleY == 3)
    {
        // Lijn 1 en lijn 2 in één aanroep vullen:
        memcpy(pLineBuf + 256, pLineBuf, 256);
        memcpy(pLineBuf + 512, pLineBuf, 256);
    }
}