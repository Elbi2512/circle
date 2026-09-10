//
// kernel.cpp
//
#include "kernel.h"
#include <assert.h>
#include <circle/startup.h>
#include <circle/logger.h>
#include <stdint.h>

static const char FromKernel[] = "kernel";

CKernel *CKernel::s_pThis = 0;

CKernel::CKernel(void)
	: m_Screen(m_Options.GetWidth(), m_Options.GetHeight()),
	  m_Timer(&m_Interrupt),
	  m_Logger(m_Options.GetLogLevel(), &m_Timer),
	  m_USBHCI(&m_Interrupt, &m_Timer, TRUE),
	  m_Mandelbrot(&m_Screen, &m_Memory),
	  m_pMouse(0),
	  m_nPendingScrollY(0),
	  m_nPendingScrollX(0),
	  m_nPosX(0),
	  m_nPosY(0),
	  m_nAnchorX(0),
	  m_nAnchorY(0),
	  m_nLastRectX0(-1),
	  m_nLastRectY0(-1),
	  m_nLastRectX1(-1),
	  m_nLastRectY1(-1),
	  m_bSelecting(FALSE),
	  m_ShutdownMode(ShutdownNone)
{
	s_pThis = this;
	m_ActLED.Blink(5);
}

CKernel::~CKernel(void)
{
	s_pThis = 0;
}

boolean CKernel::Initialize(void)
{
	boolean bOK = TRUE;

	if (bOK)
		bOK = m_Screen.Initialize();
	if (bOK)
		bOK = m_Serial.Initialize(115200);

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice(m_Options.GetLogDevice(), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}
		bOK = m_Logger.Initialize(pTarget);
	}

	if (bOK)
		bOK = m_Interrupt.Initialize();
	if (bOK)
		bOK = m_Timer.Initialize();
	if (bOK)
		bOK = m_USBHCI.Initialize();

	if (bOK)
	{
		bOK = m_Mandelbrot.Initialize();
	}

	return bOK;
}

TShutdownMode CKernel::Run(void)
{
    m_Logger.Write(FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);
    m_Logger.Write(FromKernel, LogNotice, "Starting Mandelbrot on 4 cores...");

    m_Mandelbrot.CalculateAll(MANDELBROT_MIN_X, MANDELBROT_MAX_X, MANDELBROT_MIN_Y, MANDELBROT_MAX_Y);
    m_Mandelbrot.StepCore0();

    for (unsigned nCount = 0; m_ShutdownMode == ShutdownNone; nCount++)
    {
        m_Mandelbrot.StepCore0();

        boolean bUpdated = m_USBHCI.UpdatePlugAndPlay();

        if (bUpdated && m_pMouse == 0)
        {
            m_pMouse = (CMouseDevice *)m_DeviceNameService.GetDevice("mouse1", FALSE);
            if (m_pMouse != 0)
            {
                m_pMouse->RegisterRemovedHandler(MouseRemovedHandler);

                m_Logger.Write(FromKernel, LogNotice, "USB mouse has %d buttons", m_pMouse->GetButtonCount());
                m_Logger.Write(FromKernel, LogNotice, "USB mouse has %s wheel", m_pMouse->HasWheel() ? "a" : "no");

                // 1. Koppel de event handler vóór Setup zodat geen enkel pakket gemist wordt
                m_pMouse->RegisterEventHandler(MouseEventStub);

                if (!m_pMouse->Setup(m_Screen.GetFrameBuffer()))
                {
                    m_Logger.Write(FromKernel, LogPanic, "Cannot setup mouse");
                }

                m_nPosX = m_Screen.GetWidth() / 2;
                m_nPosY = m_Screen.GetHeight() / 2;

                // 2. Directe initiële positie instellen, tonen en synchroon forceren
                m_pMouse->SetCursor(m_nPosX, m_nPosY);
                m_pMouse->ShowCursor(TRUE);
                m_pMouse->UpdateCursor();
            }
        }

        if (m_pMouse != 0)
        {
            m_pMouse->UpdateCursor();
        }

        if (m_nPendingScrollY != 0)
        {
            int nLines = m_nPendingScrollY;
            m_nPendingScrollY = 0;
            m_Mandelbrot.ScrollY(nLines);
        }

        if (m_nPendingScrollX != 0)
        {
            int nPixels = m_nPendingScrollX;
            m_nPendingScrollX = 0;
            m_Mandelbrot.ScrollX(nPixels);
        }

        m_Screen.Rotor(0, nCount);
    }

    return m_ShutdownMode;
}

void CKernel::MouseEventHandler(TMouseEvent Event, unsigned nButtons, unsigned nPosX, unsigned nPosY, int nWheelMove)
{
	// Als nPosX door Circle al op schermresolutie wordt geleverd, haal de nPosX /= 2 weg!
	// nPosX /= 2 was de veroorzaker van de scheve X-as sprong.
	m_nPosX = nPosX;
	m_nPosY = nPosY;
	m_Mandelbrot.UpdateMousePosition(nPosX, nPosY);

	switch (Event)
	{
	case MouseEventMouseWheel:
		if (nWheelMove != 0)
		{
			const int nScrollStep = 16;

#if defined(MOUSE_BUTTON_SIDE)
			boolean bThumbPressed = (nButtons & MOUSE_BUTTON_SIDE) != 0;
#elif defined(MOUSE_BUTTON_BACK)
			boolean bThumbPressed = (nButtons & MOUSE_BUTTON_BACK) != 0;
#else
			boolean bThumbPressed = (nButtons & (1 << 3)) != 0;
#endif

			if (bThumbPressed)
			{
				if (nWheelMove > 0)
					m_nPendingScrollX -= nScrollStep;
				else
					m_nPendingScrollX += nScrollStep;
			}
			else
			{
				if (nWheelMove > 0)
					m_nPendingScrollY -= nScrollStep;
				else
					m_nPendingScrollY += nScrollStep;
			}
		}
		break;

	case MouseEventMouseDown:
		if (nButtons & MOUSE_BUTTON_RIGHT)
		{
			// Rechtermuisknop: Schakel 3D landschap aan / uit!
			if (m_bSelecting)
			{
				if (m_nLastRectX0 != -1)
				{
					DrawXORRect(m_nLastRectX0, m_nLastRectY0, m_nLastRectX1, m_nLastRectY1);
					m_nLastRectX0 = -1;
				}
				m_bSelecting = FALSE;
			}

			m_Mandelbrot.Toggle3DMode();
		}
		else if (nButtons & MOUSE_BUTTON_LEFT)
		{
			// Alleen selecteren in normale 2D modus
			if (!m_Mandelbrot.Is3DMode())
			{
				m_nAnchorX = nPosX;
				m_nAnchorY = nPosY;
				m_bSelecting = TRUE;

				m_nLastRectX0 = m_nAnchorX;
				m_nLastRectY0 = m_nAnchorY;
				m_nLastRectX1 = nPosX;
				m_nLastRectY1 = nPosY;
				DrawXORRect(m_nLastRectX0, m_nLastRectY0, m_nLastRectX1, m_nLastRectY1);
			}
		}
		else if (nButtons & MOUSE_BUTTON_MIDDLE)
		{
			m_nPendingScrollX = 0;
			m_nPendingScrollY = 0;
			m_Mandelbrot.CalculateAll(MANDELBROT_MIN_X, MANDELBROT_MAX_X, MANDELBROT_MIN_Y, MANDELBROT_MAX_Y);
		}
		break;

	case MouseEventMouseMove:
		if (m_bSelecting)
		{
			if (m_nLastRectX0 != -1)
			{
				DrawXORRect(m_nLastRectX0, m_nLastRectY0, m_nLastRectX1, m_nLastRectY1);
			}

			m_nLastRectX0 = m_nAnchorX;
			m_nLastRectY0 = m_nAnchorY;
			m_nLastRectX1 = nPosX;
			m_nLastRectY1 = nPosY;
			DrawXORRect(m_nLastRectX0, m_nLastRectY0, m_nLastRectX1, m_nLastRectY1);
		}
		break;

	case MouseEventMouseUp:
		if (m_bSelecting)
		{
			if (m_nLastRectX0 != -1)
			{
				DrawXORRect(m_nLastRectX0, m_nLastRectY0, m_nLastRectX1, m_nLastRectY1);
				m_nLastRectX0 = -1;
			}
			m_bSelecting = FALSE;

			const float nScreenWidth = (float)m_Screen.GetWidth();
			const float nScreenHeight = (float)m_Screen.GetHeight();
			const float fAspectRatio = nScreenWidth / nScreenHeight;

			float curMinX = m_Mandelbrot.GetMinX();
			float curMaxX = m_Mandelbrot.GetMaxX();
			float curMinY = m_Mandelbrot.GetMinY();
			float curMaxY = m_Mandelbrot.GetMaxY();

			const float fComplexPerPixelX = (curMaxX - curMinX) / nScreenWidth;
			const float fComplexPerPixelY = (curMaxY - curMinY) / nScreenHeight;

			int diffX = (int)nPosX - (int)m_nAnchorX;
			int diffY = (int)nPosY - (int)m_nAnchorY;
			if (diffX < 0)
				diffX = -diffX;
			if (diffY < 0)
				diffY = -diffY;

			float newMinX, newMaxX, newMinY, newMaxY;

			// Offsetinstellingen: 0 = exact op de muis/kruising centreren
			// Als je een specifieke offset wilt, pas deze dan hier aan
			const int CLICK_OFFSET_X = 0;
			const int CLICK_OFFSET_Y = 0;

			const int BOX_OFFSET_X = 0;
			const int BOX_OFFSET_Y = 0;

			if (diffX >= 4 && diffY >= 4)
			{
				// Situatie A: Box zoom
				float compStartY = curMaxY - ((float)m_nAnchorY / nScreenHeight) * (curMaxY - curMinY);
				float compEndY = curMaxY - ((float)nPosY / nScreenHeight) * (curMaxY - curMinY);

				float selMinY = (compStartY < compEndY) ? compStartY : compEndY;
				float selMaxY = (compStartY < compEndY) ? compEndY : compStartY;
				float newSpanY = selMaxY - selMinY;
				float newSpanX = newSpanY * fAspectRatio;

				float compStartX = curMinX + ((float)m_nAnchorX / nScreenWidth) * (curMaxX - curMinX);
				float compEndX = curMinX + ((float)nPosX / nScreenWidth) * (curMaxX - curMinX);

				// 1. Exacte kruising / middelpunt van de box
				float midX = (compStartX + compEndX) * 0.5f;
				float midY = (selMinY + selMaxY) * 0.5f;

				// 2. Optionele offsets toepassen
				midX += (float)BOX_OFFSET_X * fComplexPerPixelX;
				midY -= (float)BOX_OFFSET_Y * fComplexPerPixelY;

				// 3. Bouw het nieuwe venster gecentreerd op het kruispunt
				newMinX = midX - (newSpanX * 0.5f);
				newMaxX = midX + (newSpanX * 0.5f);
				newMinY = midY - (newSpanY * 0.5f);
				newMaxY = midY + (newSpanY * 0.5f);
			}
			else
			{
				// Situatie B: Enkele klik -> 2x zoom gecentreerd op het klikpunt
				int targetScreenX = (int)nPosX + CLICK_OFFSET_X;
				int targetScreenY = (int)nPosY + CLICK_OFFSET_Y;

				if (targetScreenX < 0)
					targetScreenX = 0;
				if (targetScreenX >= (int)m_Screen.GetWidth())
					targetScreenX = (int)m_Screen.GetWidth() - 1;
				if (targetScreenY < 0)
					targetScreenY = 0;
				if (targetScreenY >= (int)m_Screen.GetHeight())
					targetScreenY = (int)m_Screen.GetHeight() - 1;

				float clickComplexX = curMinX + ((float)targetScreenX / nScreenWidth) * (curMaxX - curMinX);
				float clickComplexY = curMaxY - ((float)targetScreenY / nScreenHeight) * (curMaxY - curMinY);

				float newSpanY = (curMaxY - curMinY) * 0.5f;
				float newSpanX = newSpanY * fAspectRatio;

				newMinX = clickComplexX - (newSpanX * 0.5f);
				newMaxX = clickComplexX + (newSpanX * 0.5f);
				newMinY = clickComplexY - (newSpanY * 0.5f);
				newMaxY = clickComplexY + (newSpanY * 0.5f);
			}

			// Clamping & Bounds Protection
			float spanX = newMaxX - newMinX;
			float spanY = newMaxY - newMinY;

			if (spanX > (MANDELBROT_MAX_X - MANDELBROT_MIN_X))
				spanX = MANDELBROT_MAX_X - MANDELBROT_MIN_X;
			if (spanY > (MANDELBROT_MAX_Y - MANDELBROT_MIN_Y))
				spanY = MANDELBROT_MAX_Y - MANDELBROT_MIN_Y;

			if (newMinX < MANDELBROT_MIN_X)
			{
				newMinX = MANDELBROT_MIN_X;
				newMaxX = newMinX + spanX;
			}
			else if (newMaxX > MANDELBROT_MAX_X)
			{
				newMaxX = MANDELBROT_MAX_X;
				newMinX = newMaxX - spanX;
			}

			if (newMinY < MANDELBROT_MIN_Y)
			{
				newMinY = MANDELBROT_MIN_Y;
				newMaxY = newMinY + spanY;
			}
			else if (newMaxY > MANDELBROT_MAX_Y)
			{
				newMaxY = MANDELBROT_MAX_Y;
				newMinY = newMaxY - spanY;
			}

			m_Mandelbrot.CalculateAll(newMinX, newMaxX, newMinY, newMaxY);
		}
		break;

	default:
		break;
	}
}

void CKernel::MouseEventStub(TMouseEvent Event, unsigned nButtons, unsigned nPosX, unsigned nPosY, int nWheelMove)
{
	assert(s_pThis != 0);
	s_pThis->MouseEventHandler(Event, nButtons, nPosX, nPosY, nWheelMove);
}

void CKernel::DrawLine(int x0, int y0, int x1, int y1, unsigned nColor)
{
	CBcmFrameBuffer *pFB = m_Screen.GetFrameBuffer();
	if (!pFB)
		return;

	u8 *pRaw = (u8 *)(uintptr_t)pFB->GetBuffer();
	const unsigned nPitch = pFB->GetPitch();
	const int fbWidth = (int)m_Screen.GetWidth();
	const int fbHeight = (int)m_Screen.GetHeight();

	int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
	int dy = (y1 >= y0) ? (y0 - y1) : (y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	while (1)
	{
		if (x0 >= 0 && x0 < fbWidth && y0 >= 0 && y0 < fbHeight)
		{
			u32 *pLine = (u32 *)(pRaw + (y0 * nPitch));
			pLine[x0] = nColor;
		}

		if (x0 == x1 && y0 == y1)
			break;

		int e2 = 2 * err;
		if (e2 >= dy)
		{
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx)
		{
			err += dx;
			y0 += sy;
		}
	}
}

void CKernel::MouseRemovedHandler(CDevice *pDevice, void *pContext)
{
	assert(s_pThis != 0);
	CLogger::Get()->Write(FromKernel, LogDebug, "Mouse removed");
	s_pThis->m_pMouse = 0;
	CLogger::Get()->Write(FromKernel, LogNotice, "Please attach an USB mouse!");
}

int main(void)
{
	CKernel Kernel;
	if (!Kernel.Initialize())
	{
		halt();
		return EXIT_HALT;
	}

	TShutdownMode ShutdownMode = Kernel.Run();

	switch (ShutdownMode)
	{
	case ShutdownReboot:
		reboot();
		return EXIT_REBOOT;

	case ShutdownHalt:
	default:
		halt();
		return EXIT_HALT;
	}
}

void CKernel::DrawXORRect(int x0, int y0, int x1, int y1)
{
	CBcmFrameBuffer *pFB = m_Screen.GetFrameBuffer();
	if (!pFB)
		return;
	x0 /= 2;
	x1 /= 2;
	u8 *pRaw = (u8 *)(uintptr_t)pFB->GetBuffer();
	const unsigned nPitch = pFB->GetPitch();
	const int fbWidth = (int)m_Screen.GetWidth();
	const int fbHeight = (int)m_Screen.GetHeight();

	int minX = (x0 < x1) ? x0 : x1;
	int maxX = (x0 < x1) ? x1 : x0;
	int minY = (y0 < y1) ? y0 : y1;
	int maxY = (y0 < y1) ? y1 : y0;

	if (minX < 0)
		minX = 0;
	if (maxX >= fbWidth)
		maxX = fbWidth - 1;
	if (minY < 0)
		minY = 0;
	if (maxY >= fbHeight)
		maxY = fbHeight - 1;

	const u32 xorMask = 0x00FFFFFF;

	u32 *pTopRow = (u32 *)(pRaw + (minY * nPitch));
	u32 *pBottomRow = (u32 *)(pRaw + (maxY * nPitch));
	for (int x = minX; x <= maxX; x++)
	{
		pTopRow[x] ^= xorMask;
		if (minY != maxY)
		{
			pBottomRow[x] ^= xorMask;
		}
	}

	for (int y = minY + 1; y < maxY; y++)
	{
		u32 *pRow = (u32 *)(pRaw + (y * nPitch));
		pRow[minX] ^= xorMask;
		if (minX != maxX)
		{
			pRow[maxX] ^= xorMask;
		}
	}
}