#pragma once

#include <stdint.h>
#include <string.h>
#include <circle/types.h>

class CPia8255
{
public:
    CPia8255();
    ~CPia8255();

    void Initialize();
    void Reset();

    // I/O afhandeling voor de 6502 bus ($B000 - $B003)
    void Write(uint16_t addr, uint8_t val);
    uint8_t Read(uint16_t addr);

    // Toetsenbord interface (voor integratie met USB Keyboard)
    void KeyDown(uint8_t scancode);
    void KeyUp(uint8_t scancode);
    void ClearKeyboard();

    // Cassette / Tape & Sound polling
    void PollTape();
    void PollSound();

    // Status setters
    void SetVBL(bool bActive) { m_bVbl = bActive; }
    void SetTapeOn(bool bOn) { m_bTapeOn = bOn; }

    // Audio status
    uint8_t GetGfxMode() const { return m_nGfxMode; }
    bool GetSpeakerState() const { return m_bSpeaker; }
    // --- NIEUW: Status voor hardware SHIFT en CTRL lijnen ---
    void SetShift(bool bPressed) { m_bShift = bPressed; }
    void SetCtrl(bool bPressed) { m_bCtrl = bPressed; }
    void SetRept(bool bPressed) { m_bRept = bPressed; }

private:
    void InitKeyboardMatrix();
    // 8255 Interne Registers & Status
    uint8_t m_nKeyRow;  // Port A laagste 4 bits: geselecteerde matrix rij
    uint8_t m_nGfxMode; // Port A hoogste 4 bits: MC6847 video modus
    uint8_t m_nCss;     // Color Select Signal (MC6847)
    bool m_bSpeaker;    // 1-bit luidspreker uitgang
    bool m_bVbl;        // Vertical Blanking status

    // Cassette / Tape emulatie variabelen
    bool m_bTapeOn;
    int m_nTapeCyc;
    int m_nTapeDat;
    int m_nIntone;
    int m_nHighTone;
    int m_nByteValid;
    int m_nBitValid;
    uint16_t m_nDataByte;

    // Toetsenbord Matrix
    uint8_t m_aKeyl[128];     // Mapping tabel: scancode -> row/col/mask
    bool m_aKeysPressed[128]; // Status van ingedrukte toetsen (true/false)

    // Acorn Atom toetsenbord matrix definitie [rijen][kolommen]
    static const int s_aKeys[10][6];
    // --- NIEUW: Variabelen om status te bewaren ---
    bool m_bShift;
    bool m_bCtrl;
    bool m_bRept;
  };