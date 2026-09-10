//
// mandelbrot.h
//
#ifndef _mandelbrot_h
#define _mandelbrot_h

#include <circle/types.h>
#include <circle/screen.h>
#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/bcmframebuffer.h>

#ifndef DEPTH
#define DEPTH 32
#endif

#define MANDELBROT_MIN_X -2.0f
#define MANDELBROT_MAX_X  0.5f
#define MANDELBROT_MIN_Y -1.0f
#define MANDELBROT_MAX_Y  1.0f

#define MAX_ITERATION 1024

#define GRID_W 200
#define GRID_H 200

struct Point3D {
    float x, y, z;
};

struct Point2D {
    int x, y;
    boolean valid;
};

class CMandelbrotCalculator : public CMultiCoreSupport
{
public:
    float GetMinX(void) const { return m_fX1; }
    float GetMaxX(void) const { return m_fX2; }
    float GetMinY(void) const { return m_fY1; }
    float GetMaxY(void) const { return m_fY2; }
    boolean Is3DMode(void) const { return m_b3DMode; }

    CMandelbrotCalculator(CScreenDevice *pScreen, CMemorySystem *pMemorySystem);
    ~CMandelbrotCalculator(void);

    boolean Initialize(void);

    void CalculateAll(float x1, float x2, float y1, float y2);
    void ScrollY(int nLines);
    void ScrollX(int nPixels);

    void Toggle3DMode(void);

    void StepCore0(void);
    void UpdateMousePosition(unsigned nMouseX, unsigned nMouseY);
    void Run(unsigned nCore) override;
    void Shutdown(void);

private:
    void CalculateQuad(float x1, float x2, float yTop, float yBottom,
                       unsigned nPosX0, unsigned nWidth,
                       unsigned nPosY0, unsigned nHeight);

    void CalculateGrid3D(unsigned nCore);
    void RenderPerspective3D(void);
    float ComputeSmoothIteration(float cr, float ci);
    u32 GetHeightColor(float h);
    void Draw3DLine(int x0, int y0, int x1, int y1, u32 color, u32 *pFB);

private:
    CScreenDevice *m_pScreen;
    CBcmFrameBuffer *m_pFrameBuffer;

    float m_fX1;
    float m_fX2;
    float m_fY1;
    float m_fY2;

    volatile unsigned m_nFrameCounter[4];
    volatile unsigned m_nCurrentFrame;
    volatile boolean m_bShutdown;

    volatile unsigned m_nMouseX;
    volatile unsigned m_nMouseY;
    volatile unsigned m_nMaxIterations;

    volatile boolean m_b3DMode;
    float m_fPitch;
    float m_fYaw;
    float m_fCamDist;
    float m_fFocalLength;
    float m_fHeightScale;

    Point3D *m_pGrid;
    Point2D *m_pScreenPoints;
};

#endif