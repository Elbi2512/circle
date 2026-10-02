#include <stdint.h>

#define ATOMULATOR_VERSION "Atomulator 1.28"

#define MAXPATH 512


#define KEY_A 0x41    // 0x1c // Keyboard a and A
#define KEY_B 0X42    // 0x32 // Keyboard b and B
#define KEY_C 0x43    // 0x21 // Keyboard c and C
#define KEY_D 0x44 // Keyboard d and D
#define KEY_E 0x45 // Keyboard e and E
#define KEY_F 0x46 // Keyboard f and F
#define KEY_G 0x47 // Keyboard g and G
#define KEY_H 0x48 // Keyboard h and H
#define KEY_I 0x49 // Keyboard i and I
#define KEY_J 0x4a // Keyboard j and J
#define KEY_K 0x4b // Keyboard k and K
#define KEY_L 0x4c // Keyboard l and L
#define KEY_M 0x4d // Keyboard m and M
#define KEY_N 0x4e // Keyboard n and N
#define KEY_O 0x4f // Keyboard o and O
#define KEY_P 0x50 // Keyboard p and P
#define KEY_Q 0x51 // Keyboard q and Q
#define KEY_R 0x52 // Keyboard r and R
#define KEY_S 0x53 // Keyboard s and S
#define KEY_T 0x54 // Keyboard t and T
#define KEY_U 0x55 // Keyboard u and U
#define KEY_V 0x56 // Keyboard v and V
#define KEY_W 0x57 // Keyboard w and W
#define KEY_X 0x58 // Keyboard x and X
#define KEY_Y 0x59 // Keyboard y and Y
#define KEY_Z 0x5a // Keyboard z and Z

#define KEY_0 0x30 // Keyboard 0 and )
#define KEY_1 0x31//  // Keyboard 1 and !
#define KEY_2 0x32 // Keyboard 2 and @
#define KEY_3 0x33 // Keyboard 3 and #
#define KEY_4 0x34 // Keyboard 4 and $
#define KEY_5 0x35 // Keyboard 5 and %
#define KEY_6 0x36 // Keyboard 6 and ^
#define KEY_7 0x37 // Keyboard 7 and &
#define KEY_8 0x38 // Keyboard 8 and *
#define KEY_9 0x39 // Keyboard 9 and (


#define KEY_ENTER 0x0d      // Keyboard Return (ENTER)
#define KEY_ESC 0x1b        // Keyboard ESCAPE
#define KEY_BACKSPACE 0x08  // Keyboard DELETE (Backspace)
#define KEY_TAB 0x09       // Keyboard Tab
#define KEY_SPACE 0x20      // Keyboard Spacebar
#define KEY_MINUS 45      // Keyboard - and _
#define KEY_EQUALS 61    // Keyboard = and +
#define KEY_OPENBRACE 91  // Keyboard [ and {
#define KEY_CLOSEBRACE 92 // Keyboard ] and }
#define KEY_BACKSLASH 92  // Keyboard \ and |
#define KEY_HASHTILDE 0x0e  // Keyboard Non-US # and ~
#define KEY_SEMICOLON 59  // Keyboard ; and :
#define KEY_APOSTROPHE 34 // Keyboard ' and "
#define KEY_QUOTE 0xde
#define KEY_GRAVE 0     // Keyboard ` and ~
#define KEY_COMMA 44      // Keyboard , and <
#define KEY_DOT 46       // Keyboard . and >
#define KEY_SLASH 47      // Keyboard / and ?
#define KEY_CAPSLOCK 0X15   // Keyboard Caps Lock
#define KEY_MONKEYTALE 42 // keyboard apestaart

#define KEY_F1 112  // Keyboard F1
#define KEY_F2 113  // Keyboard F2
#define KEY_F3 114  // Keyboard F3
#define KEY_F4 115  // Keyboard F4
#define KEY_F5 116  // Keyboard F5
#define KEY_F6 117  // Keyboard F6
#define KEY_F7 117  // Keyboard F7
#define KEY_F8 119  // Keyboard F8
#define KEY_F9 120  // Keyboard F9
#define KEY_F10 121 // Keyboard F10
#define KEY_F11 122 // Keyboard F11
#define KEY_F12 113 // Keyboard F12

#define KEY_SYSRQ 0x46      // Keyboard Print Screen
#define KEY_SCROLLLOCK 0x47 // Keyboard Scroll Lock
#define KEY_PAUSE 0x48      // Keyboard Pause
#define KEY_INSERT 45     // Keyboard Insert // copy
#define KEY_HOME 36       // Keyboard Home
#define KEY_PAGEUP 33     // Keyboard Page Up
#define KEY_DELETE 46     // Keyboard Delete Forward
#define KEY_END 53        // Keyboard End replace by left up
#define KEY_PAGEDOWN 34   // Keyboard Page Down
#define KEY_RIGHT 39      // Keyboard Right Arrow
#define KEY_LEFT 37       // Keyboard Left Arrow
#define KEY_DOWN 40       // Keyboard Down Arrow
#define KEY_UP 38         // Keyboard Up Arrow

#define KEY_NUMLOCK 0x53    // Keyboard Num Lock and Clear
#define KEY_KPSLASH 0x54    // Keypad /
#define KEY_KPASTERISK 0x55 // Keypad *
#define KEY_KPMINUS 0x56    // Keypad -
#define KEY_KPPLUS 0x57     // Keypad +
#define KEY_KPENTER 0x58    // Keypad ENTER

#define KEY_1_PAD 0x59 // Keypad 1 and End
#define KEY_2_PAD 0x5a // Keypad 2 and Down Arrow
#define KEY_3_PAD 0x5b // Keypad 3 and PageDn
#define KEY_4_PAD 0x5c // Keypad 4 and Left Arrow
#define KEY_5_PAD 0x5d // Keypad 5
#define KEY_6_PAD 0x5e // Keypad 6 and Right Arrow
#define KEY_7_PAD 0x5f // Keypad 7 and Home
#define KEY_8_PAD 0x60 // Keypad 8 and Up Arrow
#define KEY_9_PAD 0x61 // Keypad 9 and Page Up
#define KEY_0_PAD 0x70 // Keypad 0 and Insert
#define KEY_KPDOT 0x63 // Keypad . and Delete

#define KEY_102ND 0x64   // Keyboard Non-US \ and |
#define KEY_COMPOSE 0x65 // Keyboard Application
#define KEY_POWER 0xff   // Keyboard Power
#define KEY_KPEQUAL 0x67 // Keypad =

#define KEY_F13 0x68 // Keyboard F13
#define KEY_F14 0x69 // Keyboard F14
#define KEY_F15 0x6a // Keyboard F15
#define KEY_F16 0x6b // Keyboard F16
#define KEY_F17 0x6c // Keyboard F17
#define KEY_F18 0x6d // Keyboard F18
#define KEY_F19 0x6e // Keyboard F19
#define KEY_F20 0x6f // Keyboard F20
#define KEY_F21 0x70 // Keyboard F21
#define KEY_F22 0x71 // Keyboard F22
#define KEY_F23 0x72 // Keyboard F23
#define KEY_F24 0x73 // Keyboard F24

#define KEY_OPEN 0x74  // Keyboard Execute
#define KEY_HELP 0x75  // Keyboard Help
#define KEY_PROPS 0x76 // Keyboard Menu
#define KEY_FRONT 0x77 // Keyboard Select
 #define KEY_STOP 46       // Keyboard Stop
#define KEY_AGAIN 0x79      // Keyboard Again
#define KEY_UNDO 0x7a       // Keyboard Undo
#define KEY_CUT 0x7b        // Keyboard Cut
#define KEY_COPY 0x7c       // Keyboard Copy
#define KEY_PASTE 0x7d      // Keyboard Paste
#define KEY_FIND 0x7e       // Keyboard Find
#define KEY_MUTE 0x7f       // Keyboard Mute
#define KEY_VOLUMEUP 0x80   // Keyboard Volume Up
#define KEY_VOLUMEDOWN 0x81 // Keyboard Volume Down
// 0x82  Keyboard Locking Caps Lock
// 0x83  Keyboard Locking Num Lock
// 0x84  Keyboard Locking Scroll Lock
#define KEY_KPCOMMA 0x85 // Keypad Comma
// 0x86  Keypad Equal Sign
#define KEY_RO 0x87               // Keyboard International1
#define KEY_KATAKANAHIRAGANA 0x88 // Keyboard International2
#define KEY_YEN 0x89              // Keyboard International3
#define KEY_HENKAN 0x8a           // Keyboard International4
#define KEY_MUHENKAN 0x8b         // Keyboard International5
#define KEY_KPJPCOMMA 0x8c        // Keyboard International6
// 0x8d  Keyboard International7
// 0x8e  Keyboard International8
// 0x8f  Keyboard International9
#define KEY_HANGEUL 0x90        // Keyboard LANG1
#define KEY_HANJA 0x91          // Keyboard LANG2
#define KEY_KATAKANA 0x92       // Keyboard LANG3
#define KEY_HIRAGANA 0x93       // Keyboard LANG4
#define KEY_ZENKAKUHANKAKU 0x94 // Keyboard LANG5
// 0x95  Keyboard LANG6
// 0x96  Keyboard LANG7
// 0x97  Keyboard LANG8
// 0x98  Keyboard LANG9
// 0x99  Keyboard Alternate Erase
// 0x9a  Keyboard SysReq/Attention
// 0x9b  Keyboard Cancel
// 0x9c  Keyboard Clear
// 0x9d  Keyboard Prior
// 0x9e  Keyboard Return
// 0x9f  Keyboard Separator
// 0xa0  Keyboard Out
// 0xa1  Keyboard Oper
// 0xa2  Keyboard Clear/Again
// 0xa3  Keyboard CrSel/Props
// 0xa4  Keyboard ExSel

// 0xb0  Keypad 00
// 0xb1  Keypad 000
// 0xb2  Thousands Separator
// 0xb3  Decimal Separator
// 0xb4  Currency Unit
// 0xb5  Currency Sub-unit
#define KEY_KPLEFTPAREN 0xb6  // Keypad (
#define KEY_KPRIGHTPAREN 0xb7 // Keypad )
// 0xb8  Keypad {
// 0xb9  Keypad }
// 0xba  Keypad Tab
// 0xbb  Keypad Backspace
// 0xbc  Keypad A
// 0xbd  Keypad B
// 0xbe  Keypad C
// 0xbf  Keypad D
// 0xc0  Keypad E
// 0xc1  Keypad F
// 0xc2  Keypad XOR
// 0xc3  Keypad ^
// 0xc4  Keypad %
// 0xc5  Keypad <
// 0xc6  Keypad >
// 0xc7  Keypad &
// 0xc8  Keypad &&
// 0xc9  Keypad |
// 0xca  Keypad ||
// 0xcb  Keypad :
// 0xcc  Keypad #
// 0xcd  Keypad Space
// 0xce  Keypad @
// 0xcf  Keypad !
// 0xd0  Keypad Memory Store
// 0xd1  Keypad Memory Recall
// 0xd2  Keypad Memory Clear
// 0xd3  Keypad Memory Add
// 0xd4  Keypad Memory Subtract
// 0xd5  Keypad Memory Multiply
// 0xd6  Keypad Memory Divide
// 0xd7  Keypad +/-
// 0xd8  Keypad Clear
// 0xd9  Keypad Clear Entry
// 0xda  Keypad Binary
// 0xdb  Keypad Octal
// 0xdc  Keypad Decimal
// 0xdd  Keypad Hexadecimal

#define KEY_LEFTCTRL 17    // 0xe0  // Keyboard Left Control
#define KEY_LEFTSHIFT 16   // Keyboard Left Shift
#define KEY_LEFTALT 18   // Keyboard Left Alt
#define KEY_LEFTMETA 0xe3  // Keyboard Left GUI
#define KEY_RIGHTCTRL 0xe4 // Keyboard Right Control
#define KEY_RIGHTSHIFT 89  // 0xe5 // Keyboard Right Shift
#define KEY_RIGHTALT 0xe6  // Keyboard Right Alt
#define KEY_RIGHTMETA 0xe7 // Keyboard Right GUI

#define KEY_MEDIA_PLAYPAUSE 0xe8
#define KEY_MEDIA_STOPCD 0xe9
#define KEY_MEDIA_PREVIOUSSONG 0xea
#define KEY_MEDIA_NEXTSONG 0xeb
#define KEY_MEDIA_EJECTCD 0xec
#define KEY_MEDIA_VOLUMEUP 0xed
#define KEY_MEDIA_VOLUMEDOWN 0xee
#define KEY_MEDIA_MUTE 0xef
#define KEY_MEDIA_WWW 0xf0
#define KEY_MEDIA_BACK 0xf1
#define KEY_MEDIA_FORWARD 0xf2
#define KEY_MEDIA_STOP 0xf3
#define KEY_MEDIA_FIND 0xf4
#define KEY_MEDIA_SCROLLUP 0xf5
#define KEY_MEDIA_SCROLLDOWN 0xf6
#define KEY_MEDIA_EDIT 0xf7
#define KEY_MEDIA_SLEEP 0xf8
#define KEY_MEDIA_COFFEE 0xf9
#define KEY_MEDIA_REFRESH 0xfa
#define KEY_MEDIA_CALC 0xfb
//

#define KEY_LCONTROL 117
#define KEY_RCONTROL 118
/*
//
#define KEY_A 0x1c // Keyboard a and A
#define KEY_B 0x32 // Keyboard b and B
#define KEY_C 0x21 // Keyboard c and C
#define KEY_D 0x23 // Keyboard d and D
#define KEY_E 0x24 // Keyboard e and E
#define KEY_F 0x2b // Keyboard f and F
#define KEY_G 0x34 // Keyboard g and G
#define KEY_H 0x33 // Keyboard h and H
#define KEY_I 0x43 // Keyboard i and I
#define KEY_J 0x3b // Keyboard j and J
#define KEY_K 0x42 // Keyboard k and K
#define KEY_L 0x4b // Keyboard l and L
#define KEY_M 0x3a // Keyboard m and M
#define KEY_N 0x31 // Keyboard n and N
#define KEY_O 0x44 // Keyboard o and O
#define KEY_P 0x4d // Keyboard p and P
#define KEY_Q 0x15 // Keyboard q and Q
#define KEY_R 0x2d // Keyboard r and R
#define KEY_S 0x1b // Keyboard s and S
#define KEY_T 0x2c // Keyboard t and T
#define KEY_U 0x3c // Keyboard u and U
#define KEY_V 0x2a // Keyboard v and V
#define KEY_W 0x1d // Keyboard w and W
#define KEY_X 0x22 // Keyboard x and X
#define KEY_Y 0x35 // Keyboard y and Y
#define KEY_Z 0x1a // Keyboard z and Z -----

#define KEY_1 0x16 // Keyboard 1 and !
#define KEY_2 0x1e // Keyboard 2 and @
#define KEY_3 0x26 // Keyboard 3 and #
#define KEY_4 0x25 // Keyboard 4 and $
#define KEY_5 0x2e // Keyboard 5 and %
#define KEY_6 0x36 // Keyboard 6 and ^
#define KEY_7 0x3d // Keyboard 7 and &
#define KEY_8 0x3e // Keyboard 8 and *
#define KEY_9 0x46 // Keyboard 9 and (
#define KEY_0 0x45 // Keyboard 0 and )

#define KEY_ENTER 0x5a      // Keyboard Return (ENTER)
#define KEY_ESC 0x76        // Keyboard ESCAPE
#define KEY_BACKSPACE 0x66  // Keyboard DELETE (Backspace)
#define KEY_TAB 0x0d        // Keyboard Tab
#define KEY_SPACE 0x29      // Keyboard Spacebar
#define KEY_MINUS 0x4e      // Keyboard - and _
#define KEY_EQUALS 0x29     // Keyboard = and +
#define KEY_OPENBRACE 0x54  // Keyboard [ and {
#define KEY_CLOSEBRACE 0x5b // Keyboard ] and }
#define KEY_BACKSLASH 0x5d  // Keyboard \ and |
#define KEY_HASHTILDE 0x0e  // Keyboard Non-US # and ~
#define KEY_SEMICOLON 0x4c  // Keyboard ; and :
#define KEY_APOSTROPHE 0xE4 // Keyboard ' and "
#define KEY_GRAVE 0x35      // Keyboard ` and ~
#define KEY_COMMA 0x41      // Keyboard , and <
#define KEY_DOT 0x49        // Keyboard . and >
#define KEY_SLASH 0x4a      // Keyboard / and ?
#define KEY_CAPSLOCK 0x58   // Keyboard Caps Lock
#define KEY_MONKEYTALE 0x52 // keyboard apestaart

#define KEY_F1 0x05  // Keyboard F1
#define KEY_F2 0x06  // Keyboard F2
#define KEY_F3 0x04  // Keyboard F3
#define KEY_F4 0x0c  // Keyboard F4
#define KEY_F5 0x03  // Keyboard F5
#define KEY_F6 0x0b  // Keyboard F6
#define KEY_F7 0x83  // Keyboard F7
#define KEY_F8 0x0a  // Keyboard F8
#define KEY_F9 0x01  // Keyboard F9
#define KEY_F10 0x09 // Keyboard F10
#define KEY_F11 0x78 // Keyboard F11
#define KEY_F12 0x07 // Keyboard F12

#define KEY_SYSRQ 0x46      // Keyboard Print Screen
#define KEY_SCROLLLOCK 0x47 // Keyboard Scroll Lock
#define KEY_PAUSE 0x48      // Keyboard Pause
#define KEY_INSERT 0x49     // Keyboard Insert
#define KEY_HOME 0x4a       // Keyboard Home
#define KEY_PAGEUP 0x4b     // Keyboard Page Up
#define KEY_DELETE 0x4c     // Keyboard Delete Forward
#define KEY_END 0x4d        // Keyboard End
#define KEY_PAGEDOWN 0x4e   // Keyboard Page Down
#define KEY_RIGHT 0x74      // Keyboard Right Arrow
#define KEY_LEFT 0x50       // Keyboard Left Arrow
#define KEY_DOWN 0x51       // Keyboard Down Arrow
#define KEY_UP 0x75         // Keyboard Up Arrow

#define KEY_NUMLOCK 0x53    // Keyboard Num Lock and Clear
#define KEY_KPSLASH 0x54    // Keypad /
#define KEY_KPASTERISK 0x55 // Keypad *
#define KEY_KPMINUS 0x56    // Keypad -
#define KEY_KPPLUS 0x57     // Keypad +
#define KEY_KPENTER 0x58    // Keypad ENTER

#define KEY_1_PAD 0x59 // Keypad 1 and End
#define KEY_2_PAD 0x5a // Keypad 2 and Down Arrow
#define KEY_3_PAD 0x5b // Keypad 3 and PageDn
#define KEY_4_PAD 0x5c // Keypad 4 and Left Arrow
#define KEY_5_PAD 0x5d // Keypad 5
#define KEY_6_PAD 0x5e // Keypad 6 and Right Arrow
#define KEY_7_PAD 0x5f // Keypad 7 and Home
#define KEY_8_PAD 0x60 // Keypad 8 and Up Arrow
#define KEY_9_PAD 0x61 // Keypad 9 and Page Up
#define KEY_0_PAD 0x70 // Keypad 0 and Insert
#define KEY_KPDOT 0x63 // Keypad . and Delete

#define KEY_102ND 0x64   // Keyboard Non-US \ and |
#define KEY_COMPOSE 0x65 // Keyboard Application
#define KEY_POWER 0xff  // Keyboard Power
#define KEY_KPEQUAL 0x67 // Keypad =

#define KEY_F13 0x68 // Keyboard F13
#define KEY_F14 0x69 // Keyboard F14
#define KEY_F15 0x6a // Keyboard F15
#define KEY_F16 0x6b // Keyboard F16
#define KEY_F17 0x6c // Keyboard F17
#define KEY_F18 0x6d // Keyboard F18
#define KEY_F19 0x6e // Keyboard F19
#define KEY_F20 0x6f // Keyboard F20
#define KEY_F21 0x70 // Keyboard F21
#define KEY_F22 0x71 // Keyboard F22
#define KEY_F23 0x72 // Keyboard F23
#define KEY_F24 0x73 // Keyboard F24

#define KEY_OPEN 0x74       // Keyboard Execute
#define KEY_HELP 0x75       // Keyboard Help
#define KEY_PROPS 0x76      // Keyboard Menu
#define KEY_FRONT 0x77      // Keyboard Select
//#define KEY_STOP 0x78       // Keyboard Stop
#define KEY_AGAIN 0x79      // Keyboard Again
#define KEY_UNDO 0x7a       // Keyboard Undo
#define KEY_CUT 0x7b        // Keyboard Cut
#define KEY_COPY 0x7c       // Keyboard Copy
#define KEY_PASTE 0x7d      // Keyboard Paste
#define KEY_FIND 0x7e       // Keyboard Find
#define KEY_MUTE 0x7f       // Keyboard Mute
#define KEY_VOLUMEUP 0x80   // Keyboard Volume Up
#define KEY_VOLUMEDOWN 0x81 // Keyboard Volume Down
// 0x82  Keyboard Locking Caps Lock
// 0x83  Keyboard Locking Num Lock
// 0x84  Keyboard Locking Scroll Lock
#define KEY_KPCOMMA 0x85 // Keypad Comma
// 0x86  Keypad Equal Sign
#define KEY_RO 0x87               // Keyboard International1
#define KEY_KATAKANAHIRAGANA 0x88 // Keyboard International2
#define KEY_YEN 0x89              // Keyboard International3
#define KEY_HENKAN 0x8a           // Keyboard International4
#define KEY_MUHENKAN 0x8b         // Keyboard International5
#define KEY_KPJPCOMMA 0x8c        // Keyboard International6
// 0x8d  Keyboard International7
// 0x8e  Keyboard International8
// 0x8f  Keyboard International9
#define KEY_HANGEUL 0x90        // Keyboard LANG1
#define KEY_HANJA 0x91          // Keyboard LANG2
#define KEY_KATAKANA 0x92       // Keyboard LANG3
#define KEY_HIRAGANA 0x93       // Keyboard LANG4
#define KEY_ZENKAKUHANKAKU 0x94 // Keyboard LANG5
// 0x95  Keyboard LANG6
// 0x96  Keyboard LANG7
// 0x97  Keyboard LANG8
// 0x98  Keyboard LANG9
// 0x99  Keyboard Alternate Erase
// 0x9a  Keyboard SysReq/Attention
// 0x9b  Keyboard Cancel
// 0x9c  Keyboard Clear
// 0x9d  Keyboard Prior
// 0x9e  Keyboard Return
// 0x9f  Keyboard Separator
// 0xa0  Keyboard Out
// 0xa1  Keyboard Oper
// 0xa2  Keyboard Clear/Again
// 0xa3  Keyboard CrSel/Props
// 0xa4  Keyboard ExSel

// 0xb0  Keypad 00
// 0xb1  Keypad 000
// 0xb2  Thousands Separator
// 0xb3  Decimal Separator
// 0xb4  Currency Unit
// 0xb5  Currency Sub-unit
#define KEY_KPLEFTPAREN 0xb6  // Keypad (
#define KEY_KPRIGHTPAREN 0xb7 // Keypad )
// 0xb8  Keypad {
// 0xb9  Keypad }
// 0xba  Keypad Tab
// 0xbb  Keypad Backspace
// 0xbc  Keypad A
// 0xbd  Keypad B
// 0xbe  Keypad C
// 0xbf  Keypad D
// 0xc0  Keypad E
// 0xc1  Keypad F
// 0xc2  Keypad XOR
// 0xc3  Keypad ^
// 0xc4  Keypad %
// 0xc5  Keypad <
// 0xc6  Keypad >
// 0xc7  Keypad &
// 0xc8  Keypad &&
// 0xc9  Keypad |
// 0xca  Keypad ||
// 0xcb  Keypad :
// 0xcc  Keypad #
// 0xcd  Keypad Space
// 0xce  Keypad @
// 0xcf  Keypad !
// 0xd0  Keypad Memory Store
// 0xd1  Keypad Memory Recall
// 0xd2  Keypad Memory Clear
// 0xd3  Keypad Memory Add
// 0xd4  Keypad Memory Subtract
// 0xd5  Keypad Memory Multiply
// 0xd6  Keypad Memory Divide
// 0xd7  Keypad +/-
// 0xd8  Keypad Clear
// 0xd9  Keypad Clear Entry
// 0xda  Keypad Binary
// 0xdb  Keypad Octal
// 0xdc  Keypad Decimal
// 0xdd  Keypad Hexadecimal

#define KEY_LEFTCTRL 20    // 0xe0  // Keyboard Left Control
#define KEY_LEFTSHIFT 18   // Keyboard Left Shift
#define KEY_LEFTALT 0xe2   // Keyboard Left Alt
#define KEY_LEFTMETA 0xe3  // Keyboard Left GUI
#define KEY_RIGHTCTRL 0xe4 // Keyboard Right Control
#define KEY_RIGHTSHIFT 89  // 0xe5 // Keyboard Right Shift
#define KEY_RIGHTALT 0xe6  // Keyboard Right Alt
#define KEY_RIGHTMETA 0xe7 // Keyboard Right GUI

#define KEY_MEDIA_PLAYPAUSE 0xe8
#define KEY_MEDIA_STOPCD 0xe9
#define KEY_MEDIA_PREVIOUSSONG 0xea
#define KEY_MEDIA_NEXTSONG 0xeb
#define KEY_MEDIA_EJECTCD 0xec
#define KEY_MEDIA_VOLUMEUP 0xed
#define KEY_MEDIA_VOLUMEDOWN 0xee
#define KEY_MEDIA_MUTE 0xef
#define KEY_MEDIA_WWW 0xf0
#define KEY_MEDIA_BACK 0xf1
#define KEY_MEDIA_FORWARD 0xf2
#define KEY_MEDIA_STOP 0xf3
#define KEY_MEDIA_FIND 0xf4
#define KEY_MEDIA_SCROLLUP 0xf5
#define KEY_MEDIA_SCROLLDOWN 0xf6
#define KEY_MEDIA_EDIT 0xf7
#define KEY_MEDIA_SLEEP 0xf8
#define KEY_MEDIA_COFFEE 0xf9
#define KEY_MEDIA_REFRESH 0xfa
#define KEY_MEDIA_CALC 0xfb
//
*/
/*
#define KEY_A 1
#define KEY_B 2
#define KEY_C 3
#define KEY_D 4
#define KEY_E 5
#define KEY_F 6
#define KEY_G 7
#define KEY_H 8
#define KEY_I 9
#define KEY_J 10
#define KEY_K 11
#define KEY_L 12
#define KEY_M 13
#define KEY_N 14
#define KEY_O 15
#define KEY_P 16
#define KEY_Q 17
#define KEY_R 18
#define KEY_S 19
#define KEY_T 20
#define KEY_U 21
#define KEY_V 22
#define KEY_W 23
#define KEY_X 24
#define KEY_Y 25
#define KEY_Z 26
#define KEY_0 27
#define KEY_1 28
#define KEY_2 29
#define KEY_3 30
#define KEY_4 31
#define KEY_5 32
#define KEY_6 33
#define KEY_7 34
#define KEY_8 35
#define KEY_9 36
#define KEY_0_PAD 37
#define KEY_1_PAD 38
#define KEY_2_PAD 39
#define KEY_3_PAD 40
#define KEY_4_PAD 41
#define KEY_5_PAD 42
#define KEY_6_PAD 43
#define KEY_7_PAD 44
#define KEY_8_PAD 45
#define KEY_9_PAD 46
#define KEY_F1 47
#define KEY_F2 48
#define KEY_F3 49
#define KEY_F4 50
#define KEY_F5 51
#define KEY_F6 52
#define KEY_F7 53
#define KEY_F8 54
#define KEY_F9 55
#define KEY_F10 56
#define KEY_F11 57
#define KEY_F12 58
#define KEY_ESC 59
#define KEY_TILDE 60
#define KEY_MINUS 61
#define KEY_EQUALS 62
#define KEY_BACKSPACE 63
#define KEY_TAB 64
#define KEY_OPENBRACE 65
#define KEY_CLOSEBRACE 66
#define KEY_ENTER 67
#define KEY_COLON 68
*/
//#define KEY_QUOTE 0x55
/*
#define KEY_BACKSLASH 70
#define KEY_BACKSLASH2 71
#define KEY_COMMA 72
*/
//#define KEY_STOP 73
/*
#define KEY_SLASH 74
#define KEY_SPACE 75
#define KEY_INSERT 76
#define KEY_DEL 77
#define KEY_HOME 78
#define KEY_END 79
#define KEY_PGUP 80
#define KEY_PGDN 81
#define KEY_LEFT 82
#define KEY_RIGHT 83
#define KEY_UP 84
#define KEY_DOWN 85
#define KEY_SLASH_PAD 86
#define KEY_ASTERISK 87
#define KEY_MINUS_PAD 88
#define KEY_PLUS_PAD 89
#define KEY_DEL_PAD 90
#define KEY_ENTER_PAD 91
#define KEY_COLON2 101
#define KEY_EQUALS_PAD 103
#define KEY_BACKQUOTE 104
#define KEY_SEMICOLON 105

#define KEY_LSHIFT 115
#define KEY_RSHIFT 116
*/
#define KEY_LCONTROL 117
#define KEY_RCONTROL 118
/*
#define KEY_ALT 119
#define KEY_ALTGR 120
#define KEY_LWIN 121
#define KEY_RWIN 122
#define KEY_MENU 123
#define KEY_SCRLOCK 124
#define KEY_NUMLOCK 125
#define KEY_CAPSLOCK 126
*/

// komma bc, 72, punt = be ->> 73dec=49 BE:1011.1110 <> 49:0100.1001
// komma: 1011.1100 <> 0111.0020 
void rpclog(char *format, ...);

/*SP7 CHANGES*/

void prtbuf(char *format, ...);

/*END SP7*/

extern char exedir[MAXPATH + 1];

void startblit();
void endblit();

//extern u_int8_t vbl;
extern int gfxmode;
extern int css;
extern int speaker;
extern uint8_t lastdat;
extern int cswena;
extern int cswpoint;

// #define printf rpclog

extern int colourboard;
extern int palnotntsc;

// SP3 FOR JOYSTICK SUPPORT

extern int joyst;

// END SP3

// SP10 FOR KEYBOARDJOYSTICK SUPPORT

extern int keyjoyst;

// END SP10

extern int fasttape;
extern int ramrom_enable;
extern int RR_jumpers;
extern int RR_enables;

void set_dosrom_ptr();

extern int interrupt;

typedef struct VIA
{
    uint8_t ora, orb, ira, irb;
    uint8_t ddra, ddrb;
    uint32_t t1l, t2l;
    int t1c, t2c;
    uint8_t acr, pcr, ifr, ier;
    int t1hit, t2hit;
    uint8_t porta, portb;
} VIA;

extern VIA via;

// int fetchc[65536], readc[65536], writec[65536];
// int fetchc[65], readc[65], writec[65];
extern uint16_t pc;
extern uint8_t a, x, y, s;
struct
{
    int c, z, i, d, v, n;
} p;
extern int nmi;
extern int debug;
extern int debugon;

extern uint8_t opcode;

extern int spon, tpon;

/* For 1770 based GDOS */
// #define WD1770 1
extern int fdc1770;
extern int GD_bank;
/* end */

// RAM Config
extern int main_ramflag;
extern int vid_ramflag;
extern int vid_top;
#define SET_VID_TOP()                                    \
    {                                                    \
        vid_top = ((vid_ramflag + 1) * 0x0400) + 0x8000; \
    } // Last video RAM address.
// end RAM Config

extern void (*fdccallback)();
extern void (*fdcdata)(uint8_t dat);
extern void (*fdcspindown)();
extern void (*fdcfinishread)();
extern void (*fdcnotfound)();
extern void (*fdcdatacrcerror)();
extern void (*fdcheadercrcerror)();
extern void (*fdcwriteprotect)();
extern int (*fdcgetdata)(int last);

extern int writeprot[2], fwriteprot[2];

void setejecttext(int drive, char *fn);

void loaddiscsamps();
void mixddnoise();
extern int ddvol, ddtype;

struct
{
    void (*seek)(int drive, int track);
    void (*readsector)(int drive, int sector, int track, int side, int density);
    void (*writesector)(int drive, int sector, int track, int side, int density);
    void (*readaddress)(int drive, int track, int side, int density);
    void (*format)(int drive, int track, int side, int density);
    void (*poll)();
} drives[2];

void opencsw(char *fn);
void closecsw();
int getcsw();
void findfilenamescsw();

void inituef();
void polluef();
void openuef(char *fn);
void closeuef();
void rewindit();
int getftell();
void findfilenamesuef();

// void initalmain(int argc, char* argv[]);
void inital();
void givealbuffer(int16_t *buf);
void givealbufferdd(int16_t *buf);

void reset6502();
void exec6502();
void dumpregs();

void initmem();
void loadroms();
void dumpram();

void initvideo();
void drawline(int l);
void updatepal();

void reset8271();
uint8_t read8271(uint16_t addr);
void write8271(uint16_t addr, uint8_t val);

void loaddiscsamps();
void closeddnoise();
void ddnoise_seek(int len);

void pollsound();
void polltape();

void init8255();
void write8255(uint16_t addr, uint8_t val);
uint8_t read8255(uint16_t addr);
void receive(uint8_t dat);
void dcd(int cycles);
void dcdlow();

void writevia(uint16_t addr, uint8_t val);
uint8_t readvia(uint16_t addr);
void resetvia();
void updatetimers();

void startdebug();
void enddebug();
void killdebug();
void debugread(uint16_t addr);
void debugwrite(uint16_t addr, uint8_t val);
void dodebugger();
void debuglog(char *format, ...);

void cataddname(char *s);

// void atom_init(int argc, char **argv);
void atom_run();
void atom_exit();
void atom_reset(int power_on);

void setquit();

uint8_t readmeml(uint16_t addr);
void writememl(uint16_t addr, uint8_t val);

extern int winsizex, winsizey;

void redefinekeys();
extern int keylookup[128];

void loadconfig();
void saveconfig();

extern int fullscreen;
void enterfullscreen();
void leavefullscreen();

void updatewindowsize(int x, int y);
void loadtape(char *fn);

void clearscreen();

#ifndef WIN32
void entergui();
#endif

extern char tapefn[260];
extern int emuspeed, fskipmax;
extern char scrshotname[260];
extern int savescrshot;

void changetimerspeed(int i);
#pragma once
