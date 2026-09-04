#include "pia8255.h"
#include <string.h>

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
  m_nGfxMode = 0;
  m_nCss = 0;
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
  case '3':
    row = 0;
    bit = 1;
    return true;
  case '-':
    row = 0;
    bit = 2;
    return true;
  case 'G':
    row = 0;
    bit = 3;
    return true;
  case 'Q':
    row = 0;
    bit = 4;
    return true;
  case 0x1B:
    row = 0;
    bit = 5;
    return true; // ESC

  // --- RIJ 1 ---
  case '2':
    row = 1;
    bit = 1;
    return true;
  case ',':
    row = 1;
    bit = 2;
    return true;
  case 'F':
    row = 1;
    bit = 3;
    return true;
  case 'P':
    row = 1;
    bit = 4;
    return true;
  case 'Z':
    row = 1;
    bit = 5;
    return true;

  // --- RIJ 2 ---
  // case pijltje
  // case 83: row = 2; bit = 0; return true;
  case 0x01:
    row = 2;
    bit = 0;
    return true;
  case '1':
    row = 2;
    bit = 1;
    return true;
  case ';':
    row = 2;
    bit = 2;
    return true;
  case 'E':
    row = 2;
    bit = 3;
    return true;
  case 'O':
    row = 2;
    bit = 4;
    return true;
  case 'Y':
    row = 2;
    bit = 5;
    return true;

  // --- RIJ 3 ---
  // case pijltje
  case 0x02:
    row = 3;
    bit = 0;
    return true;
  case '0':
    row = 3;
    bit = 1;
    return true;
  case ':':
    row = 3;
    bit = 2;
    return true; // dubbele punt (met shift '*')
  case 'D':
    row = 3;
    bit = 3;
    return true;
  case 'N':
    row = 3;
    bit = 4;
    return true;
  case 'X':
    row = 3;
    bit = 5;
    return true;

  // --- RIJ 4 ---
  case 0x03:
    row = 4;
    bit = 0;
    return true; // LOCK
  case 0x08:
    row = 4;
    bit = 1;
    return true; // DELETE / BS
  case '9':
    row = 4;
    bit = 2;
    return true;
  case 'C':
    row = 4;
    bit = 3;
    return true;
  case 'M':
    row = 4;
    bit = 4;
    return true;
  case 'W':
    row = 4;
    bit = 5;
    return true;

  // --- RIJ 5 ---
  // case 0x01: row = 5; bit = 0; return true; // UP
  case 0x09:
    row = 5;
    bit = 1;
    return true; // COPY / TAB
  case '8':
    row = 5;
    bit = 2;
    return true;
  case 'B':
    row = 5;
    bit = 3;
    return true;
  case 'L':
    row = 5;
    bit = 4;
    return true;
  case 'V':
    row = 5;
    bit = 5;
    return true;

  // --- RIJ 6 ---
  case ']':
    row = 6;
    bit = 0;
    return true;
  case '\r':
    row = 6;
    bit = 1;
    return true; // RETURN
  case '7':
    row = 6;
    bit = 2;
    return true;
  case 'A':
    row = 6;
    bit = 3;
    return true;
  case 'K':
    row = 6;
    bit = 4;
    return true;
  case 'U':
    row = 6;
    bit = 5;
    return true;

  // --- RIJ 7 ---
  case '\\':
    row = 7;
    bit = 0;
    return true;
  case '6':
    row = 7;
    bit = 2;
    return true;
  case '@':
    row = 7;
    bit = 3;
    return true;
  case 'J':
    row = 7;
    bit = 4;
    return true;
  case 'T':
    row = 7;
    bit = 5;
    return true;

  // --- RIJ 8 ---
  case '[':
    row = 8;
    bit = 0;
    return true;
  case '5':
    row = 8;
    bit = 2;
    return true;
  case '/':
    row = 8;
    bit = 3;
    return true;
  case 'I':
    row = 8;
    bit = 4;
    return true;
  case 'S':
    row = 8;
    bit = 5;
    return true;

  // --- RIJ 9 ---
  case ' ':
    row = 9;
    bit = 0;
    return true; // SPACE
  case '4':
    row = 9;
    bit = 2;
    return true;
  case '.':
    row = 9;
    bit = 3;
    return true;
  case 'H':
    row = 9;
    bit = 4;
    return true;
  case 'R':
    row = 9;
    bit = 5;
    return true;

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
 /*
    // Bit 7 = A/G (1 = Graphics, 0 = Tekst/Semigraphics)
    if (val & 0x80)
    {
      uint8_t gm = (val >> 4) & 0x07;
      static const uint8_t s_VdgModeMap[8] = {1, 3, 5, 7, 9, 13, 11, 15};
      m_nGfxMode = s_VdgModeMap[gm];
      m_nCss = (val & 0x01); // Bit 0 = CSS
    }
    else
    {
      // Bit 7 = 0.
      // Als we in graphics mode zitten: het OS schrijft 0x00..0x09 (scan) en 0x3F (bus release).
      // Al deze waarden hebben de hoge bits (GM2..GM0) op 0 of 3, maar zijn GEEN mode switch!
      // In Atom BASIC zet CLEAR 0 het register B000 op 0x00 en wist het scherm.
      // We negeren elke write met Bit 7 = 0 zolang we in grafische modus zitten,
      // BEHALVE als het VRAM weer leeggemaakt is (tekst), OF als het expliciet via Reset/CLEAR 0 gaat.
      //
      // Om 100% stabiel te zijn: een actieve grafische stand blijft graphics zolang
      // er niet expliciet een CLEAR 0 / Reset plaatsvindt!
    } */
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