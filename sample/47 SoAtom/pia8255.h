#pragma once

#include <stdint.h>
#include <string.h>
#include <circle/types.h>

class CPia8255
{
public:
  CPia8255();
  void Reset();
  void Initialize() { Reset(); }
  void PollTape() {}

  void Write(uint16_t addr, uint8_t val);
  uint8_t Read(uint16_t addr);

  void KeyDown(uint8_t scancode);
  void KeyUp(uint8_t scancode);
  void ClearKeyboard();

  void SetShift(bool bShift) { m_bShift = bShift; }
  void SetCtrl(bool bCtrl) { m_bCtrl = bCtrl; }
  void SetRept(bool bRept) { m_bRept = bRept; }

  uint8_t GetGfxMode() const { return (m_PortA >> 4) & 0x0F; }
  uint8_t GetCss() const { return m_nCss; }

private:
  uint8_t m_nGfxMode;
  uint8_t m_nCss;
  volatile uint8_t m_PortA;
  volatile uint8_t m_PortB;
  volatile uint8_t m_PortC;
  volatile uint8_t m_Control;

  volatile bool m_bShift;
  volatile bool m_bCtrl;
  volatile bool m_bRept;

  // 10 rijen (0..9), bits 0..5 corresponderen 1-op-1 met PB0..PB5
  volatile uint8_t m_KeyMatrix[10];
};