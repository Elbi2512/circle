//
// mandelbrot.cpp
//
#include "mandelbrot.h"
#include <circle/synchronize.h>
#include <circle/logger.h>
#include <stdint.h>
#include <circle/chargenerator.h>
#include <circle/string.h>
#include <circle/util.h>
#include <math.h>

static const char FromMandel[] = "mandel";

// Dynamische Z-buffer en Backbuffer in het RAM voor flicker-vrije weergave
static u16 *s_pZBuffer = 0;
static u32 *s_pBackBuffer = 0;
static unsigned s_nZBufferWidth = 0;
static unsigned s_nZBufferHeight = 0;

void CMandelbrotCalculator::UpdateMousePosition(unsigned nMouseX, unsigned nMouseY)
{
    m_nMouseX = nMouseX;
    m_nMouseY = nMouseY;

    if (m_b3DMode && m_pScreen)
    {
        const float fbWidth  = (float)m_pScreen->GetWidth();
        const float fbHeight = (float)m_pScreen->GetHeight();

        // Muis X stuurt de rotatiehoek rondom (Yaw)
        float normX = ((float)nMouseX / fbWidth) * 2.0f - 1.0f;
        m_fYaw = normX * 3.14159f;

        // Muis Y stuurt de kantelhoek (Pitch)
        float normY = (float)nMouseY / fbHeight;
        m_fPitch = 0.15f + normY * 1.30f;

        m_nCurrentFrame++;
        DataSyncBarrier();
        asm volatile("sev");
        StepCore0();
    }
}

CMandelbrotCalculator::CMandelbrotCalculator(CScreenDevice *pScreen, CMemorySystem *pMemorySystem)
    : CMultiCoreSupport(pMemorySystem),
      m_pScreen(pScreen),
      m_pFrameBuffer(0),
      m_fX1(MANDELBROT_MIN_X),
      m_fX2(MANDELBROT_MAX_X),
      m_fY1(MANDELBROT_MIN_Y),
      m_fY2(MANDELBROT_MAX_Y),
      m_nCurrentFrame(0),
      m_bShutdown(FALSE),
      m_nMouseX(0),
      m_nMouseY(0),
      m_nMaxIterations(128),
      m_b3DMode(FALSE),
      m_fPitch(0.75f),
      m_fYaw(0.0f),
      m_fCamDist(0.0f),
      m_fFocalLength(0.0f),
      m_fHeightScale(0.015f)
{
    for (unsigned i = 0; i < 4; i++)
    {
        m_nFrameCounter[i] = 0;
    }

    m_pGrid = new Point3D[GRID_W * GRID_H];
    m_pScreenPoints = new Point2D[GRID_W * GRID_H];
}

CMandelbrotCalculator::~CMandelbrotCalculator(void)
{
    Shutdown();
    delete[] m_pGrid;
    delete[] m_pScreenPoints;

    if (s_pZBuffer) { delete[] s_pZBuffer; s_pZBuffer = 0; }
    if (s_pBackBuffer) { delete[] s_pBackBuffer; s_pBackBuffer = 0; }
}

boolean CMandelbrotCalculator::Initialize(void)
{
    if (m_pScreen != 0)
    {
        m_pFrameBuffer = m_pScreen->GetFrameBuffer();

        s_nZBufferWidth = m_pScreen->GetWidth();
        s_nZBufferHeight = m_pScreen->GetHeight();
        s_pZBuffer = new u16[s_nZBufferWidth * s_nZBufferHeight];
            s_pBackBuffer = new u32[s_nZBufferWidth * s_nZBufferHeight];
    }

    return CMultiCoreSupport::Initialize();
}

void CMandelbrotCalculator::Shutdown(void)
{
    m_bShutdown = TRUE;
    DataSyncBarrier();
    asm volatile("sev");
}

void CMandelbrotCalculator::Toggle3DMode(void)
{
    m_b3DMode = !m_b3DMode;
    CLogger::Get()->Write(FromMandel, LogNotice, "3D Mode: %s", m_b3DMode ? "ON" : "OFF");

    CalculateAll(m_fX1, m_fX2, m_fY1, m_fY2);
}

void CMandelbrotCalculator::CalculateAll(float x1, float x2, float y1, float y2)
{
    float realX1 = (x1 < x2) ? x1 : x2;
    float realX2 = (x1 < x2) ? x2 : x1;
    float realY1 = (y1 < y2) ? y1 : y2;
    float realY2 = (y1 < y2) ? y2 : y1;

    m_fX1 = realX1;
    m_fX2 = realX2;
    m_fY1 = realY1;
    m_fY2 = realY2;

    const float baseSpanX = MANDELBROT_MAX_X - MANDELBROT_MIN_X;
    float currentSpanX = m_fX2 - m_fX1;
    if (currentSpanX <= 0.0f)
        currentSpanX = 0.000001f;

    float zoomFactor = baseSpanX / currentSpanX;
    int zoomLevels = 0;
    float zf = zoomFactor;
    while (zf >= 2.0f)
    {
        zoomLevels++;
        zf *= 0.5f;
    }

    int iters = 128 + (zoomLevels * 32);
    if (iters < 128)  iters = 128;
    if (iters > 1024) iters = 1024;
    m_nMaxIterations = (unsigned)iters;

    m_nCurrentFrame++;
    DataSyncBarrier();
    asm volatile("sev");

    StepCore0();
}

void CMandelbrotCalculator::ScrollY(int nLines)
{
    if (nLines == 0 || !m_pScreen) return;

    const unsigned nHeight = m_pScreen->GetHeight();
    const float fRangeY = m_fY2 - m_fY1;
    const float fPixelStepY = fRangeY / (float)nHeight;

    const float fShiftY = (float)nLines * fPixelStepY;
    float newY1 = m_fY1 - fShiftY;
    float newY2 = m_fY2 - fShiftY;

    if (newY1 < MANDELBROT_MIN_Y)
    {
        if (m_fY1 <= MANDELBROT_MIN_Y) return;
        newY1 = MANDELBROT_MIN_Y;
        newY2 = newY1 + fRangeY;
    }
    else if (newY2 > MANDELBROT_MAX_Y)
    {
        if (m_fY2 >= MANDELBROT_MAX_Y) return;
        newY2 = MANDELBROT_MAX_Y;
        newY1 = newY2 - fRangeY;
    }

    CalculateAll(m_fX1, m_fX2, newY1, newY2);
}

void CMandelbrotCalculator::ScrollX(int nPixels)
{
    if (nPixels == 0 || !m_pScreen) return;

    const unsigned nWidth = m_pScreen->GetWidth();
    const float fRangeX = m_fX2 - m_fX1;
    const float fPixelStepX = fRangeX / (float)nWidth;

    const float fShiftX = (float)nPixels * fPixelStepX;
    float newX1 = m_fX1 + fShiftX;
    float newX2 = m_fX2 + fShiftX;

    if (newX1 < MANDELBROT_MIN_X)
    {
        if (m_fX1 <= MANDELBROT_MIN_X) return;
        newX1 = MANDELBROT_MIN_X;
        newX2 = newX1 + fRangeX;
    }
    else if (newX2 > MANDELBROT_MAX_X)
    {
        if (m_fX2 >= MANDELBROT_MAX_X) return;
        newX2 = MANDELBROT_MAX_X;
        newX1 = newX2 - fRangeX;
    }

    CalculateAll(newX1, newX2, m_fY1, m_fY2);
}

static inline u32 IterationToColor(unsigned nIteration, unsigned maxIter, unsigned nBytesPerPixel)
{
    u8 r = 0, g = 0, b = 0;
    if (nIteration < maxIter)
    {
        r = (u8)((nIteration * 9) & 0xFF);
        g = (u8)((nIteration * 5) & 0xFF);
        b = (u8)((nIteration * 13) & 0xFF);
    }

    if (nBytesPerPixel == 4)
    {
        return (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
    else
    {
        return (u32)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

void CMandelbrotCalculator::Draw3DLine(int x0, int y0, int x1, int y1, u32 color, u32 *pFB)
{
    if (!pFB || !m_pScreen)
        return;

    const int width = (int)m_pScreen->GetWidth();
    const int height = (int)m_pScreen->GetHeight();
    const int pitch = (int)(m_pFrameBuffer->GetPitch() / sizeof(u32));
    const int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    const int dy = (y1 >= y0) ? (y0 - y1) : (y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int error = dx + dy;

    for (;;)
    {
        if (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height)
            pFB[y0 * pitch + x0] = color;

        if (x0 == x1 && y0 == y1)
            break;

        const int doubledError = 2 * error;
        if (doubledError >= dy)
        {
            error += dy;
            x0 += sx;
        }
        if (doubledError <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}

// -----------------------------------------------------------------------------
// Driehoek rasterizer MET Z-BUFFER en Backbuffer rendering
// -----------------------------------------------------------------------------
static void FillTriangleZ(int x0, int y0, float z0,
                          int x1, int y1, float z1,
                          int x2, int y2, float z2,
                          u32 color, u32 *pBackBuf, unsigned nBytesPerPixel,
                          int fbWidth, int fbHeight)
{
    if (y0 > y1) { int tx = x0; x0 = x1; x1 = tx; int ty = y0; y0 = y1; y1 = ty; float tz = z0; z0 = z1; z1 = tz; }
    if (y1 > y2) { int tx = x1; x1 = x2; x2 = tx; int ty = y1; y1 = y2; y2 = ty; float tz = z1; z1 = z2; z2 = tz; }
    if (y0 > y1) { int tx = x0; x0 = x1; x1 = tx; int ty = y0; y0 = y1; y1 = ty; float tz = z0; z0 = z1; z1 = tz; }

    if (y2 < 0 || y0 >= fbHeight) return;

    int total_height = y2 - y0;
    if (total_height == 0) return;

    for (int y = y0; y <= y2; y++)
    {
        if (y < 0 || y >= fbHeight) continue;

        boolean second_half = (y > y1) || (y1 == y0);
        int segment_height = second_half ? (y2 - y1) : (y1 - y0);
        if (segment_height == 0) continue;

        float alpha = (float)(y - y0) / (float)total_height;
        float beta  = (float)(y - (second_half ? y1 : y0)) / (float)segment_height;

        int ax = x0 + (int)((float)(x2 - x0) * alpha);
        float az = z0 + (z2 - z0) * alpha;

        int bx = second_half ? (x1 + (int)((float)(x2 - x1) * beta))
                             : (x0 + (int)((float)(x1 - x0) * beta));
        float bz = second_half ? (z1 + (z2 - z1) * beta)
                               : (z0 + (z1 - z0) * beta);

        if (ax > bx) { int t = ax; ax = bx; bx = t; float tz = az; az = bz; bz = tz; }

        if (ax < 0) ax = 0;
        if (bx >= fbWidth) bx = fbWidth - 1;

        if (ax <= bx)
        {
            u32 *pLine = pBackBuf + (y * fbWidth);
            u16 *pZLine = s_pZBuffer + (y * fbWidth);
            float span = (float)(bx - ax);
            float dz = (span > 0.0f) ? ((bz - az) / span) : 0.0f;
            float curZ = az;

            for (int x = ax; x <= bx; x++, curZ += dz)
            {
                u16 depthVal = (u16)(curZ * 1000.0f + 30000.0f);

                if (depthVal < pZLine[x])
                {
                    pZLine[x] = depthVal;
                    pLine[x] = color;
                }
            }
        }
    }
}

float CMandelbrotCalculator::ComputeSmoothIteration(float cr, float ci)
{
    float y2 = ci * ci;
    float xMinusQuarter = cr - 0.25f;
    float q = xMinusQuarter * xMinusQuarter + y2;

    if (q * (q + xMinusQuarter) <= 0.25f * y2) return (float)m_nMaxIterations;
    if ((cr + 1.0f) * (cr + 1.0f) + y2 <= 0.0625f) return (float)m_nMaxIterations;

    float zr = 0.0f, zi = 0.0f;
    unsigned iter = 0;
    const unsigned maxI = m_nMaxIterations;

    while (zr * zr + zi * zi <= 4.0f && iter < maxI)
    {
        float zr2 = zr * zr - zi * zi + cr;
        zi = 2.0f * zr * zi + ci;
        zr = zr2;
        iter++;
    }

    return (float)iter;
}

void CMandelbrotCalculator::CalculateGrid3D(unsigned nCore)
{
}

u32 CMandelbrotCalculator::GetHeightColor(float h)
{
    if (h < 0.0f)
        h = 0.0f;
    if (h > 1.0f)
        h = 1.0f;

    const u8 red = (u8)(32.0f + 223.0f * h);
    const u8 green = (u8)(64.0f + 160.0f * (1.0f - h));
    const u8 blue = (u8)(192.0f + 63.0f * (1.0f - h));
    return 0xFF000000u | ((u32)red << 16) | ((u32)green << 8) | blue;
}

void CMandelbrotCalculator::StepCore0(void)
{
    if (m_nFrameCounter[0] != m_nCurrentFrame && m_pScreen && m_pFrameBuffer)
    {
        if (m_b3DMode)
        {
            const float spanX = m_fX2 - m_fX1;
            const float spanY = m_fY2 - m_fY1;
            const float midX  = (m_fX1 + m_fX2) * 0.5f;
            const float midY  = (m_fY1 + m_fY2) * 0.5f;

            const float dx = spanX / (float)(GRID_W - 1);
            const float dy = spanY / (float)(GRID_H - 1);

            for (unsigned gy = 0; gy < GRID_H; ++gy)
            {
                float ci = m_fY1 + (float)gy * dy;

                for (unsigned gx = 0; gx < GRID_W; ++gx)
                {
                    float cr = m_fX1 + (float)gx * dx;
                    float iter = ComputeSmoothIteration(cr, ci);

                    unsigned idx = gy * GRID_W + gx;

                    m_pGrid[idx].x = (cr - midX) * (2.4f / spanX);
                    m_pGrid[idx].y = (ci - midY) * (2.4f / spanY);

                    // Zwarte gat (binnen set) = diepte 0, buitenranden vormen het reliëf
                    if (iter >= (float)m_nMaxIterations)
                    {
                        m_pGrid[idx].z = 0.0f;
                    }
                    else
                    {
                        float depthFactor = (float)m_nMaxIterations / (iter + 5.0f);
                        m_pGrid[idx].z = logf(1.0f + depthFactor) * 0.35f;
                    }
                }
            }

            RenderPerspective3D();

            m_nFrameCounter[0] = m_nCurrentFrame;
            DataSyncBarrier();
        }
        else
        {
            unsigned nWidth = m_pScreen->GetWidth();
            unsigned nHeight = m_pScreen->GetHeight();
            unsigned nHalfW = nWidth / 2;
            unsigned nHalfH = nHeight / 2;

            float fMidX = (m_fX1 + m_fX2) * 0.5f;
            float fMidY = (m_fY1 + m_fY2) * 0.5f;

            CalculateQuad(m_fX1, fMidX, m_fY2, fMidY,
                          0, nHalfW,
                          0, nHalfH);

            m_nFrameCounter[0] = m_nCurrentFrame;
            DataSyncBarrier();
        }
    }
}

void CMandelbrotCalculator::Run(unsigned nCore)
{
    while (!m_bShutdown)
    {
        while (m_nFrameCounter[nCore] == m_nCurrentFrame && !m_bShutdown)
        {
            asm volatile("wfe");
        }

        if (m_bShutdown || !m_pScreen || !m_pFrameBuffer)
        {
            break;
        }

        if (!m_b3DMode)
        {
            unsigned nWidth = m_pScreen->GetWidth();
            unsigned nHeight = m_pScreen->GetHeight();
            unsigned nHalfW = nWidth / 2;
            unsigned nHalfH = nHeight / 2;

            float fMidX = (m_fX1 + m_fX2) * 0.5f;
            float fMidY = (m_fY1 + m_fY2) * 0.5f;

            switch (nCore)
            {
            case 1:
                CalculateQuad(fMidX, m_fX2, m_fY2, fMidY,
                              nHalfW, nWidth - nHalfW,
                              0, nHalfH);
                break;
            case 2:
                CalculateQuad(m_fX1, fMidX, fMidY, m_fY1,
                              0, nHalfW,
                              nHalfH, nHeight - nHalfH);
                break;
            case 3:
                CalculateQuad(fMidX, m_fX2, fMidY, m_fY1,
                              nHalfW, nWidth - nHalfW,
                              nHalfH, nHeight - nHalfH);
                break;
            }
        }

        m_nFrameCounter[nCore] = m_nCurrentFrame;
        DataSyncBarrier();
    }
}

// -----------------------------------------------------------------------------
// 3D Renderer met Z-Buffer en Flicker-vrije Backbuffer blitting
// -----------------------------------------------------------------------------
void CMandelbrotCalculator::RenderPerspective3D(void)
{
    if (!m_pFrameBuffer || !m_pScreen || !s_pZBuffer || !s_pBackBuffer) return;

    u8 *pRaw = (u8 *)(uintptr_t)m_pFrameBuffer->GetBuffer();
    const unsigned fbWidth        = m_pScreen->GetWidth();
    const unsigned fbHeight       = m_pScreen->GetHeight();
    const unsigned nPitchBytes    = m_pFrameBuffer->GetPitch();
    const unsigned nBytesPerPixel = m_pFrameBuffer->GetDepth() / 8;

    // 1. Backbuffer en Z-buffer legen in het RAM (geen schermknippering!)
    memset(s_pBackBuffer, 0, fbWidth * fbHeight * sizeof(u32));
    memset(s_pZBuffer, 0xFF, fbWidth * fbHeight * sizeof(u16));

    // 2. Camerahoeken en projectie
    const float cosY = cosf(m_fYaw);
    const float sinY = sinf(m_fYaw);
    const float cosP = cosf(m_fPitch);
    const float sinP = sinf(m_fPitch);

    const float scaleX = (float)fbWidth * 0.32f;
    const float scaleY = (float)fbHeight * 0.30f;
    const float scaleZ = (float)fbHeight * 0.50f;

    const float centerX = (float)fbWidth * 0.5f;
    const float centerY = (float)fbHeight * 0.55f;

    for (int i = 0; i < GRID_W * GRID_H; ++i)
    {
        float wx = m_pGrid[i].x;
        float wy = m_pGrid[i].y;
        float wz = m_pGrid[i].z;

        float rx = wx * cosY - wy * sinY;
        float ry = wx * sinY + wy * cosY;
        float rz = wz;

        float sx = centerX + rx * scaleX;
        float sy = centerY + (ry * sinP * scaleY) - (rz * cosP * scaleZ);

        float depth = ry * cosP + rz * sinP;

        m_pScreenPoints[i].x = (int)sx;
        m_pScreenPoints[i].y = (int)sy;
        m_pGrid[i].y = depth;
        m_pScreenPoints[i].valid = TRUE;
    }

    // 3. Render naar de backbuffer
    const float spanX = m_fX2 - m_fX1;
    const float spanY = m_fY2 - m_fY1;
    const float dx = spanX / (float)(GRID_W - 1);
    const float dy = spanY / (float)(GRID_H - 1);

    for (int gy = 0; gy < (int)GRID_H - 1; ++gy)
    {
        float ci = m_fY1 + (float)gy * dy;

        for (int gx = 0; gx < (int)GRID_W - 1; ++gx)
        {
            float cr = m_fX1 + (float)gx * dx;
            unsigned iter = (unsigned)ComputeSmoothIteration(cr, ci);

            u32 color = IterationToColor(iter, m_nMaxIterations, nBytesPerPixel);

            int idxTL = gy * GRID_W + gx;
            int idxTR = gy * GRID_W + (gx + 1);
            int idxBL = (gy + 1) * GRID_W + gx;
            int idxBR = (gy + 1) * GRID_W + (gx + 1);

            Point2D pTL = m_pScreenPoints[idxTL];
            Point2D pTR = m_pScreenPoints[idxTR];
            Point2D pBL = m_pScreenPoints[idxBL];
            Point2D pBR = m_pScreenPoints[idxBR];

            float zTL = m_pGrid[idxTL].y;
            float zTR = m_pGrid[idxTR].y;
            float zBL = m_pGrid[idxBL].y;
            float zBR = m_pGrid[idxBR].y;

            FillTriangleZ(pTL.x, pTL.y, zTL,
                          pTR.x, pTR.y, zTR,
                          pBL.x, pBL.y, zBL,
                          color, s_pBackBuffer, nBytesPerPixel, fbWidth, fbHeight);

            FillTriangleZ(pTR.x, pTR.y, zTR,
                          pBR.x, pBR.y, zBR,
                          pBL.x, pBL.y, zBL,
                          color, s_pBackBuffer, nBytesPerPixel, fbWidth, fbHeight);
        }
    }

    // 4. Blit de volledige backbuffer in één vloeiende beweging naar de actieve framebuffer
    for (unsigned y = 0; y < fbHeight; y++)
    {
        u8 *pDst = pRaw + y * nPitchBytes;
        const u32 *pSrc = s_pBackBuffer + y * fbWidth;
        if (nBytesPerPixel == 4)
        {
            memcpy(pDst, pSrc, fbWidth * sizeof(u32));
        }
        else
        {
            u16 *pDst16 = (u16 *)pDst;
            for (unsigned x = 0; x < fbWidth; ++x)
                pDst16[x] = (u16)pSrc[x];
        }
    }

    CleanDataCache();
}

// -----------------------------------------------------------------------------
// 2D Mandelbrot functies
// -----------------------------------------------------------------------------
static inline unsigned ComputeMandelPixel(float x0, float y0, unsigned maxIter)
{
    float y2 = y0 * y0;
    float xMinusQuarter = x0 - 0.25f;
    float q = xMinusQuarter * xMinusQuarter + y2;

    if (q * (q + xMinusQuarter) <= 0.25f * y2) return maxIter;
    if ((x0 + 1.0f) * (x0 + 1.0f) + y2 <= 0.0625f) return maxIter;

    float x = 0.0f, y = 0.0f;
    float xSaved = 0.0f, ySaved = 0.0f;

    unsigned nIteration = 0;
    unsigned nCheckInterval = 1;
    unsigned nStepsToNextCheck = 1;

    while (x * x + y * y <= 4.0f && nIteration < maxIter)
    {
        float xtmp = x * x - y * y + x0;
        y = 2.0f * x * y + y0;
        x = xtmp;
        nIteration++;

        if (x == xSaved && y == ySaved) return maxIter;

        nStepsToNextCheck--;
        if (nStepsToNextCheck == 0)
        {
            xSaved = x;
            ySaved = y;
            nCheckInterval <<= 1;
            nStepsToNextCheck = nCheckInterval;
        }
    }

    return nIteration;
}

void CMandelbrotCalculator::CalculateQuad(float x1, float x2, float yTop, float yBottom,
                                          unsigned nPosX0, unsigned nWidth,
                                          unsigned nPosY0, unsigned nHeight)
{
    u8 *pRawBuffer = (u8 *)(uintptr_t)m_pFrameBuffer->GetBuffer();
    if (!pRawBuffer) return;

    const unsigned maxIter = m_nMaxIterations;
    const unsigned nPitchBytes = m_pFrameBuffer->GetPitch();
    const unsigned nBytesPerPixel = m_pFrameBuffer->GetDepth() / 8;

    const float dx = (x2 - x1) / (float)nWidth;
    const float dy = (yBottom - yTop) / (float)nHeight;

    const unsigned BLOCK_SIZE = 16;

    for (unsigned by = 0; by < nHeight; by += BLOCK_SIZE)
    {
        unsigned blockH = (by + BLOCK_SIZE <= nHeight) ? BLOCK_SIZE : (nHeight - by);

        for (unsigned bx = 0; bx < nWidth; bx += BLOCK_SIZE)
        {
            unsigned blockW = (bx + BLOCK_SIZE <= nWidth) ? BLOCK_SIZE : (nWidth - bx);

            if (blockW < 4 || blockH < 4)
            {
                for (unsigned ry = 0; ry < blockH; ry++)
                {
                    unsigned py = nPosY0 + by + ry;
                    float cy = yTop + (float)(by + ry) * dy;
                    u8 *pLine = pRawBuffer + (py * nPitchBytes);

                    for (unsigned rx = 0; rx < blockW; rx++)
                    {
                        unsigned px = nPosX0 + bx + rx;
                        float cx = x1 + (float)(bx + rx) * dx;
                        u32 color = IterationToColor(ComputeMandelPixel(cx, cy, maxIter), maxIter, nBytesPerPixel);

                        if (nBytesPerPixel == 4)
                            *(u32 *)(pLine + px * 4) = color;
                        else
                            *(u16 *)(pLine + px * 2) = (u16)color;
                    }
                    CleanDataCacheRange((uintptr_t)(pLine + (nPosX0 + bx) * nBytesPerPixel), blockW * nBytesPerPixel);
                }
                continue;
            }

            float cx0 = x1 + (float)bx * dx;
            float cy0 = yTop + (float)by * dy;
            unsigned firstIter = ComputeMandelPixel(cx0, cy0, maxIter);
            boolean bUniform = TRUE;

            for (unsigned rx = 0; rx < blockW; rx++)
            {
                float cx = x1 + (float)(bx + rx) * dx;
                if (ComputeMandelPixel(cx, cy0, maxIter) != firstIter ||
                    ComputeMandelPixel(cx, yTop + (float)(by + blockH - 1) * dy, maxIter) != firstIter)
                {
                    bUniform = FALSE;
                    break;
                }
            }

            if (bUniform)
            {
                float cxRight = x1 + (float)(bx + blockW - 1) * dx;
                for (unsigned ry = 1; ry < blockH - 1; ry++)
                {
                    float cy = yTop + (float)(by + ry) * dy;
                    if (ComputeMandelPixel(cx0, cy, maxIter) != firstIter ||
                        ComputeMandelPixel(cxRight, cy, maxIter) != firstIter)
                    {
                        bUniform = FALSE;
                        break;
                    }
                }
            }

            if (bUniform)
            {
                u32 color = IterationToColor(firstIter, maxIter, nBytesPerPixel);
                for (unsigned ry = 0; ry < blockH; ry++)
                {
                    unsigned py = nPosY0 + by + ry;
                    u8 *pLine = pRawBuffer + (py * nPitchBytes);

                    if (nBytesPerPixel == 4)
                    {
                        u32 *pPix = (u32 *)(pLine + (nPosX0 + bx) * 4);
                        for (unsigned rx = 0; rx < blockW; rx++) pPix[rx] = color;
                    }
                    else
                    {
                        u16 *pPix = (u16 *)(pLine + (nPosX0 + bx) * 2);
                        for (unsigned rx = 0; rx < blockW; rx++) pPix[rx] = (u16)color;
                    }
                    CleanDataCacheRange((uintptr_t)(pLine + (nPosX0 + bx) * nBytesPerPixel), blockW * nBytesPerPixel);
                }
            }
            else
            {
                for (unsigned ry = 0; ry < blockH; ry++)
                {
                    unsigned py = nPosY0 + by + ry;
                    float cy = yTop + (float)(by + ry) * dy;
                    u8 *pLine = pRawBuffer + (py * nPitchBytes);

                    for (unsigned rx = 0; rx < blockW; rx++)
                    {
                        unsigned px = nPosX0 + bx + rx;
                        float cx = x1 + (float)(bx + rx) * dx;
                        u32 color = IterationToColor(ComputeMandelPixel(cx, cy, maxIter), maxIter, nBytesPerPixel);

                        if (nBytesPerPixel == 4)
                            *(u32 *)(pLine + px * 4) = color;
                        else
                            *(u16 *)(pLine + px * 2) = (u16)color;
                    }
                    CleanDataCacheRange((uintptr_t)(pLine + (nPosX0 + bx) * nBytesPerPixel), blockW * nBytesPerPixel);
                }
            }
        }
    }
}