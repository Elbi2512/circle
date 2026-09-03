#include "pia8255.h"
#include <circle/util.h>
#include <circle/logger.h>

// Acorn Atom Toetsenbord Matrix Mapping [10 rijen x 6 kolommen]
const int CPia8255::s_aKeys[10][6] = {
    {0, '3', '-', 'G', 'Q', 0x1B}, // 0x1B = ESC
    {0, '2', ',', 'F', 'P', 'Z'},
    {0x01, '1', ';', 'E', 'O', 'Y'},  // 0x01 = UP, ';' met shift wordt '*' en '+'
    {0x02, '0', '\'', 'D', 'N', 'X'}, // 0x02 = RIGHT
    {0x03, 0x08, '9', 'C', 'M', 'W'}, // 0x03 = CAPS, 0x08 = BACKSPACE
    {0x09, 0x04, '8', 'B', 'L', 'V'}, // 0x09 = TAB/COPY, 0x04 = END
    {']', '\r', '7', 'A', 'K', 'U'},  // '\r' = ENTER
    {'\\', 0, '6', '@', 'J', 'T'},    // <-- Rij 7, Kolom 3 is '@' op de Atom matrix!
    {'[', 0, '5', '/', 'I', 'S'},
    {' ', 0, '4', '.', 'H', 'R'}};

CPia8255::CPia8255()
    : m_nKeyRow(0), m_nGfxMode(0), m_nCss(0), m_bSpeaker(false), m_bVbl(false),
      m_bTapeOn(false), m_nTapeCyc(635), m_nTapeDat(0), m_nIntone(0),
      m_nHighTone(0), m_nByteValid(0), m_nBitValid(0), m_nDataByte(0),
      m_bShift(false), m_bCtrl(false), m_bRept(false)
{
    memset(m_aKeyl, 0, sizeof(m_aKeyl));
    memset(m_aKeysPressed, 0, sizeof(m_aKeysPressed));
}

CPia8255::~CPia8255()
{
}

void CPia8255::Initialize()
{
    InitKeyboardMatrix();
    Reset();
}

void CPia8255::Reset()
{
    m_nKeyRow = 0;
    m_nGfxMode = 0;
    m_nCss = 0;
    m_bSpeaker = false;
    m_bTapeOn = false;
    m_nTapeCyc = 635;
    m_nTapeDat = 0;
    m_nIntone = 0;
    m_nHighTone = 0;
    m_bShift = false;
    m_bCtrl = false;
    m_bRept = false;

    ClearKeyboard();
}

void CPia8255::InitKeyboardMatrix()
{
    memset(m_aKeyl, 0, sizeof(m_aKeyl));

    for (int r = 0; r < 10; r++)
    {
        for (int c = 0; c < 6; c++)
        {
            int keyVal = s_aKeys[r][c];
            if (keyVal > 0 && keyVal < 128)
            {
                // Bit 7 = Actief, Bits 0..3 = Rij, Bits 4..6 = Kolom
                m_aKeyl[keyVal] = r | (c << 4) | 0x80;
            }
        }
    }
}

void CPia8255::KeyDown(uint8_t scancode)
{
    if (scancode < 128)
    {
        m_aKeysPressed[scancode] = true;
    }
}

void CPia8255::KeyUp(uint8_t scancode)
{
    if (scancode < 128)
    {
        m_aKeysPressed[scancode] = false;
    }
}

void CPia8255::ClearKeyboard()
{
    memset(m_aKeysPressed, 0, sizeof(m_aKeysPressed));
}

void CPia8255::Write(uint16_t addr, uint8_t val)
{
    switch (addr & 3)
    {
    case 0:
    {
        // Port A op de Acorn Atom:
        // Bits 0..3 = Geselecteerde toetsenbordrij (KeyRow)
        // Bits 4..7 = Grafische modus (GfxMode)
        m_nKeyRow = val & 0x0F;

        uint8_t newGfxMode = (val >> 4) & 0x0F;
        if (m_nGfxMode != newGfxMode)
        {
            m_nGfxMode = newGfxMode;
            // Schoon logbericht om overstroming van de seriële poort te voorkomen
            // CLogger::Get()->Write("PIA8255", LogNotice, "8255 GFX Mode gewijzigd naar: %d (Waarde: 0x%02X)", m_nGfxMode, val);
        }
    }
    break;

    case 1: // Port B (Op de Atom typisch een input-port, maar afhankelijk van de 8255 configuratie soms schrijfbaar)
        break;

    case 2: // Port C
        m_nCss = (val & 8) >> 2;
        m_bSpeaker = (val & 4) != 0;
        break;

    case 3: // Control Register van de 8255
        // Als bit 7 laag is, is het een BSR (Bit Set/Reset) commando voor Port C.
        // Als bit 7 hoog is, is het een Mode Definition commando.
        if (!(val & 0x80))
        {
            // BSR mode voor Port C
            switch (val & 0x0E)
            {
            case 0x04: // Speaker bit aanpassen (Bit 1 van Port C)
                m_bSpeaker = (val & 1) != 0;
                break;

            case 0x06: // CSS bit aanpassen (Bit 3 van Port C)
                m_nCss = (val & 1) ? 2 : 0;
                break;
            }
        }
        break;
    }
}

uint8_t CPia8255::Read(uint16_t addr)
{
    uint8_t temp = 0xFF;

    switch (addr & 3)
    {
    case 0: // Port A (Keyrow & GFX Mode leesbaar)
        return (m_nKeyRow & 0x0F) | ((m_nGfxMode << 4) & 0xF0);

    case 1: // Port B (Lezen van de Matrix-kolommen voor de geselecteerde Keyrow)
    {
        // 2. Matrix kolommen scannen (Bits 0..5)
        // 1. Matrix kolommen scannen (Bits 0..5)
        for (int c = 0; c < 128; c++)
        {
            if (m_aKeysPressed[c])
            {
                if ((m_aKeyl[c] & 0x80) && (m_nKeyRow == (m_aKeyl[c] & 0x0F)))
                {
                    uint8_t colBit = (m_aKeyl[c] & 0x70) >> 4;
                    temp &= ~(1 << colBit);
                }
            }
        }

        // 2. CTRL status (Active-Low op Bit 6)
        if (m_bCtrl)
        {
            temp &= ~0x40; // Laag (0) wanneer CTRL is ingedrukt
        }
        else
        {
            temp |= 0x40; // Hoog (1) wanneer CTRL niet is ingedrukt
        }

        // 3. SHIFT status (Active-Low op Bit 7)
        if (m_bShift)
        {
            temp &= ~0x80; // Laag (0) wanneer SHIFT is ingedrukt
        }
        else
        {
            temp |= 0x80; // Hoog (1) wanneer SHIFT niet is ingedrukt
        }

        return temp;
    }

    case 2: // Port C Input (VBL, Tape, Speaker, Intone status)
        if (m_bVbl)
            temp &= ~0x80; // Bit 7: Vertical Blanking
        if (!m_nCss)
            temp &= ~0x08; // Bit 3: CSS
        if (!m_bSpeaker)
            temp &= ~0x04; // Bit 2: Speaker state
        if (!m_nIntone)
            temp &= ~0x10; // Bit 4: In-tone
        if (!m_nTapeDat)
            temp &= ~0x20; // Bit 5: Tape data
        if (m_bRept)
            temp &= ~0x40; // Bit 6: REPT toets (0 = ingedrukt, 1 = niet ingedrukt)
        return temp;

    default:
        break;
    }

    return 0xFF;
}

void CPia8255::PollTape()
{
    m_nTapeCyc += 832; // Atom Tape klok
    m_nIntone ^= 0x10;

    if (m_bTapeOn)
    {
        if (m_nHighTone)
        {
            m_nHighTone--;
            m_nTapeDat = m_nHighTone & 1;
        }
        else if (m_nByteValid)
        {
            if (m_nDataByte & 1)
                m_nTapeDat = m_nBitValid & 1;
            else
                m_nTapeDat = m_nBitValid & 2;

            m_nBitValid--;
            if (!m_nBitValid)
            {
                m_nByteValid--;
                m_nDataByte >>= 1;
                if (m_nByteValid)
                {
                    m_nBitValid = 16;
                }
            }
        }
    }
}

void CPia8255::PollSound()
{
    // Audio signaal kan hier opgevangen worden voor Circle's PWM/Sound driver
}