#include "pia8255.h"
#include <string.h>

uint8_t CPia8255::GetGfxMode() const
{
    // Bit 7 = A/G (0 = Alphanumeric/Semigraphics, 1 = Graphics)
    if (!(m_PortA & 0x80))
    {
        return 0; // Mode 0 (Tekst / Semigraphics)
    }

    // GM2, GM1, GM0 zijn de bits 6, 5 en 4 van Port A!
    uint8_t gm = (m_PortA >> 4) & 0x07;

    // VDG Hardware Modes (oneven waarden 1 t/m 15):
    // gm = 0 (%000) -> Mode 1  (64x64, 4-kleuren)   -> VDG 1
    // gm = 1 (%001) -> Mode 2a (128x64, 2-kleuren)  -> VDG 3
    // gm = 2 (%010) -> Mode 2b (128x64, 4-kleuren)  -> VDG 5
    // gm = 3 (%011) -> Mode 2  (128x96, 2-kleuren)  -> VDG 7
    // gm = 4 (%100) -> Mode 3a (128x96, 4-kleuren)  -> VDG 9
    // gm = 5 (%101) -> Mode 3b (128x192, 2-kleuren) -> VDG 11
    // gm = 6 (%110) -> Mode 3  (128x192, 4-kleuren) -> VDG 13
    // gm = 7 (%111) -> Mode 4  (256x192, 2-kleuren) -> VDG 15 (CLEAR 4)
    return (gm * 2) + 1;
}

CPia8255::CPia8255()
{
  Reset();
}

void CPia8255::Reset()
{
  m_PortA = 0;
  m_PortB = 0xFF;
  m_PortC = 0xFF;
  m_Control = 0;
  m_bShift = true;
  m_bCtrl = false;
  m_bRept = false;
  ClearKeyboard();
}

void CPia8255::ClearKeyboard()
{
  for (int i = 0; i < 10; i++)
  {
    m_KeyMatrix[i] = 0x00;
  }
}

static bool GetMatrixPosition(uint8_t ch, int &row, int &bit)
{
  switch (ch)
  {
    // --- RIJ 0 ---
    case '3':  row = 0; bit = 1; return true;
    case '-':  row = 0; bit = 2; return true;
    case 'G':  row = 0; bit = 3; return true;
    case 'Q':  row = 0; bit = 4; return true;
    case 0x1B: row = 0; bit = 5; return true; // ESC

    // --- RIJ 1 ---
    case '2':  row = 1; bit = 1; return true;
    case ',':  row = 1; bit = 2; return true;
    case 'F':  row = 1; bit = 3; return true;
    case 'P':  row = 1; bit = 4; return true;
    case 'Z':  row = 1; bit = 5; return true;

    // --- RIJ 2 ---
    // case pijltje
    // case 83: row = 2; bit = 0; return true;
    case 0x01: row = 2; bit = 0; return true;
    case '1':  row = 2; bit = 1; return true;
    case ';':  row = 2; bit = 2; return true;
    case 'E':  row = 2; bit = 3; return true;
    case 'O':  row = 2; bit = 4; return true;
    case 'Y':  row = 2; bit = 5; return true;

    // --- RIJ 3 ---
    // case pijltje
    case 0x02: row = 3; bit = 0; return true;
    case '0':  row = 3; bit = 1; return true;
    case ':':  row = 3; bit = 2; return true; // dubbele punt (met shift '*')
    case 'D':  row = 3; bit = 3; return true;
    case 'N':  row = 3; bit = 4; return true;
    case 'X':  row = 3; bit = 5; return true;

    // --- RIJ 4 ---
    case 0x03: row = 4; bit = 0; return true; // LOCK
    case 0x08: row = 4; bit = 1; return true; // DELETE / BS
    case '9':  row = 4; bit = 2; return true;
    case 'C':  row = 4; bit = 3; return true;
    case 'M':  row = 4; bit = 4; return true;
    case 'W':  row = 4; bit = 5; return true;

    // --- RIJ 5 ---
    //case 0x01: row = 5; bit = 0; return true; // UP
    case 0x09: row = 5; bit = 1; return true; // COPY / TAB
    case '8':  row = 5; bit = 2; return true;
    case 'B':  row = 5; bit = 3; return true;
    case 'L':  row = 5; bit = 4; return true;
    case 'V':  row = 5; bit = 5; return true;

    // --- RIJ 6 ---
    case ']':  row = 6; bit = 0; return true;
    case '\r': row = 6; bit = 1; return true; // RETURN
    case '7':  row = 6; bit = 2; return true;
    case 'A':  row = 6; bit = 3; return true;
    case 'K':  row = 6; bit = 4; return true;
    case 'U':  row = 6; bit = 5; return true;

    // --- RIJ 7 ---
    case '\\': row = 7; bit = 0; return true;
    case '6':  row = 7; bit = 2; return true;
    case '@':  row = 7; bit = 3; return true;
    case 'J':  row = 7; bit = 4; return true;
    case 'T':  row = 7; bit = 5; return true;

    // --- RIJ 8 ---
    case '[':  row = 8; bit = 0; return true;
    case '5':  row = 8; bit = 2; return true;
    case '/':  row = 8; bit = 3; return true;
    case 'I':  row = 8; bit = 4; return true;
    case 'S':  row = 8; bit = 5; return true;

    // --- RIJ 9 ---
    case ' ':  row = 9; bit = 0; return true; // SPACE
    case '4':  row = 9; bit = 2; return true;
    case '.':  row = 9; bit = 3; return true;
    case 'H':  row = 9; bit = 4; return true;
    case 'R':  row = 9; bit = 5; return true;

    default:
      return false;
  }
}

void CPia8255::KeyDown(uint8_t scancode)
{
  int row = -1, bit = -1;
  if (GetMatrixPosition(scancode, row, bit))
  {
    m_KeyMatrix[row] |= (1 << bit);
  }
}



void CPia8255::KeyUp(uint8_t scancode)
{
  int row = -1, bit = -1;
  if (GetMatrixPosition(scancode, row, bit))
  {
    m_KeyMatrix[row] &= ~(1 << bit);
  }
}

void CPia8255::Write(uint16_t addr, uint8_t val)
{
  switch (addr & 0x03)
  {
    case 0: // $B000 - Port A
      m_PortA = val;
      break;

    case 1: // $B001 - Port B
      m_PortB = val;
      break;

    case 2: // $B002 - Port C
      m_PortC = val;
      break;

    case 3: // $B003 - Control
      m_Control = val;
      break;
  }
}

uint8_t CPia8255::Read(uint16_t addr)
{
  switch (addr & 0x03)
  {
    case 0: // $B000 - Port A
      return m_PortA;

    case 1: // $B001 - Port B (PB0..PB5 = Matrix, PB6 = CTRL, PB7 = SHIFT)
    {
      uint8_t row = m_PortA & 0x0F;
      uint8_t val = 0xFF; // Standaard pull-up HOOG

      // Matrixlijnen PB0..PB5
      if (row < 10)
      {
        val &= ~(m_KeyMatrix[row] & 0x3F);
      }

      // PB6 = CTRL (Active LOW via R23)
      if (m_bCtrl)
        val &= ~0x40;
      else
        val |= 0x40;

      // PB7 = SHIFT (Active LOW via R24) -> DIT IS DE ECHTE SHIFT LIJN OP PORT B!
      if (m_bShift)
        val &= ~0x80;
      else
        val |= 0x80;

      return val;
    }

    case 2: // $B002 - Port C (PC6 = REPT)
    {
      uint8_t val = m_PortC | 0xFF;

      // PC6 = REPT (Active LOW via R25) -> DIT IS DE REPEAT LIJN!
      if (m_bRept)
        val &= ~0x40;
      else
        val |= 0x40; // ALTIJD HOOG HOUDEN TENZIJ ALT INGEDRUKT IS!

      return val;
    }

    case 3: // $B003
      return m_Control;
  }
  return 0xFF;
}