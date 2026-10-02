#include <Arduino.h>
// #include <M5Core2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roms.h"
#include "atom.h"
#include "Emulator/Keyboard/PS2Kbd.h"
#include "atommc.h"
#include "romdata.h"
// #include <TFT_eSPI.h>

// extern TFT_eSPI tft; //  = TFT_eSPI();

// deze werkt en poept een bitstream uit op het scherm

// Struct voor een Huffman-boomknoop
int gemen(int n);
int gemeen(int n);
struct MinHeapNode
{
  unsigned char data;
  unsigned freq;
  struct MinHeapNode *left, *right;
};

// Struct voor een Min Heap: een verzameling van min-heap knopen
struct MinHeap
{
  unsigned size;
  unsigned capacity;
  struct MinHeapNode **array;
};

// Statische array voor nodes
#define MAX_TREE_HT 100
#define MAX_NODES 256
struct MinHeapNode nodes[MAX_NODES];
int nodeCount = 0;

struct MinHeap *StatHeap;
int nAantalbits[256];
unsigned char nChar[256];
int AantalBitsx = 0;
void freeTree(struct MinHeapNode *minHeap);
struct MinHeapNode *extractMin(struct MinHeap *minHeap);
int isSizeOne(struct MinHeap *minHeap);
void swapMinHeapNode(struct MinHeapNode **a, struct MinHeapNode **b);
void decode(struct MinHeapNode *root, char *encodedStr);
struct MinHeapNode *buildHuffmanTree(unsigned char data[], int freq[], int size);
void printCodes(struct MinHeapNode *root, int arr[], int top);
int isLeaf(struct MinHeapNode *root);

struct MinHeapNode *extractMin(struct MinHeap *minHeap);

void set_rr_ptrs();
unsigned short crc32(unsigned short crc, const unsigned char *buf, size_t len);

uint8_t *utility_ptr;
uint8_t *abasic_ptr;
uint8_t *afloat_ptr;
uint8_t *dosrom_ptr;
uint8_t *akernel_ptr;
int lines;
struct AtomFrame
{
  unsigned char b;
  unsigned char Page;
  unsigned char crcL;
  unsigned char crcH;
  unsigned char PageData[256];
};
extern struct AtomFrame s1;
// extern void SetTxt(int, int, int); // 6522 via
extern uint8_t shift, ctrl, alt;
extern void load_rom(String, uint8_t *);
extern u_int8_t scanline;
// extern u_int8_t scanline;
//  extern 	Color **backBuffer;
//  extern uint8_t fontdata[];
// void drawli(int, int, int, int);
#define TIMER1INT 0x40
#define TIMER2INT 0x20
#define PORTBINT 0x18
#define PORTAINT 0x03

#define ORB 0x00
#define ORA 0x01
#define DDRB 0x02
#define DDRA 0x03
#define T1CL 0x04
#define T1CH 0x05
#define T1LL 0x06
#define T1LH 0x07
#define T2CL 0x08
#define T2CH 0x09
#define SR 0x0a
#define ACR 0x0b
#define PCR 0x0c
#define IFR 0x0d
#define IER 0x0e
#define ORAnh 0x0f

int vid_top = 0x9c00;
int main_ramflag = 3; // 25K by default (when RAMROM *DISABLED!*).

int fullscreen = 0;
int winsizex = 512, winsizey = 384;

extern byte key[128];
int keylookup[128];
extern int debugdisaddr;
extern void debugdisassemble();
extern void InitMMC(void);

// uint8_t* ram;
int cy = 0,
    sy = 0;
int textcol[4] = {0, 1, 0, 8};
int semigrcol[8] = {1, 2, 3, 4, 5, 6, 7, 8};
int grcol[4] = {0, 1, 0, 5};
// int tapeon;
int frmcount = 0;
int fskipcount = 0;

int gfxmode;
extern uint8_t vbl; // 60Hz
int css;            // cassette sub system
int speaker;
uint8_t lastdat;
int cswena;
int cswpoint;

VIA via;

int interrupt;
uint16_t pc;
uint8_t a, x, y, s;

char scrshotname[260];
int savescrshot = 0;

uint8_t fetcheddat[32];
int keyl[128];
int keys[16][6] =
    {
        {0, KEY_3, KEY_MINUS, KEY_G, KEY_Q, KEY_ESC},
        {0, KEY_2, KEY_COMMA, KEY_F, KEY_P, KEY_Z},
        {KEY_UP, KEY_1, KEY_SEMICOLON, KEY_E, KEY_O, KEY_Y},
        {KEY_RIGHT, KEY_0, KEY_QUOTE, KEY_D, KEY_N, KEY_X},
        {KEY_CAPSLOCK, KEY_BACKSPACE, KEY_9, KEY_C, KEY_M, KEY_W},
        {KEY_TAB, KEY_END, KEY_8, KEY_B, KEY_L, KEY_V},
        {KEY_CLOSEBRACE, KEY_ENTER, KEY_7, KEY_A, KEY_K, KEY_U},
        {KEY_BACKSLASH, 0, KEY_6, KEY_EQUALS, KEY_J, KEY_T},
        {KEY_OPENBRACE, 0, KEY_5, KEY_SLASH, KEY_I, KEY_S},
        {KEY_SPACE, 0, KEY_4, KEY_STOP, KEY_H, KEY_R}};

void initmem();
// void drawlines(int);
void loadroms();
// void initvideo();
void init8255();
void atom_reset(int);
void atom_run();
void resetvia();
void reset6502();
void exec6502(int, int);
void updatetimers();
void writememl(uint16_t, uint8_t);
uint8_t readmeml(uint16_t);

int totcyc = 0;
int skipint, nmi, nmilock;
int timetolive, oldnmi;
int skipint2, oldnmi2;
uint8_t *ram, *svideo; //, *rom;

int drawscr = 0;
int ddframes = 0;
int timerspeeds[] = {5, 12, 25, 38, 50, 75, 85, 100, 150, 200, 250};
int frameskips[] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4};
int emuspeed = 4;
int fskipmax = 0;

void scrupdate()
{
  ddframes++;
  drawscr++;
}

int cycles = 0;
int output = 0;
int ins = 0;

/* RAMROM */
int RR_bankreg = 0;
int RR_enables = 1;
int RR_jumpers = 4;
int ramrom_enable = 1;

void initmem()
{
  ram = (byte *)malloc(0xA000);
  svideo = (byte *)malloc(6144);
  for (int i = 0; i < 6144; i++)
  {
    svideo[i] = 0x2a;
  }
  // Serial.printf("ram: %X\r\n", ram);

  // rom = (uint8_t *)malloc(0x6000);
  //                               ROM_MEM_SIZE +
  //                         RAM_ROM_SIZE + ROM_SIZE_GDOS2015);
  //  Serial.printf("rom: %X\r\n", rom);

  // (ROM_MEM_SIZE + RAM_ROM_SIZE + ROM_SIZE_GDOS2015);

  ram[8] = rand() & 255;
  ram[9] = rand() & 255;
  ram[10] = rand() & 255;
  ram[11] = rand() & 255;
}

void changetimerspeed(int i)
{
  // LB remove_int(scrupdate);
  // LB  install_int_ex(scrupdate, BPS_TO_TIMER(i * 6));
}

int oldf12 = 0;

void atom_run()
{
  exec6502(225, 64);
}

#define readmem(a) readmeml(a)
#define writemem(a, v) writememl(a, v)
// #define readmem(a) ((memstat[(a) >> 8] == 2) ? readmeml(a) : mem[(a) >> 8][(a)&0xFF])
/* #define writemem(a, b)             \
  if (memstat[(a) >> 8] == 0)      \
  mem[(a) >> 8][(a)&0xFF] = b;   \
  else if (memstat[(a) >> 8] == 2) \
  writememl(a, b, lines);
*/

// MH - function needs to be declared static inline to compile on OSX
static inline uint16_t getsw()
{
  uint16_t temp = readmem(pc);
  pc++;

  temp |= (readmem(pc) << 8);
  pc++;
  return temp;
}
#define getw() getsw()
// #define getw() (readmem(pc)|(readmem(pc+1)<<8)); pc+=2

void reset6502()
{
  //        atexit(dumpram);
  // reset_rom();
  pc = readmem(0xFFFC) | (readmem(0xFFFD) << 8);
  p.i = 1;
  nmi = oldnmi = nmilock = 0;
}

void dumpregs()
{
  // dumpram();
  // Serial.printf("6502 registers :\n");
  //  Serial.printf("A=%02X X=%02X Y=%02X S=01%02X PC=%04X\n", a, x, y, s, pc);
  //  Serial.printf("Status : %c%c%c%c%c%c\n", (p.n) ? 'N' : ' ', (p.v) ? 'V' : ' ', (p.d) ? 'D' : ' ', (p.i) ? 'I' : ' ', (p.z) ? 'Z' : ' ', (p.c) ? 'C' : ' ');
}

#define setzn(v) \
  p.z = !(v);    \
  p.n = (v) & 0x80

#define push(v) ram[0x100 + (s--)] = v
#define pull() ram[0x100 + (++s)]

#define polltime(c)                   \
  {                                   \
    cycles -= c;                      \
    totcyc += c;                      \
    via.t1c -= c;                     \
    if (!(via.acr & 0x20))            \
      via.t2c -= c;                   \
    if (via.t1c < -1 || via.t2c < -1) \
      updatetimers();                 \
  }

/*ADC/SBC temp variables*/
uint16_t tempw;
int tempv, hc, al, ah;
uint8_t tempb;
#define ADC(temp)                                         \
  if (!p.d)                                               \
  {                                                       \
    tempw = (a + temp + (p.c ? 1 : 0));                   \
    p.v = (!((a ^ temp) & 0x80) && ((a ^ tempw) & 0x80)); \
    a = tempw & 0xFF;                                     \
    p.c = tempw & 0x100;                                  \
    setzn(a);                                             \
  }                                                       \
  else                                                    \
  {                                                       \
    ah = 0;                                               \
    p.z = p.n = 0;                                        \
    tempb = a + temp + (p.c ? 1 : 0);                     \
    if (!tempb)                                           \
      p.z = 1;                                            \
    al = (a & 0xF) + (temp & 0xF) + (p.c ? 1 : 0);        \
    if (al > 9)                                           \
    {                                                     \
      al -= 10;                                           \
      al &= 0xF;                                          \
      ah = 1;                                             \
    }                                                     \
    ah += ((a >> 4) + (temp >> 4));                       \
    if (ah & 8)                                           \
      p.n = 1;                                            \
    p.v = (((ah << 4) ^ a) & 128) && !((a ^ temp) & 128); \
    p.c = 0;                                              \
    if (ah > 9)                                           \
    {                                                     \
      p.c = 1;                                            \
      ah -= 10;                                           \
      ah &= 0xF;                                          \
    }                                                     \
    a = (al & 0xF) | (ah << 4);                           \
  }

#define SBC(temp)                                         \
  if (!p.d)                                               \
  {                                                       \
    tempw = a - (temp + (p.c ? 0 : 1));                   \
    tempv = (int16_t)a - (int16_t)(temp + (p.c ? 0 : 1)); \
    p.v = (((a ^ temp) & 0x80) && ((a ^ tempw) & 0x80));  \
    p.c = tempv >= 0;                                     \
    a = tempw & 0xFF;                                     \
    setzn(a);                                             \
  }                                                       \
  else                                                    \
  {                                                       \
    hc = 0;                                               \
    p.z = p.n = 0;                                        \
    tempb = a - temp - ((p.c) ? 0 : 1);                   \
    if (!(tempb))                                         \
      p.z = 1;                                            \
    al = (a & 15) - (temp & 15) - ((p.c) ? 0 : 1);        \
    if (al & 16)                                          \
    {                                                     \
      al -= 6;                                            \
      al &= 0xF;                                          \
      hc = 1;                                             \
    }                                                     \
    ah = (a >> 4) - (temp >> 4);                          \
    if (hc)                                               \
      ah--;                                               \
    if ((a - (temp + ((p.c) ? 0 : 1))) & 0x80)            \
      p.n = 1;                                            \
    p.v = ((a ^ temp) & 0x80) && ((a ^ tempb) & 0x80);    \
    p.c = 1;                                              \
    if (ah & 16)                                          \
    {                                                     \
      p.c = 0;                                            \
      ah -= 6;                                            \
      ah &= 0xF;                                          \
    }                                                     \
    a = (al & 0xF) | ((ah & 0xF) << 4);                   \
  }

// int lns;
uint8_t opcode;
void exec6502(int linenum, int cpl)
{
  uint16_t addr;
  uint8_t temp;
  int tempi;
  int8_t offset;
  int c;

  int oldcyc;
  int halfline;
  //unsigned long ts1, ts2;
  // Maintain an error metric in 1000th of a cycle
  int error = 0;


  // static int nT = 0;
  // static int scanlineold = 255;
 
  // int lns = lines;
/*
  if (scanline != scanlineold)
  {
    drawline(scanline);
    //   Serial.println("In scanline");
    scanlineold = scanline;
  }
*/

  cycles += (cpl >> 1);
  // Serial.printf(", cycles: %d\r", cycles);
  //  At cps=64 us, every half line the error is 0.152us
  error += 152;
  if (error >= 1000)
  {
    cycles--;
    error -= 1000;
  }
  while (cycles > 0)
  {
    // Serial.print("*");
    oldcyc = cycles;
    if (skipint == 1)
    {
      skipint = 0;
    }
    opcode = readmem(pc);
    //  nInstr++;
    pc++;
    switch (opcode)
    {
    case 0x00: /*BRK*/
      /*                                Serial.printf("BRK at %04X\n",pc);
                 dumpregs();
                 dumpram();
                 exit(-1);*/
      pc++;
      push(pc >> 8);
      push(pc & 0xFF);
      temp = 0x30;
      if (p.c)
        temp |= 1;
      if (p.z)
        temp |= 2;
      if (p.d)
        temp |= 8;
      if (p.v)
        temp |= 0x40;
      if (p.n)
        temp |= 0x80;
      push(temp);
      pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
      p.i = 1;
      polltime(7);
      break;

    case 0x01: /*ORA (,x)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      a |= readmem(addr);
      setzn(a);
      polltime(6);
      break;

    case 0x05: /*ORA zp*/
      addr = readmem(pc);
      pc++;
      a |= readmem(addr);
      setzn(a);
      polltime(3);
      break;

    case 0x06: /*ASL zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      p.c = temp & 0x80;
      temp <<= 1;
      setzn(temp);
      writemem(addr, temp);
      polltime(5);
      break;

    case 0x08: /*PHP*/
      temp = 0x30;
      if (p.c)
        temp |= 1;
      if (p.z)
        temp |= 2;
      if (p.i)
        temp |= 4;
      if (p.d)
        temp |= 8;
      if (p.v)
        temp |= 0x40;
      if (p.n)
        temp |= 0x80;
      push(temp);
      polltime(3);
      break;

    case 0x09: /*ORA imm*/
      a |= readmem(pc);
      pc++;
      setzn(a);
      polltime(2);
      break;

    case 0x0A: /*ASL A*/
      p.c = a & 0x80;
      a <<= 1;
      setzn(a);
      polltime(2);
      break;

    case 0x0B: /*ANC imm*/
      a &= readmem(pc);
      pc++;
      setzn(a);
      p.c = p.n;
      polltime(2);
      break;

    case 0x0D: /*ORA abs*/
      addr = getw();
      a |= readmem(addr);
      setzn(a);
      polltime(4);
      break;

    case 0x0E: /*ASL abs*/
      addr = getw();
      temp = readmem(addr);
      p.c = temp & 0x80;
      temp <<= 1;
      setzn(temp);
      writemem(addr, temp);
      polltime(6);
      break;

    case 0x10: /*BPL*/
      offset = (int8_t)readmem(pc);
      pc++;
      temp = 2;
      if (!p.n)
      {
        temp++;
        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
          temp++;
        pc += offset;
      }
      polltime(temp);
      break;

    case 0x11: /*ORA (),y*/
      temp = readmem(pc);
      pc++;
      addr = readmem(temp) + (readmem(temp + 1) << 8);
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      a |= readmem(addr + y);
      setzn(a);
      polltime(5);
      break;

    case 0x15: /*ORA zp,x*/
      addr = readmem(pc);
      pc++;
      a |= ram[(addr + x) & 0xFF];
      setzn(a);
      polltime(3);
      break;

    case 0x16: /*ASL zp,x*/
      addr = (readmem(pc) + x) & 0xFF;
      pc++;
      temp = ram[addr];
      p.c = temp & 0x80;
      temp <<= 1;
      setzn(temp);
      ram[addr] = temp;
      polltime(5);
      break;

    case 0x18: /*CLC*/
      p.c = 0;
      polltime(2);
      break;

    case 0x19: /*ORA abs,y*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      a |= readmem(addr + y);
      setzn(a);
      polltime(4);
      break;

    case 0x1D: /*ORA abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      addr += x;
      a |= readmem(addr);
      setzn(a);
      polltime(4);
      break;

    case 0x1E: /*ASL abs,x*/
      addr = getw();
      addr += x;
      temp = readmem(addr);
      p.c = temp & 0x80;
      temp <<= 1;
      writemem(addr, temp);
      setzn(temp);
      polltime(7);
      break;

    case 0x20: /*JSR*/
      addr = getw();
      pc--;
      push(pc >> 8);
      push(pc);
      pc = addr;
      polltime(6);
      break;

    case 0x21: /*AND (,x)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      a &= readmem(addr);
      setzn(a);
      polltime(6);
      break;

    case 0x24: /*BIT zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      p.z = !(a & temp);
      p.v = temp & 0x40;
      p.n = temp & 0x80;
      polltime(3);
      break;

    case 0x25: /*AND zp*/
      addr = readmem(pc);
      pc++;
      a &= readmem(addr);
      setzn(a);
      polltime(3);
      break;

    case 0x26: /*ROL zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      tempi = p.c;
      p.c = temp & 0x80;
      temp <<= 1;
      if (tempi)
        temp |= 1;
      setzn(temp);
      writemem(addr, temp);
      polltime(5);
      break;

    case 0x28: /*PLP*/
      temp = pull();
      p.c = temp & 1;
      p.z = temp & 2;
      p.i = temp & 4;
      p.d = temp & 8;
      p.v = temp & 0x40;
      p.n = temp & 0x80;
      polltime(4);
      break;

    case 0x29: /*AND*/
      a &= readmem(pc);
      pc++;
      setzn(a);
      polltime(2);
      break;

    case 0x2A: /*ROL A*/
      tempi = p.c;
      p.c = a & 0x80;
      a <<= 1;
      if (tempi)
        a |= 1;
      setzn(a);
      polltime(2);
      break;

    case 0x2C: /*BIT abs*/
      addr = getw();
      temp = readmem(addr);
      p.z = !(a & temp);
      p.v = temp & 0x40;
      p.n = temp & 0x80;
      polltime(4);
      break;

    case 0x2D: /*AND abs*/
      addr = getw();
      a &= readmem(addr);
      setzn(a);
      polltime(4);
      break;

    case 0x2E: /*ROL abs*/
      addr = getw();
      temp = readmem(addr);
      tempi = p.c;
      p.c = temp & 0x80;
      temp <<= 1;
      if (tempi)
        temp |= 1;
      writemem(addr, temp);
      setzn(temp);
      polltime(6);
      break;

    case 0x30: /*BMI*/
      offset = (int8_t)readmem(pc);
      pc++;
      temp = 2;
      if (p.n)
      {
        temp++;
        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
          temp++;
        pc += offset;
      }
      polltime(temp);
      break;

    case 0x31: /*AND (),y*/
      temp = readmem(pc);
      pc++;
      addr = readmem(temp) + (readmem(temp + 1) << 8);
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      a &= readmem(addr + y);
      setzn(a);
      polltime(5);
      break;

    case 0x35: /*AND zp,x*/
      addr = readmem(pc);
      pc++;
      a &= ram[(addr + x) & 0xFF];
      setzn(a);
      polltime(3);
      break;

    case 0x36: /*ROL zp,x*/
      addr = readmem(pc);
      pc++;
      addr += x;
      addr &= 0xFF;
      temp = ram[addr];
      tempi = p.c;
      p.c = temp & 0x80;
      temp <<= 1;
      if (tempi)
        temp |= 1;
      setzn(temp);
      ram[addr] = temp;
      polltime(5);
      break;

    case 0x38: /*SEC*/
      p.c = 1;
      polltime(2);
      break;

    case 0x39: /*AND abs,y*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      a &= readmem(addr + y);
      setzn(a);
      polltime(4);
      break;

    case 0x3D: /*AND abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      addr += x;
      a &= readmem(addr);
      setzn(a);
      polltime(4);
      break;

    case 0x3E: /*ROL abs,x*/
      addr = getw();
      addr += x;
      temp = readmem(addr);
      tempi = p.c;
      p.c = temp & 0x80;
      temp <<= 1;
      if (tempi)
        temp |= 1;
      writemem(addr, temp);
      setzn(temp);
      polltime(7);
      break;

    case 0x40: /*RTI*/
      //                                output=0;
      temp = pull();
      p.c = temp & 1;
      p.z = temp & 2;
      p.i = temp & 4;
      p.d = temp & 8;
      p.v = temp & 0x40;
      p.n = temp & 0x80;
      pc = pull();
      pc |= (pull() << 8);
      polltime(6);
      nmilock = 0;
      break;

    case 0x41: /*EOR (,x)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      a ^= readmem(addr);
      setzn(a);
      polltime(6);
      break;

    case 0x45: /*EOR zp*/
      addr = readmem(pc);
      pc++;
      a ^= readmem(addr);
      setzn(a);
      polltime(3);
      break;

    case 0x46: /*LSR zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      p.c = temp & 1;
      temp >>= 1;
      setzn(temp);
      writemem(addr, temp);
      polltime(5);
      break;

    case 0x48: /*PHA*/
      push(a);
      polltime(3);
      break;

    case 0x49: /*EOR*/
      a ^= readmem(pc);
      pc++;
      setzn(a);
      polltime(2);
      break;

    case 0x4A: /*LSR A*/
      p.c = a & 1;
      a >>= 1;
      setzn(a);
      polltime(2);
      break;

    case 0x4C: /*JMP*/
      addr = getw();
      pc = addr;
      polltime(3);
      break;

    case 0x4D: /*EOR abs*/
      addr = getw();
      a ^= readmem(addr);
      setzn(a);
      polltime(4);
      break;

    case 0x4E: /*LSR abs*/
      addr = getw();
      polltime(4);
      temp = readmem(addr);
      polltime(1);
      writemem(addr, temp);
      polltime(1);
      p.c = temp & 1;
      temp >>= 1;
      setzn(temp);
      writemem(addr, temp);
      polltime(6);
      break;

    case 0x50: /*BVC*/
      offset = (int8_t)readmem(pc);
      pc++;
      temp = 2;
      if (!p.v)
      {
        temp++;
        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
          temp++;
        pc += offset;
      }
      polltime(temp);
      break;

    case 0x51: /*EOR (),y*/
      temp = readmem(pc);
      pc++;
      addr = readmem(temp) + (readmem(temp + 1) << 8);
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      a ^= readmem(addr + y);
      setzn(a);
      polltime(5);
      break;

    case 0x55: /*EOR zp,x*/
      addr = readmem(pc);
      pc++;
      a ^= ram[(addr + x) & 0xFF];
      setzn(a);
      polltime(3);
      break;

    case 0x56: /*LSR zp,x*/
      addr = (readmem(pc) + x) & 0xFF;
      pc++;
      temp = ram[addr];
      p.c = temp & 1;
      temp >>= 1;
      setzn(temp);
      ram[addr] = temp;
      polltime(5);
      break;

    case 0x58: /*CLI*/
      //                                if (pc<0x8000) Serial.printf("CLI at %04X\n",pc);
      p.i = 0;
      skipint = 1;
      polltime(2);
      break;

    case 0x59: /*EOR abs,y*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      a ^= readmem(addr + y);
      setzn(a);
      polltime(4);
      break;

    case 0x5D: /*EOR abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      addr += x;
      a ^= readmem(addr);
      setzn(a);
      polltime(4);
      break;

    case 0x5E: /*LSR abs,x*/
      addr = getw();
      addr += x;
      temp = readmem(addr);
      p.c = temp & 1;
      temp >>= 1;
      writemem(addr, temp);
      setzn(temp);
      polltime(7);
      break;

    case 0x60: /*RTS*/
      pc = pull();
      pc |= (pull() << 8);
      pc++;
      polltime(6);
      break;

    case 0x61: /*ADC (,x)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      temp = readmem(addr);
      ADC(temp);
      polltime(6);
      break;

    case 0x65: /*ADC zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      ADC(temp);
      polltime(3);
      break;

    case 0x66: /*ROR zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      tempi = p.c;
      p.c = temp & 1;
      temp >>= 1;
      if (tempi)
        temp |= 0x80;
      setzn(temp);
      writemem(addr, temp);
      polltime(5);
      break;

    case 0x68: /*PLA*/
      a = pull();
      setzn(a);
      polltime(4);
      break;

    case 0x69: /*ADC imm*/
      temp = readmem(pc);
      pc++;
      ADC(temp);
      polltime(2);
      break;

    case 0x6A: /*ROR A*/
      tempi = p.c;
      p.c = a & 1;
      a >>= 1;
      if (tempi)
        a |= 0x80;
      setzn(a);
      polltime(2);
      break;

    case 0x6C: /*JMP ()*/
      addr = getw();
      if ((addr & 0xFF) == 0xFF)
        pc = readmem(addr) | (readmem(addr - 0xFF) << 8);
      else
        pc = readmem(addr) | (readmem(addr + 1) << 8);
      polltime(5);
      break;

    case 0x6D: /*ADC abs*/
      addr = getw();
      temp = readmem(addr);
      ADC(temp);
      polltime(4);
      break;

    case 0x6E: /*ROR abs*/
      addr = getw();
      polltime(4);
      temp = readmem(addr);
      polltime(1);
      writemem(addr, temp);
      polltime(1);
      tempi = p.c;
      p.c = temp & 1;
      temp >>= 1;
      if (tempi)
        temp |= 0x80;
      setzn(temp);
      writemem(addr, temp);
      break;

    case 0x70: /*BVS*/
      offset = (int8_t)readmem(pc);
      pc++;
      temp = 2;
      if (p.v)
      {
        temp++;
        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
          temp++;
        pc += offset;
      }
      polltime(temp);
      break;

    case 0x71: /*ADC (),y*/
      temp = readmem(pc);
      pc++;
      addr = readmem(temp) + (readmem(temp + 1) << 8);
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      temp = readmem(addr + y);
      ADC(temp);
      polltime(5);
      break;

    case 0x75: /*ADC zp,x*/
      addr = readmem(pc);
      pc++;
      temp = ram[(addr + x) & 0xFF];
      ADC(temp);
      polltime(4);
      break;

    case 0x76: /*ROR zp,x*/
      addr = readmem(pc);
      pc++;
      addr += x;
      addr &= 0xFF;
      temp = ram[addr];
      tempi = p.c;
      p.c = temp & 1;
      temp >>= 1;
      if (tempi)
        temp |= 0x80;
      setzn(temp);
      ram[addr] = temp;
      polltime(5);
      break;

    case 0x78: /*SEI*/
      //                                if (pc<0x8000) Serial.printf("SEI at %04X\n",pc);
      p.i = 1;
      polltime(2);
      //                                if (output2) Serial.printf("SEI at line %i %04X %02X %02X\n",lines,pc,ram[0x103+s],ram[0x104+s]);
      break;

    case 0x79: /*ADC abs,y*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      temp = readmem(addr + y);
      ADC(temp);
      polltime(4);
      break;

    case 0x7D: /*ADC abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      addr += x;
      temp = readmem(addr);
      ADC(temp);
      polltime(4);
      break;

    case 0x7E: /*ROR abs,x*/
      addr = getw();
      addr += x;
      temp = readmem(addr);
      tempi = p.c;
      p.c = temp & 1;
      temp >>= 1;
      if (tempi)
        temp |= 0x80;
      writemem(addr, temp);
      setzn(temp);
      polltime(7);
      break;

    case 0x81: /*STA (,x)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      writemem(addr, a);
      polltime(6);
      break;

    case 0x84: /*STY zp*/
      addr = readmem(pc);
      pc++;
      writemem(addr, y);
      polltime(3);
      break;

    case 0x85: /*STA zp*/
      addr = readmem(pc);
      pc++;
      writemem(addr, a);
      polltime(3);
      break;

    case 0x86: /*STX zp*/
      addr = readmem(pc);
      pc++;
      writemem(addr, x);
      polltime(3);
      break;

    case 0x88: /*DEY*/
      y--;
      setzn(y);
      polltime(2);
      break;

    case 0x8A: /*TXA*/
      a = x;
      setzn(a);
      polltime(2);
      break;

    case 0x8C: /*STY abs*/
      addr = getw();
      polltime(3);
      writemem(addr, y);
      polltime(1);
      break;

    case 0x8D: /*STA abs*/
      addr = getw();
      polltime(3);
      writemem(addr, a);
      polltime(1);
      break;

    case 0x8E: /*STX abs*/
      addr = getw();
      polltime(3);
      writemem(addr, x);
      polltime(1);
      break;

    case 0x90: /*BCC*/
      offset = (int8_t)readmem(pc);
      pc++;
      temp = 2;
      if (!p.c)
      {
        temp++;
        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
          temp++;
        pc += offset;
      }
      polltime(temp);
      break;

    case 0x91: /*STA (),y*/
      temp = readmem(pc);
      pc++;
      addr = readmem(temp) + (readmem(temp + 1) << 8) + y;
      writemem(addr, a);
      polltime(6);
      break;

    case 0x94: /*STY zp,x*/
      addr = readmem(pc);
      pc++;
      ram[(addr + x) & 0xFF] = y;
      polltime(4);
      break;

    case 0x95: /*STA zp,x*/
      addr = readmem(pc);
      pc++;
      writemem((addr + x) & 0xFF, a);
      //                                ram[(addr+x)&0xFF]=a;
      polltime(4);
      break;

    case 0x96: /*STX zp,y*/
      addr = readmem(pc);
      pc++;
      ram[(addr + y) & 0xFF] = x;
      polltime(4);
      break;

    case 0x98: /*TYA*/
      a = y;
      setzn(a);
      polltime(2);
      break;

    case 0x99: /*STA abs,y*/
      addr = getw();
      polltime(4);
      writemem(addr + y, a);
      polltime(1);
      break;

    case 0x9A: /*TXS*/
      s = x;
      polltime(2);
      break;

    case 0x9D: /*STA abs,x*/
      addr = getw();
      polltime(4);
      writemem(addr + x, a);
      polltime(1);
      break;

    case 0xA0: /*LDY imm*/
      y = readmem(pc);
      pc++;
      setzn(y);
      polltime(2);
      break;

    case 0xA1: /*LDA (,x)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      a = readmem(addr);
      setzn(a);
      polltime(6);
      break;

    case 0xA2: /*LDX imm*/
      x = readmem(pc);
      pc++;
      setzn(x);
      polltime(2);
      break;

    case 0xA4: /*LDY zp*/
      addr = readmem(pc);
      pc++;
      y = readmem(addr);
      setzn(y);
      polltime(3);
      break;

    case 0xA5: /*LDA zp*/
      addr = readmem(pc);
      pc++;
      a = readmem(addr);
      setzn(a);
      polltime(3);
      break;

    case 0xA6: /*LDX zp*/
      addr = readmem(pc);
      pc++;
      x = readmem(addr);
      setzn(x);
      polltime(3);
      break;

    case 0xA8: /*TAY*/
      y = a;
      setzn(y);
      break;

    case 0xA9: /*LDA imm*/
      a = readmem(pc);
      pc++;
      setzn(a);
      polltime(2);
      break;

    case 0xAA: /*TAX*/
      x = a;
      setzn(x);
      polltime(2);
      break;

    case 0xAC: /*LDY abs*/
      addr = getw();
      polltime(3);
      y = readmem(addr);
      setzn(y);
      polltime(1);
      break;

    case 0xAD: /*LDA abs*/
      addr = getw();
      polltime(3);
      a = readmem(addr);
      setzn(a);
      polltime(1);
      break;

    case 0xAE: /*LDX abs*/
      addr = getw();
      polltime(3);
      x = readmem(addr);
      setzn(x);
      polltime(1);
      break;

    case 0xB0: /*BCS*/
      offset = (int8_t)readmem(pc);
      pc++;
      temp = 2;
      if (p.c)
      {
        temp++;
        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
          temp++;
        pc += offset;
      }
      polltime(temp);
      break;

    case 0xB1: /*LDA (),y*/
      temp = readmem(pc);
      pc++;
      addr = readmem(temp) + (readmem(temp + 1) << 8);
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      a = readmem(addr + y);
      setzn(a);
      polltime(5);
      break;

    case 0xB4: /*LDY zp,x*/
      addr = readmem(pc);
      pc++;
      y = ram[(addr + x) & 0xFF];
      setzn(y);
      polltime(3);
      break;

    case 0xB5: /*LDA zp,x*/
      addr = readmem(pc);
      pc++;
      a = ram[(addr + x) & 0xFF];
      setzn(a);
      polltime(3);
      break;

    case 0xB6: /*LDX zp,y*/
      addr = readmem(pc);
      pc++;
      x = ram[(addr + y) & 0xFF];
      setzn(x);
      polltime(3);
      break;

    case 0xB8: /*CLV*/
      p.v = 0;
      polltime(2);
      break;

    case 0xB9: /*LDA abs,y*/
      addr = getw();
      polltime(3);
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      a = readmem(addr + y);
      setzn(a);
      polltime(1);
      break;

    case 0xBA: /*TSX*/
      x = s;
      setzn(x);
      polltime(2);
      break;

    case 0xBC: /*LDY abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      y = readmem(addr + x);
      setzn(y);
      polltime(4);
      break;

    case 0xBD: /*LDA abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      a = readmem(addr + x);
      setzn(a);
      polltime(4);
      break;

    case 0xBE: /*LDX abs,y*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      x = readmem(addr + y);
      setzn(x);
      polltime(4);
      break;

    case 0xC0: /*CPY imm*/
      temp = readmem(pc);
      pc++;
      setzn(y - temp);
      p.c = (y >= temp);
      polltime(2);
      break;

    case 0xC1: /*CMP (,x)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      temp = readmem(addr);
      setzn(a - temp);
      p.c = (a >= temp);
      polltime(6);
      break;

    case 0xC4: /*CPY zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      setzn(y - temp);
      p.c = (y >= temp);
      polltime(3);
      break;

    case 0xC5: /*CMP zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      setzn(a - temp);
      p.c = (a >= temp);
      polltime(3);
      break;

    case 0xC6: /*DEC zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr) - 1;
      writemem(addr, temp);
      setzn(temp);
      polltime(5);
      break;

    case 0xC8: /*INY*/
      y++;
      setzn(y);
      polltime(2);
      break;

    case 0xC9: /*CMP imm*/
      temp = readmem(pc);
      pc++;
      setzn(a - temp);
      p.c = (a >= temp);
      polltime(2);
      break;

    case 0xCA: /*DEX*/
      x--;
      setzn(x);
      polltime(2);
      break;

    case 0xCC: /*CPY abs*/
      addr = getw();
      temp = readmem(addr);
      setzn(y - temp);
      p.c = (y >= temp);
      polltime(4);
      break;

    case 0xCD: /*CMP abs*/
      addr = getw();
      temp = readmem(addr);
      setzn(a - temp);
      p.c = (a >= temp);
      polltime(4);
      break;

    case 0xCE: /*DEC abs*/
      addr = getw();
      polltime(4);
      temp = readmem(addr) - 1;
      polltime(1);
      writemem(addr, temp + 1);
      polltime(1);
      writemem(addr, temp);
      setzn(temp);
      break;

    case 0xD0: /*BNE*/
      offset = (int8_t)readmem(pc);
      pc++;
      temp = 2;
      if (!p.z)
      {
        temp++;
        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
          temp++;
        pc += offset;
      }
      polltime(temp);
      break;

    case 0xD1: /*CMP (),y*/
      temp = readmem(pc);
      pc++;
      addr = readmem(temp) + (readmem(temp + 1) << 8);
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      temp = readmem(addr + y);
      setzn(a - temp);
      p.c = (a >= temp);
      polltime(5);
      break;

    case 0xD5: /*CMP zp,x*/
      addr = readmem(pc);
      pc++;
      temp = ram[(addr + x) & 0xFF];
      setzn(a - temp);
      p.c = (a >= temp);
      polltime(3);
      break;

    case 0xD6: /*DEC zp,x*/
      addr = readmem(pc);
      pc++;
      ram[(addr + x) & 0xFF]--;
      setzn(ram[(addr + x) & 0xFF]);
      polltime(5);
      break;

    case 0xD8: /*CLD*/
      p.d = 0;
      polltime(2);
      break;

    case 0xD9: /*CMP abs,y*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      temp = readmem(addr + y);
      setzn(a - temp);
      p.c = (a >= temp);
      polltime(4);
      break;

    case 0xDD: /*CMP abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      temp = readmem(addr + x);
      setzn(a - temp);
      p.c = (a >= temp);
      polltime(4);
      break;

    case 0xDE: /*DEC abs,x*/
      addr = getw();
      addr += x;
      temp = readmem(addr) - 1;
      writemem(addr, temp);
      setzn(temp);
      polltime(6);
      break;

    case 0xE0: /*CPX imm*/
      temp = readmem(pc);
      pc++;
      setzn(x - temp);
      p.c = (x >= temp);
      polltime(3);
      break;

      /*SP8 CHANGE SBC(oper,X) */

    case 0xE1: /*SBC (,x)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      temp = readmem(addr);
      SBC(temp);
      polltime(6);
      break;

      /*SP8 END */

    case 0xE4: /*CPX zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      setzn(x - temp);
      p.c = (x >= temp);
      polltime(3);
      break;

    case 0xE5: /*SBC zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr);
      SBC(temp);
      polltime(3);
      break;

    case 0xE6: /*INC zp*/
      addr = readmem(pc);
      pc++;
      temp = readmem(addr) + 1;
      writemem(addr, temp);
      setzn(temp);
      polltime(5);
      break;

    case 0xE8: /*INX*/
      x++;
      setzn(x);
      polltime(2);
      break;

    case 0xE9: /*SBC imm*/
      temp = readmem(pc);
      pc++;
      SBC(temp);
      polltime(2);
      break;

    case 0xEA: /*NOP*/
      polltime(2);
      break;

    case 0xEC: /*CPX abs*/
      addr = getw();
      temp = readmem(addr);
      setzn(x - temp);
      p.c = (x >= temp);
      polltime(3);
      break;

    case 0xED: /*SBC abs*/
      addr = getw();
      temp = readmem(addr);
      SBC(temp);
      polltime(4);
      break;

    case 0xEE: /*DEC abs*/
      addr = getw();
      polltime(4);
      temp = readmem(addr) + 1;
      polltime(1);
      writemem(addr, temp - 1);
      polltime(1);
      writemem(addr, temp);
      setzn(temp);
      break;

    case 0xF0: /*BEQ*/
      offset = (int8_t)readmem(pc);
      pc++;
      temp = 2;
      if (p.z)
      {
        temp++;
        if ((pc & 0xFF00) ^ ((pc + offset) & 0xFF00))
          temp++;
        pc += offset;
      }
      polltime(temp);
      break;

    case 0xF1: /*SBC (),y*/
      temp = readmem(pc);
      pc++;
      addr = readmem(temp) + (readmem(temp + 1) << 8);
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      temp = readmem(addr + y);
      SBC(temp);
      polltime(5);
      break;

    case 0xF5: /*SBC zp,x*/
      addr = readmem(pc);
      pc++;
      temp = ram[(addr + x) & 0xFF];
      SBC(temp);
      polltime(3);
      break;

    case 0xF6: /*INC zp,x*/
      addr = readmem(pc);
      pc++;
      ram[(addr + x) & 0xFF]++;
      setzn(ram[(addr + x) & 0xFF]);
      polltime(5);
      break;

    case 0xF8: /*SED*/
      p.d = 1;
      polltime(2);
      break;

    case 0xF9: /*SBC abs,y*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + y) & 0xFF00))
        polltime(1);
      temp = readmem(addr + y);
      SBC(temp);
      polltime(4);
      break;

    case 0xFD: /*SBC abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      temp = readmem(addr + x);
      SBC(temp);
      polltime(4);
      break;

    case 0xFE: /*INC abs,x*/
      addr = getw();
      addr += x;
      temp = readmem(addr) + 1;
      writemem(addr, temp);
      setzn(temp);
      polltime(6);
      break;

    case 0x04: /*Undocumented - NOP zp*/
      addr = readmem(pc);
      pc++;
      polltime(3);
      break;

    case 0xF4: /*Undocumented - NOP zpx*/
      addr = readmem(pc);
      pc++;
      polltime(4);
      break;

    case 0xA3: /*Undocumented - LAX (,y)*/
      temp = readmem(pc) + x;
      pc++;
      addr = readmem(temp) | (readmem(temp + 1) << 8);
      a = x = readmem(addr);
      setzn(a);
      polltime(6);
      break;

    case 0x07: /*Undocumented - SLO zp*/
      addr = readmem(pc);
      pc++;
      c = ram[addr] & 0x80;
      ram[addr] <<= 1;
      a |= ram[addr];
      setzn(a);
      polltime(5);
      break;

    case 0x23: /*Undocumented - RLA*/
      break;   /*This was found in Repton 3 and
                       looks like a mistake, so I'll
                       ignore it for now*/

    case 0x2F:       /*Undocumented - RLA abs*/
      addr = getw(); /*Found in The Hobbit*/
      temp = readmem(addr);
      tempi = p.c;
      p.c = temp & 0x80;
      temp <<= 1;
      if (tempi)
        temp |= 1;
      writemem(addr, temp);
      a &= temp;
      setzn(a);
      polltime(6);
      break;

    case 0x4B: /*Undocumented - ASR*/
      a &= readmem(pc);
      pc++;
      p.c = a & 1;
      a >>= 1;
      setzn(a);
      polltime(2);
      break;

    case 0x67: /*Undocumented - RRA zp*/
      addr = readmem(pc);
      pc++;
      ram[addr] >>= 1;
      if (p.c)
        ram[addr] |= 1;
      temp = ram[addr];
      ADC(temp);
      polltime(5);
      break;

    case 0x80: /*Undocumented - NOP imm*/
      readmem(pc);
      pc++;
      polltime(2);
      break;

    case 0x87: /*Undocumented - SAX zp*/
      addr = readmem(pc);
      pc++;
      ram[addr] = a & x;
      polltime(3);
      break;

    case 0x9C: /*Undocumented - SHY abs,x*/
      addr = getw();
      writemem(addr + x, y & ((addr >> 8) + 1));
      polltime(5);
      break;

    case 0xDA: /*Undocumented - NOP*/
      //                                case 0xFA:
      polltime(2);
      break;

    case 0xDC: /*Undocumented - NOP abs,x*/
      addr = getw();
      if ((addr & 0xFF00) ^ ((addr + x) & 0xFF00))
        polltime(1);
      readmem(addr + x);
      polltime(4);
      break;

    default:
      switch (opcode & 0xF)
      {
      case 0xA:
        break;
      case 0x0:
      case 0x2:
      case 0x3:
      case 0x4:
      case 0x7:
      case 0x9:
      case 0xB:
        pc++;
        break;
      case 0xC:
      case 0xE:
      case 0xF:
        pc += 2;
        break;
      }
    }
    oldcyc -= cycles;
    if (debugon)
    {
      if (pc < 0xFE00)
      {
        debugdisaddr = pc;
        debugdisassemble();
        // Serial.printf("%02X A=%02X X=%02X Y=%02X PC=%04X %c%c%c%c%c%c %i\n", opcode, a, x, y, pc, (p.n) ? 'N' : ' ', (p.v) ? 'V' : ' ', (p.d) ? 'D' : ' ', (p.i) ? 'I' : ' ', (p.z) ? 'Z' : ' ', (p.c) ? 'C' : ' ', totcyc);
      }
    }
    ins++;
    if (nmi && !oldnmi)
    {
      push(pc >> 8);
      push(pc & 0xFF);
      temp = 0x20;
      if (p.c)
        temp |= 1;
      if (p.z)
        temp |= 2;
      if (p.i)
        temp |= 4;
      if (p.d)
        temp |= 8;
      if (p.v)
        temp |= 0x40;
      if (p.n)
        temp |= 0x80;
      push(temp);
      pc = readmem(0xFFFA) | (readmem(0xFFFB) << 8);
      p.i = 1;
      polltime(7);
      nmi = 0;
      nmilock = 1;
      //                                rpclog("NMI %04X\n",pc);
    }
    oldnmi = nmi;

    if ((interrupt && !p.i && !skipint) || skipint == 2)
    {
      //                                Serial.printf("Intrupt\n");
      //                                if (skipint==2) Serial.printf("interrupt\n");
      //        rpclog("Interrupt\n");
      skipint = 0;
      push(pc >> 8);
      push(pc & 0xFF);
      temp = 0x20;
      if (p.c)
        temp |= 1;
      if (p.z)
        temp |= 2;
      if (p.i)
        temp |= 4;
      if (p.d)
        temp |= 8;
      if (p.v)
        temp |= 0x40;
      if (p.n)
        temp |= 0x80;
      push(temp);
      pc = readmem(0xFFFE) | (readmem(0xFFFF) << 8);
      p.i = 1;
      polltime(7);
      //                                if (pc<0x100)
      //                                {
      //                                        Serial.printf("Interrupt line %i %04X %04X %02X %02X %02X %02X %i %i %i\n",lines,cia2.t1c,cia2.t2c,cia1.ifr,vic.ifr,cia2.ifr,cia2.ier,nmi,oldnmi,nmilock);
      //                                        output2=1;
      //                                }
      //                                output=1;
      //                                Serial.printf("Interrupt line %i %i %02X %02X %02X %02X\n",interrupt,lines,sysvia.ifr&sysvia.ier,uservia.ifr&uservia.ier,uservia.ier,uservia.ifr);
    }
    if (interrupt && !p.i && skipint)
    {
      skipint = 2;
      //                                Serial.printf("skipint=2\n");
    }
  }
  //       Serial.printf("TijdEmu: %05d   \r", millis() - ts1);
  //  }
  // Serial.printf("TijdEmu: %05d   \r", millis() - ts1);
  //  }
  // 2,3 uS per instructie gemiddeld. bij 1 MHz cpu., is nu 8 uS met print
  /* if (nT++ > 100)
  {
    long ts1 = millis() - ts2;
    float f = ts1 / (float)nInstr;
    nT = 0;
    Serial.printf("TijdEmu: %05d,    #instr: %d     Tijd: %f     \r", ts1, nInstr, f);
  }
  */
  // }
}

void loadroms()
{
  utility_ptr = &barawData[0x3000]; //(ROM_OFS_UTILITY]; // pcharme = 3
  abasic_ptr = &barawData[0x8000];  // ROM_OFS_ABASIC];
  afloat_ptr = &barawData[0x9000];  // ROM_OFS_AFLOAT];
  dosrom_ptr = &barawData[0xA000];  // dos rom
  akernel_ptr = &barawData[0xb000]; // ROM_OFS_AKERNEL];

  memcpy(&ram[0x2900], Invader + 0X16, sizeof(Invader) - 0x16);
}

void init8255()
{
  int c, d;

  memset(keyl, 0, sizeof(keyl));
  for (c = 0; c < 16; c++)
  {
    for (d = 0; d < 6; d++)
    {
      keyl[keys[c][d]] = c | (d << 4) | 0x80;
    }
  }
}

int keyrow;
void write8255(uint16_t addr, uint8_t val)
{
  int oldgfx = gfxmode;

  switch (addr & 0xF)
  {
  case 0:
    keyrow = val & 0xF;
    //                if (gfxmode!=(val>>4)) Serial.printf("GFX mode now %02X %04X\n",val,pc);
    gfxmode = (val >> 4) & 0x0F;
    if (gfxmode != oldgfx)
    {
      // Serial.printf("gfxmode changed at PC=%04X from %02X to %02X\n", pc, oldgfx, gfxmode);
      // for (int i = 0; i < 6144; i++)
      //   svideo[i] = 0xff;

      // Serial.printf("GFX mode now %02X %04X\n", val, pc);
      // Serial.printf("Keyrow now %i %04X\n", keyrow, pc);
    }
    break;
  case 2:
    css = (val & 8) >> 2;
    speaker = val & 4;
    //    rpclog("Speaker %i\n", (val & 4) >> 2);
    break;
  case 3:
    switch (val & 0xE)
    {
    case 0x4:
      speaker = val & 1;
      // rpclog("Speaker %i\n", (val & 4) >> 2);
      break;

    case 0x6:
      css = (val & 1) ? 2 : 0;
      break;
    }
    break;
    //                        rpclog("8255 port 3 %02X\n",val);
  }
  //        Serial.printf("Write 8255 %04X %02X\n",addr,val);
}

uint8_t read8255(uint16_t addr)
{
  uint8_t temp = 0xFF;
  int c;
  int lb;

  //        Serial.printf("Read 8255 %04X %04X\n",addr,pc);
  switch (addr & 3)
  {
  case 0:
    return (keyrow & 0x0F) | ((gfxmode << 4) & 0xF0);
    break;
  case 1:
    for (c = 0; c < 128; c++)
    {
      if (key[c] && keyl[keylookup[c]] & 0x80 && keyrow == (keyl[keylookup[c]] & 0xF))
      {
        temp &= ~(1 << ((keyl[keylookup[c]] & 0x70) >> 4));
        // M5.Lcd.setCursor(100, 50);
        // M5.Lcd.printf("keyl:%d.%02X ", keylookup[c], temp);
        //  6 = a7 -> Output 7, PB2; 9 = a4 ->output 4, PB2; 0 = 93 (hex) PB1, output 3
        //  enter=96 -> output 6, pb1; o = C2 - > output 2, PB4
      }
      if (key[c] && ctrl)
      {
        temp &= ~0x40;
      }
      if (key[c] && shift)
      {
        temp &= ~0x80;
      }
    }
    return temp;
    break;
  case 2:

    if (vbl)
      temp &= ~0x80;
    if (alt)
      temp &= ~0x40;
    if (!css)
      if (!css)
        temp &= ~8;
    if (!speaker)
      temp &= ~4;
    // if (!intone)
    //   temp &= ~0x10;
    // if (!tapedat)
    //   temp &= ~0x20;
    //                 Serial.printf("VBL %i %04X\n",vbl,pc);
    return temp;
    break;
  default:
    // Serial.printf("Read 8255 %04X\n", addr);
    delay(1);
  }
  return 0;
}

void updateIFR()
{
  if ((via.ifr & 0x7F) & (via.ier & 0x7F))
  {
    via.ifr |= 0x80;
    interrupt |= 2;
  }
  else
  {
    via.ifr &= ~0x80;
    interrupt &= ~2;
  }
}

int timerout = 1;
int lns;
void updatetimers()
{
  if (via.t1c < -3)
  {
    while (via.t1c < -3)
      via.t1c += via.t1l + 4;
    if (!via.t1hit)
    {
      via.ifr |= TIMER1INT;
      updateIFR();
    }

    if ((via.acr & 0x80) && !via.t1hit)
    {
      via.orb ^= 0x80;
      via.irb ^= 0x80;
      via.portb ^= 0x80;
      timerout ^= 1;
    }

    if (!(via.acr & 0x40))
      via.t1hit = 1;
  }

  if (!(via.acr & 0x20) /* && !via.t2hit*/)
  {
    if (via.t2c < -3 && !via.t2hit)
    {
      //                        via.t2c+=via.t2l+4;
      //                        rpclog(" Timer 2 reset %05X %05X %04X\n",via.t2c,via.t2l,pc);
      if (!via.t2hit)
      {
        via.ifr |= TIMER2INT;
        updateIFR();
        //                                output=1;
      }
      via.t2hit = 1;
    }
  }
}

void writevia(uint16_t addr, uint8_t val)
{
  //        rpclog("VIA write %04X %02X %04X\n",addr,val,pc);
  switch (addr & 0xF)
  {
  case ORA:
    via.ifr &= 0xfc; //~PORTAINT;
    updateIFR();
  case ORAnh:
    via.ora = val;
    via.porta = (via.porta & ~via.ddra) | (via.ora & via.ddra);

    /*SP7 CHANGES*/

    switch (val)
    {
    case 13:
      //  Serial.printf("\n");
      yield();
      break;
    default:
      //  Serial.printf("%C", val);
      yield();
      break;
    }
    /*END SP7*/

    break;
  case ORB:
    via.orb = val;
    via.portb = (via.portb & ~via.ddrb) | (via.orb & via.ddrb);
    via.ifr &= 0xfe; //~PORTBINT;
    updateIFR();
    break;

  case DDRA:
    via.ddra = val;
    break;
  case DDRB:
    via.ddrb = val;
    break;
  case ACR:
    via.acr = val;
    break;
  case PCR:
    via.pcr = val;
    break;
  case T1LL:
  case T1CL:
    //                Serial.printf("T1L write %02X at %04X %i\n",val,pc,lns);
    via.t1l &= 0xFF00;
    via.t1l |= val;
    break;
  case T1LH:
    //                Serial.printf("T1LH write %02X at %04X %i\n",val,pc,lns);
    via.t1l &= 0xFF;
    via.t1l |= (val << 8);
    if (via.acr & 0x40)
    {
      via.ifr &= ~TIMER1INT;
      updateIFR();
    }
    //                Serial.printf("%04X\n",via.t1l>>1);
    break;
  case T1CH:
    if ((via.acr & 0xC0) == 0x80)
      timerout = 0;
    //                Serial.printf("T1CH write %02X at %04X %i\n",val,pc,lns);
    via.t1l &= 0xFF;
    via.t1l |= (val << 8);
    //                if (via.t1c<1) Serial.printf("UT1 reload %i\n",via.t1c);
    //                Serial.printf("T1 l now %05X\n",via.t1l);
    via.t1c = via.t1l + 1;
    via.ifr &= ~TIMER1INT;
    updateIFR();
    via.t1hit = 0;
    break;
  case T2CL:
    via.t2l &= 0xFF00;
    via.t2l |= val;
    //                Serial.printf("T2CL=%02X at line %i\n",val,line);
    break;
  case T2CH: // && !(via.ifr&TIMER2INT))
    if ((via.t2c == -3 && (via.ier & TIMER2INT)) ||
        (via.ifr & via.ier & TIMER2INT))
    {
      interrupt |= 128;
      //                        rpclog("uTimer 2 extra interrupt\n");
    }
    //                if (output) rpclog("Write uT2CH %i\n",via.t2c);
    via.t2l &= 0xFF;
    via.t2l |= (val << 8);
    //                if (via.t2c<1) Serial.printf("UT2 reload %i\n",via.t2c);
    via.t2c = via.t2l + 1;
    via.ifr &= ~TIMER2INT;
    updateIFR();
    via.t2hit = 0;
    //                output=0;
    //                Serial.printf("T2CH=%02X at line %i\n",val,line);
    break;
  case IER:
    /*                if (val==0x40)
            {
                Serial.printf("Here\n");
        //                        output=1;
            }*/
    if (val & 0x80)
      via.ier |= (val & 0x7F);
    else
      via.ier &= ~(val & 0x7F);
    updateIFR();
    //                rpclog("Write IER %02X %04X %02X\n",val,pc,via.ier);
    //                if (via.ier&0x40) Serial.printf("0x40 enabled at %04X\n",pc);
    //                via.ifr&=~via.ier;
    break;
  case IFR:
    via.ifr &= ~(val & 0x7F);
    updateIFR();
    //                rpclog("Write IFR %02X %04X %02X\n",val,pc,via.ifr);
    break;
  }
}

uint8_t readvia(uint16_t addr)
{
  uint8_t temp;

  //        if (addr>=4 && addr<=9) Serial.printf("Read U %04X %04X\n",addr,pc);
  //        rpclog("Read  VIA %04X %04X\n",addr,pc);
  switch (addr & 0xF)
  {
  case ORA:
    via.ifr &= ~PORTAINT;
    updateIFR();
  case ORAnh:
    temp = via.ora & via.ddra;
    temp |= (via.porta & ~via.ddra);
    temp &= 0x7F;
    return temp;

  case ORB:
    //                via.ifr&=~PORTBINT;
    updateIFR();
    temp = via.orb & via.ddrb;
    if (via.acr & 2)
      temp |= (via.irb & ~via.ddrb);
    else
      temp |= (via.portb & ~via.ddrb);
    temp |= 0xFF;
    if (timerout)
      temp |= 0x80;
    else
      temp &= ~0x80;
    //                Serial.printf("ORB read %02X\n",temp);
    //                temp|=0xF0;
    return temp;

  case DDRA:
    return via.ddra;
  case DDRB:
    return via.ddrb;
  case T1LL:
    //                Serial.printf("Read T1LL %02X %04X\n",(via.t1l&0x1FE)>>1,via.t1l);
    return via.t1l & 0xFF;
  case T1LH:
    //                Serial.printf("Read T1LH %02X\n",via.t1l>>9);
    return via.t1l >> 8;
  case T1CL:
    via.ifr &= ~TIMER1INT;
    updateIFR();
    //                Serial.printf("Read T1CL %02X %i %08X\n",((via.t1c+2)>>1)&0xFF,via.t1c,via.t1c);
    if (via.t1c < -1)
      return 0xFF;
    return via.t1c & 0xFF;
  case T1CH:
    if (via.t1c < -1)
      return 0xFF;
    return via.t1c >> 8;
  case T2CL:
    via.ifr &= ~TIMER2INT;
    updateIFR();
    //                Serial.printf("Read T2CL %02X\n",((via.t2c+2)>>1)&0xFF);
    //                if (via.t2c<0) return 0xFF;
    return via.t2c & 0xFF;
  case T2CH:
    //                Serial.printf("Read T2CH %02X\n",((via.t2c+2)>>1)>>8);
    //                Serial.printf("T2CH read %05X %04X %02X %04X %i %02X\n",via.t2c,via.t2c>>1,via.t2c>>9,pc,p.i,a);
    //                if (via.t2c<0) return 0xFF;
    return via.t2c >> 8;
  case ACR:
    return via.acr;
  case PCR:
    return via.pcr;
  case IER:
    return via.ier | 0x80;
  case IFR:
    //                rpclog("IFR %02X\n",via.ifr);
    return via.ifr;
  }
  return 0xFF;
}

void resetvia()
{
  via.ora = 0x80;
  via.ifr = via.ier = 0;
  via.t1c = via.t1l = 0x1FFFE;
  via.t2c = via.t2l = 0x1FFFE;
  via.t1hit = via.t2hit = 1;
  timerout = 1;
  via.acr = 0;

  // To make sure interrupts get cleared at reset
  updateIFR();
}

void dumpvia()
{
  // Serial.printf("T1 = %04X %04X T2 = %04X %04X\n", via.t1c, via.t1l, via.t2c, via.t2l);
  // Serial.printf("%02X %02X  %02X %02X\n", via.ifr, via.ier, via.pcr, via.acr);
}

void atom_reset(int power_on)
{
  // Serial.println("atom_reset");

  resetvia();
  reset6502();
  InitMMC();
  // Serial.println("atom_reset():done");
}

int RamEnabled(uint16_t addr)
{
  int result;

  result = ((addr < 0x400) ||                                              // Always RAM
            ((addr >= 0x0400) && (addr < 0x09FF) && (main_ramflag > 3)) || // Low ram
            ((addr >= 0x0B00) && (addr < 0x1FFF) && (main_ramflag > 3)) || // Low ram
            ((addr >= 0x0A00) && (addr < 0x0AFF) && (main_ramflag > 4)) || // Low ram + RAM in disk area
            ((addr >= 0x2800) && (addr < 0x3C00) && (main_ramflag > 0)) || // 5K on motherboard
            ((addr >= 0x2000) && (addr < 0x2800) && (main_ramflag > 1)) || // DOS additional 3K
            ((addr >= 0x3C00) && (addr < 0x4000) && (main_ramflag > 1)) ||
            ((addr >= 0x4000) && (addr < 0x8000) && (main_ramflag > 2)) || // Extra 16K
            ramrom_enable);                                                // Always enabled for RAMROM.
  return result;
}

uint8_t readmeml(uint16_t addr)
{
  uint8_t addrh = (addr >> 8); // It seems all unmapped addresses just return the high byte of the address....
  int ram_enabled;

  ram_enabled = RamEnabled(addr);

  // LB To do: wat doet de atomulator hier. Snap hem nog niet..
  /*  if (pc == addr)
  {
    fetchc[addr] = 31;
  }
  else
  {
    readc[addr] = 31;
  }
*/
  int i = (addr & 0xFC00);

  if (i == 0x0800)
  {
    if ((addr & 0x0F00) == 0x0A00) /*FDC*/
    {
      if ((ramrom_enable && RR_BLKA_enabled()) ||
          (!ramrom_enable && ram_enabled))
        return ram[addr];
      // LB else
      // LB   return read8271(addr);                  /*FDC*/
    }
    else
      return ram_enabled ? ram[addr] : addrh;
  }

  if (i <= 0x6C00)
    return ram_enabled ? ram[addr] : addrh; /* RAM, DOS RAM */

  if (i <= 0x7C00)
  {
    if (!ramrom_enable || (RR_enables & RAMROM_FLAG_EXTRAM) == 0)
      return ram[addr];

    return ram_enabled ? ram[addr] : addrh;
    ; // dont let the code execution fall through
  }

  if (i <= 0x9C00) /* Video RAM */
  {
    if (cycles >= 0 && cycles < 32)
      fetcheddat[31 - cycles] = ram[addr];

    if (addr < vid_top)
      return ram[addr];
    else
      return addrh;
  }

  if (i == 0xB000)
  {
    int i;
    i = 0;
    int temp = read8255(addr); /* 8255 PIA */
                               /*  if (addr & 3)
                                 { // 290824 lb
                                   if (temp != 255 && temp != 247 && temp != 119 )
                                   {
                                     M5.Lcd.setCursor(160, 0);
                                     M5.Lcd.printf("temp: %d", temp);
                                   }
                                 } */
    return temp;
  }
  if (i == 0xB400)
  {
    unsigned char dato = ReadMMC(addr);
    // Serial.printf("Read Adres: %04X, data: %02X\r\n", addr, dato);
    return dato; /* MMC      */
  }
  if (i == 0xB800)
    return readvia(addr); /* 6522 VIA */
  if (i == 0xBC00)
  {
    // GDOS 2015, BC10.
    // LB if ((fdc1770) && (addr >= WDBASE) && (addr <= CTRLREG))
    // LB return read1770(addr);

    // LB if ((sndatomsid) && (addr >= 0xBDC0) && (addr <= 0xBDDF))
    // LB   return sid_read(addr & 0x1F);

    // Serial.printf("ramrom_enable:%x, %04X, val: %d\n", ramrom_enable, addr, (RR_bankreg & 0x0F));
    if (ramrom_enable)
    {
      switch (addr)
      {
      case 0xBFFF:
        return (0xB0 | (RR_bankreg & 0x0F));
      case 0xBFFE:
        return (0xB0 | (RR_enables & 0x0F));
      case 0xBFFD:
        return (0xB0 | (RR_jumpers & 0x0F));
      default:
        return 0xBF;
      }
    }

    return addrh; // don't let the code execution fall through
  }

  if (i <= 0xAC00)
  {
    // Serial.printf("fall through: %x\n", i);
    return utility_ptr[addr & 0x0FFF]; /* Utility ROM        */
  }
  if (i <= 0xCC00)
    return abasic_ptr[addr & 0x0FFF]; /* BASIC              */
  if (i <= 0xDC00)
    return afloat_ptr[addr & 0x0FFF]; /* Floating point ROM */
  if (i <= 0xEC00)
    return dosrom_ptr[addr & 0x0FFF]; /* Disc ROM           */
  return akernel_ptr[addr & 0x0FFF];  /* Kernel             */
}

void writememl(uint16_t addr, uint8_t val)
{
  int ram_enabled;

  ram_enabled = RamEnabled(addr);

  if (debugon)
  {
    // Serial.printf("Adres: %04x, data: %02x\r\n", addr, val);
  }

  int i = (addr & 0xFC00);

  if (i == 0x0800)
  {
    if ((addr & 0x0F00) == 0x0A00) /*FDC*/
    {
      if ((ramrom_enable && RR_BLKA_enabled()) ||
          (!ramrom_enable && ram_enabled))
      {
        if (ram_enabled)
          ram[addr] = val;
      }
      // LB else
      // LB   write8271(addr, val);                  /*FDC*/
      return;
    }
    else
    {
      if (ram_enabled)
        ram[addr] = val;
      return;
    }
  }

  if (i <= 0x6C00)
  {
    if (ram_enabled)
      ram[addr] = val; /* RAM, DOS RAM */
    return;
  }

  if (i <= 0x7C00)
  {
    if ((!ramrom_enable || (RR_enables & RAMROM_FLAG_EXTRAM) == 0) ||
        (ram_enabled && !ramrom_enable))
      ram[addr] = val;

    return;
  }

  if (i <= 0x9C00) /* Video RAM */
  {
    if (cycles >= 0 && cycles < 32)
      fetcheddat[31 - cycles] = val;

    if (addr < vid_top)
      ram[addr] = val;

    return;
  }

  if (i == 0xB000)
  {
    write8255(addr, val); /* 8255 PIA */
    return;
  }

  if (i == 0xB400)
  {
    // Serial.printf("Write Adres: %04X, data: %02X\r\n", addr, val);
    WriteMMC(addr, val); /* MMC      */
    return;
  }

  if (i == 0xB800)
  {
    if (addr < 0xB810)
      writevia(addr, val); /* 6522 VIA */
    return;
  }

  if (i == 0xBC00)
  {
    if (ramrom_enable)
    {
      if (addr == 0xBFFF)
      {
        RR_bankreg = val & 0x0F;
        set_rr_ptrs();
      }

      if (addr == 0xBFFE)
      {
        RR_enables = val & 0x0F;
        set_rr_ptrs();
      }
    }
    return;
  }

  if (i <= 0xAC00) /* Utility ROM        */
  {
    // Special case of RAM mapped into utility rom space and rom bank 0 selected

    if (ramrom_enable && (RR_enables & RAMROM_FLAG_EXTRAM) && (RR_bankreg == 0))
      utility_ptr[addr & 0x0FFF] = val;
    return;
  }

  // Otherwise a ROM - BASIC, Floating Point, Disc or Kernel
  return;
}

// Re-written for 1.20 to reflect latest RAMROM ROM Layout - see roms.h
void set_rr_ptrs()
{
  int c;

  // Leave the pointers alone if ramrom is disabled
  if (ramrom_enable)
  {
    //    Serial.printf("Running with Ramoth RAMROM roms\n");
    //    Serial.printf("RR_enables=%2X, RR_bankreg=%2X\n", RR_enables, RR_bankreg);

    // Atom Mode
    if (RR_bit_set(RAMROM_FLAG_DISKROM))
    {
      // Atom Mode with DISKROM = 1
      // Serial.printf("Running with Atom mode roms, diskrom flag = 1\n");
      // utility_ptr = &barawData[0];      //(ROM_OFS_UTILITY];
      abasic_ptr = &barawData[0x8000];  // ROM_OFS_ABASIC];
      afloat_ptr = &barawData[0x9000];  // ROM_OFS_AFLOAT];
      dosrom_ptr = &barawData[0xa000];  // mmc
      akernel_ptr = &barawData[0xb000]; // ROM_OFS_AKERNEL];
    }
    else
    {
      // Atom Mode with DISKROM = 0
      // Serial.printf("Running with Atom mode roms, diskrom flag = 0\n");
      abasic_ptr = &barawData[0xc000];  // ROM_OFS_ABASIC];
      afloat_ptr = &barawData[0xd000];  // ROM_OFS_AFLOAT];
      akernel_ptr = &barawData[0xf000]; // ROM_OFS_AKERNEL];
      set_dosrom_ptr();                 // wat overbodig..
    }
    // Select what is mapped into $A000 block, this allows the
    // mapping of rom or ram into the utility block so that a
    // rom may be loaded from disk and then mapped in.
    if ((RR_enables & RAMROM_FLAG_EXTRAM) && (RR_bankreg == 0))
    {
      utility_ptr = &ram[0x7000];
    }
    else
    {
      // Serial.printf("optelling ROM data ofset util rom: %x, bankregs: %d\n", (RR_bankreg * ROM_SIZE_ATOM), RR_bankreg);
      utility_ptr = &barawData[RR_bankreg * ROM_SIZE_ATOM];
      // Serial.printf("utility_ptr = 0x%05x\n", utility_ptr);
    }
  }
  else
  {
    //  Serial.printf("Running with standard roms\n");
    utility_ptr = &barawData[0];      //(ROM_OFS_UTILITY];
    abasic_ptr = &barawData[0x8000];  // ROM_OFS_ABASIC];
    afloat_ptr = &barawData[0xa000];  // ROM_OFS_AFLOAT];
    akernel_ptr = &barawData[0xc000]; // ROM_OFS_AKERNEL];
    set_dosrom_ptr();
  }
}

void set_dosrom_ptr()
{
  /*
  if (!ramrom_enable || (ramrom_enable && !RR_bit_set(RAMROM_FLAG_BBCMODE) && !RR_bit_set(RAMROM_FLAG_DISKROM)))
    dosrom_ptr = fdc1770 ? &rom[ROM_OFS_GDOS2015 + (GD_bank * ROM_SIZE_ATOM)] : &rom[ROM_OFS_DOSROM];
    */
  dosrom_ptr = &barawData[0xa000]; // ROM_OFS_AFLOAT];
                                   // Serial.println("setdosrom pointer gedoe");
}

// Re-written for 1.20 to reflect latest RAMROM ROM Layout - see roms.h
void reset_rom()
{
  // Serial.printf("reset_rom(), ramrom=%d, bbcmode=%d\n", ramrom_enable, RR_bit_set(RAMROM_FLAG_BBCMODE));
  set_rr_ptrs();
  // Serial.printf("reset_rom():done\n");
}
void plot23(int x, int y, int color)
{
}

void plot22(int x, int y, int color)
{
}

void plot21(int x, int y, int color)
{
}

// Table of when to increment the address (every N lines)
static int lines_per_row[16] = {
    12,
    3, // 1a   64x64 in 4 cols
    12,
    3, // 1   128x64 in 2 cols
    12,
    3, // 2a  128x64 in 4 cols
    12,
    2, // 2   128x96 in 2 cols
    12,
    2, // 3a  128x96 in 4 cols
    12,
    1, // 3  128x192 in 2 cols
    12,
    1, // 4a 128x192 in 4 cols
    12,
    1 // 4  256x192 in 2 cols
};

static int bytes_per_row[16] = {
    32,
    16, // 1a   64x64 in 4 cols
    32,
    16, // 1   128x64 in 2 cols
    32,
    32, // 2a  128x64 in 4 cols
    32,
    16, // 2   128x96 in 2 cols
    32,
    32, // 3a  128x96 in 4 cols
    32,
    16, // 3  128x192 in 2 cols
    32,
    32, // 4a 128x192 in 4 cols
    32,
    32 // 4  256x192 in 2 cols
};

static int mask_per_row[16] = {
    0xFFE0,
    0xFFF0, // 1a   64x64 in 4 cols
    0xFFE0,
    0xFFF0, // 1   128x64 in 2 cols
    0xFFE0,
    0xFFE0, // 2a  128x64 in 4 cols
    0xFFE0,
    0xFFF0, // 2   128x96 in 2 cols
    0xFFE0,
    0xFFE0, // 3a  128x96 in 4 cols
    0xFFE0,
    0xFFF0, // 3  128x192 in 2 cols
    0xFFE0,
    0xFFE0, // 4a 128x192 in 4 cols
    0xFFE0,
    0xFFE0 // 4  256x192 in 2 cols
};

void drawline(int line)
{
  int chr, col;
  int x, xx;
  uint8_t temp;
  static unsigned int addr = 0x8000;
  if (!line)
  {
    addr = 0x8000;
    // vbl =
    cy = sy = 0;
  }
  if (line < 192)
  {

    // For repeated lines, decrement the address pointer
    if ((line % lines_per_row[gfxmode]) != 0)
    {
      addr -= bytes_per_row[gfxmode];
    }

    // Make sure address pointer has the right alignment
    addr &= mask_per_row[gfxmode];

    switch (gfxmode)
    {
      /*
       case 0:
       case 2:
       case 4:
       case 6: //Text mode
       case 8:
       case 10:
       case 12:
       case 14:
         for (x = 0; x < 256; x += 8)
         {
           chr = ram[addr + (x >> 3)];
           if (chr & 0x40)
           {
             temp = chr;
             chr <<= ((sy >> 2) << 1);
             chr = (chr >> 4) & 3;
             if (chr & 2)
               col = semigrcol[(temp >> 6) | (css << 1)];
             else
               col = 0;
             // b->line[line][x] = b->line[line][x + 1] = b->line[line][x + 2] = b->line[line][x + 3] = col;
             for (int i = 0; i < 4; i++)
             {
               dotFast(x + xx + i, line, col);
             }

             if (chr & 1)
               col = semigrcol[(temp >> 6) | (css << 1)];
             else
               col = 0;
             // b->line[line][x + 4] = b->line[line][x + 5] = b->line[line][x + 6] = b->line[line][x + 7] = col;
             for (int i = 4; i < 8; i++)
             {
               dotFast(x + xx + i, line, col);
             }
           }
           else
           {
             chr = ((chr & 0x3F) * 12) + sy;
             if (ram[addr + (x >> 3)] & 0x80)
             {
               for (xx = 0; xx < 8; xx++)
               {
                 // b->line[line][x + xx] = textcol[(((fontdata[chr] >> (xx ^ 7)) & 1) ^ 1) | css];
                 dotFast(x + xx, line, textcol[(((fontdata[chr] >> (xx ^ 7)) & 1) ^ 1) | css]);
               }
             }
             else
             {
               for (xx = 0; xx < 8; xx++)
               {
                 // b->line[line][x + xx] = textcol[((fontdata[chr] >> (xx ^ 7)) & 1) | css];
                 dotFast(x + xx, line, textcol[((fontdata[chr] >> (xx ^ 7)) & 1) | css]);
               }
             }
           }
         }
         sy++;
         if (sy == 12)
         {
           sy = 0;
           cy++;
         }
         break;
   */
      /*
  addr = (cy << 5) + 0x8000;
  addr = lines * 32; // 32 bytes is 256 puntjes

  for (x = 0; x < 256; x += 8)
  {
    chr = fetcheddat[x >> 3];
    if (chr & 0x40)
    {
      temp = chr;
      chr <<= ((sy >> 2) << 1);
      chr = (chr >> 4) & 3;
      if (chr & 2)
        col = semigrcol[(temp >> 6) | (css << 1)];
      else
        col = 0;
      //  b->line[line][x] = b->line[line][x + 1] = b->line[line][x + 2] = b->line[line][x + 3] = col;
      if (chr & 1)
        col = semigrcol[(temp >> 6) | (css << 1)];
      else
        col = 0;
      //  b->line[line][x + 4] = b->line[line][x + 5] = b->line[line][x + 6] = b->line[line][x + 7] = col;
    }
    else
    {
      chr = ((chr & 0x3F) * 12) + sy;
      if (fetcheddat[x >> 3] & 0x80)
      {
        int lb;
        for (xx = 0; xx < 8; xx++)
        {
          //      b->line[line][x + xx]
          // lb = textcol[(((fontdata[chr] >> (xx ^ 7)) & 1) ^ 1) | css];
        }
      }
      else
      {
        int lb;
        for (xx = 0; xx < 8; xx++)
        {

          //      b->line[line][x + xx] =
          //     lb = textcol[((fontdata[chr] >> (xx ^ 7)) & 1) | css];
        }
      }
    }
  }
  sy++;
  if (sy == 12)
  {
    sy = 0;
    cy++;
  }
  addr = (cy << 5) + 0x8000;
  for (x = 0; x < 32; x++)
    fetcheddat[x] = ram[addr++];
  break;
  */

      // Propper graphics modes
      /*
        case 1:         // 64x64, 4 colours
        for (x = 0; x < 256; x += 16)
        {
        temp = fetcheddat[x >> 3];
        for (xx = 0; xx < 16; xx += 4)
        {
          b->line[line][x + xx] = b->line[line][x + xx + 1] = b->line[line][x + xx + 2] = b->line[line][x + xx + 3] = semigrcol[(temp >> 6) | (css << 1)];
          temp <<= 2;
        }
        }

        addr = (((line + 1) / 3) << 4) | 0x8000;
        for (x = 0; x < 32; x++)
        fetcheddat[x] = ram[addr + (x >> 1)];

        break;
*/
    case 3: // 128x64, 2 colours
    {
      int cnt = 0x8000 + (line * (128 / 8));
      unsigned ch;
      for (int px = 0; px < (128 / 8); px++)
      {
        int nIndex = (line * (128 / 8)) + px;
        ch = ram[cnt++];
        if (cnt < (0x8000 + 0x400))
        {
          if (svideo[nIndex] != ch)
          {
            svideo[nIndex] = ch;
            for (int py = 0; py < 8; py++) // HIER ZET HIJ 8 PUNTEN IN ZW EN DUS 4 IN CLEAR A
            {
              int lb = ((px * 8) + py); // px pakt steeds 0, 8, 16 en py vult de tussenliggende waardes.

              if (ch & (1 << (7 - py))) // de byte in kwestie staat in ch. Met deze AND wordt er 1 bit uitgezocht.
              {
                plot23(lb, line, 0);
                // dotFast(lb, line, 0);
              }
              else
              {
                plot23(lb, line, 4);
                // dotFast(lb, line, 4);
              }
            }
          }
        }
      }
    }
    break;

      //* PATCH FOR CORRECT CLEAR2a

    case 5: // 128x64, 4 colours

      for (x = 0; x < 256; x += 8)
      {
        temp = fetcheddat[x >> 3];
        for (xx = 0; xx < 8; xx += 2)
        {
          // b->line[line][x + xx] = b->line[line][x + xx + 1] = semigrcol[(temp >> 6) | (css << 1)];
          // functie aanroepen met line en byte nieuw, byte oud, px (welke byte)
          // the line of the framebuffer is 120 bytes long, however, we use only (in Atom mode) 96, since Clear 4 has only 192 points
          // So each bit of a byte (nwch) is spread over 4 bits of a byte in the scan line.
          // for now, we use only on and of, which is 0x00 or 0x20 or 0x,22, or 0x02 for the two green bits.
          // drawl(line, svideo[nIndex], ch, px);

          // drawll(line, x + xx, semigrcol[(temp >> 6) | (css << 1)]);
          // drawll(line, x + xx + 1, semigrcol[(temp >> 6) | (css << 1)]);

          temp <<= 2;
        }
      }

      addr = (((line + 1) / 3) << 5) | 0x8000;
      for (x = 0; x < 32; x++)
        fetcheddat[x] = ram[addr + x];

      break;

      // PATCH CHANGES

    case 7: // 192x96, 2 colours clear 2
    {
      int cnt = 0x8000 + (line * (128 / 8));
      unsigned ch;
      for (int px = 0; px < (128 / 8); px++)
      {
        int nIndex = (line * (128 / 8)) + px;
        ch = ram[cnt++];
        if (cnt < (0x8000 + 0x600))
        {
          if (svideo[nIndex] != ch)
          {
            svideo[nIndex] = ch;
            for (int py = 0; py < 8; py++) // HIER ZET HIJ 8 PUNTEN IN ZW EN DUS 4 IN CLEAR A
            {
              int lb = ((px * 8) + py); // px pakt steeds 0, 8, 16 en py vult de tussenliggende waardes.

              if (ch & (1 << (7 - py))) // de byte in kwestie staat in ch. Met deze AND wordt er 1 bit uitgezocht.
              {
                plot22(lb, line, 0);
                // dotFast(lb, line, 0);
              }
              else
              {
                plot22(lb, line, 4);
                // dotFast(lb, line, 4);
              }
            }
          }
        }
      }
    }
    break;
    case 9: // 128x96, 4 colours Clear 3a
    {
      int cnt = 0x8000 + (line * 32), lb;
      unsigned ch, chh;
      for (int px = 0; px < 32; px++) // aantal bytes *8 op de x as 32=256, 1 bits kleur; 32=128 met 2 bits kleur
      {
        int nIndex = (line * 32) + px; // idem als boven.
        ch = ram[cnt++];
        if (cnt <= 0x8c00)
        {
          if (svideo[nIndex] != ch)
          {
            svideo[nIndex] = ch;
          }
        }
      }
    }
    break;

    case 11: // 192x128, 2 colours
    {
      int cnt = 0x8000 + (line * (128 / 8));
      unsigned ch;
      for (int px = 0; px < (128 / 8); px++)
      {
        int nIndex = (line * (128 / 8)) + px;
        ch = ram[cnt++];
        if (cnt < (0x8000 + 0xC00))
        {
          if (svideo[nIndex] != ch)
          {
            svideo[nIndex] = ch;
            for (int py = 0; py < 8; py++) // HIER ZET HIJ 8 PUNTEN IN ZW EN DUS 4 IN CLEAR A
            {
              int lb = ((px * 8) + py); // px pakt steeds 0, 8, 16 en py vult de tussenliggende waardes.

              if (ch & (1 << (7 - py))) // de byte in kwestie staat in ch. Met deze AND wordt er 1 bit uitgezocht.
              {
                plot21(lb, line, 0);
                // dotFast(lb, line, 0);
              }
              else
              {
                plot21(lb, line, 4);
                // dotFast(lb, line, 4);
              }
            }
          }
        }
      }
    }
    break;
    case 13: // 128x192, 4 colours
      break;
      /*
        for (x = 0; x < 256; x += 8)
        {
        temp = fetcheddat[x >> 3];
        for (xx = 0; xx < 8; xx += 2)
        {
          b->line[line][x + xx] = b->line[line][x + xx + 1] = semigrcol[(temp >> 6) | (css << 1)];
          temp <<= 2;
        }
        }

        addr = ((line + 1) << 5) | 0x8000;

        for (x = 0; x < 32; x++)
        fetcheddat[x] = ram[addr + x];

        break;
      */
    case 15: // 256x192, 2 colours
    {
      /*   //  Adjusted plot to improve on speed.
         static long tijdoud = 0;
         //     static unsigned short crcOud[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
         bool bDirty[24];
         for (int i = 0; i < 24; i++)
         {
           bDirty[i] = false;
         }

         if (millis() - tijdoud > 20)
         {
           for (int j = 0; j < 24; j++)
           {
             for (int k = 0; k < 256; k++)
             {
               if (svideo[k + (j * 256)] != ram[0x8000 + k + (j * 256)])
               {
                 bDirty[j] = true;
                 for (int m = k; m < 256; m++)
                 {
                   svideo[m + (j * 256)] = ram[0x8000 + m + (j * 256)];
                 }
                 break;
               }
               // svideo[k + (j * 256)] = ram[0x8000 + k + (j * 256)];
               // memcpy(&svideo[k + (j * 256)], &ram[0x8000 + k + (j * 256)], 1);
             }
           }

           s1.b = 4;
           for (int j = 0; j < 24; j++)
           {
             if (bDirty[j] == true)
             {
               s1.Page = 0x80 + j;
               memcpy(&s1.PageData, &ram[0x8000 + (j * 256)], 256);
               Serial.write(&s1.b, sizeof(s1));
               do
               {
                 yield();
               } while (Serial.availableForWrite() != 128);
               delay(10);
               //   gemen(j);
             }
           }
           tijdoud = millis();
         }
         */
    }

    break;

      //                        default:
      //                        Serial.printf("Bad GFX mode %i\n",gfxmode);
      //                        dumpregs();
      //                        dumpram();
      //                        exit(-1);
    }
    // After every line, increment the address pointer
    addr += bytes_per_row[gfxmode];
  }
  /*
    if (line == 192)
    {
      vbl = 1;
    }
    if (line == 224)
      vbl = 0;
      */
}

unsigned char calculate_checksum(unsigned char *array, size_t length)
{
  unsigned int sum = 0;
  for (size_t i = 0; i < length; ++i)
  {
    sum += array[i];
  }
  return (unsigned char)(sum % 256);
}

unsigned short crc32(unsigned short crc, const unsigned char *buf, size_t len)
{
  static unsigned short table[256];
  static int have_table = 0;
  unsigned short rem;
  unsigned char octet;
  int i, j;
  const unsigned char *p, *q;

  /* This check is not thread safe; there is no mutex. */
  if (have_table == 0)
  {
    for (i = 0; i < 256; i++)
    {
      rem = i; /* remainder from polynomial division */
      for (j = 0; j < 8; j++)
      {
        if (rem & 1)
        {
          rem >>= 1;
          rem ^= 0xedb88320;
        }
        else
          rem >>= 1;
      }
      table[i] = rem;
    }
    have_table = 1;
  }

  crc = ~crc;
  q = buf + len;
  for (p = buf; p < q; p++)
  {
    octet = *p;
    crc = (crc >> 8) ^ table[(crc & 0xff) ^ octet];
  }
  return ~crc;
}

// huffman gedoe

int gemeen(int n)
{
}
int gemen(int n)
{
  int arr1[256], top = 0;
  int aantalbits = 0;
  int index = 0;
  unsigned char arr[256];  // = { 'a', 'b', 'c', 'd', 'e', 'f' };
  int freq[256];           // = { 5, 9, 12, 13, 16, 45 };
  unsigned char harr[256]; // = { 'a', 'b', 'c', 'd', 'e', 'f' };
  int hfreq[256];          // = { 5, 9, 12, 13, 16, 45 };
  unsigned char j;
  int aantal = 0;

  for (j = 0; j < 255; j++)
  {
    arr[j] = 0;
    freq[j] = 0;
  }

  for (j = 0; j < 255; j++)
  {
    for (int k = 0; k < (255 * 1); k++)
    {
      if (s1.PageData[k] == j)
      {
        arr[j] = j;
        freq[j]++;
        index++;
      }
    }
  }

  // printf("%d: \n", index);

  for (j = 0; j < 255; j++)
  {
    if (freq[j] != 0)
    {
      //   printf("#%d, Waarde: %02X, aantal: %d\r\n", aantal, arr[j], freq[j]);
      hfreq[aantal] = freq[j];
      harr[aantal] = arr[j];
      aantal++;
    }
  }
  // int size = sizeof(arr) / sizeof(arr[0]);

  struct MinHeapNode *root = buildHuffmanTree(harr, hfreq, aantal);
  AantalBitsx = 0;
  printCodes(root, arr1, top);
  for (int j = 0; j < aantal; j++)
  {
    for (int i = 0; i < aantal; i++)
    {
      int m = harr[j];
      int n = nChar[i];
      if (nChar[i] == harr[j])
      {
        //  m5.Lcd.printf("Iwaarde:%d, %02x: %d, freq: %d, %02X\n", i, nChar[i], nAantalbits[i], hfreq[j], harr[j]);
        aantalbits += (nAantalbits[i] * hfreq[j]);
      }
    }
  }

  // M5.Lcd.setCursor(((n % 2) * 120), 0 + ((n / 2) * 13));
  // M5.Lcd.printf("%02d: b: %03d ", n, aantalbits / 8);

  return (aantalbits / 8);
}

// Functie om Huffman-codering uit te voeren
void HuffmanCodes(unsigned char data[], int freq[], int size)
{
  struct MinHeapNode *root = buildHuffmanTree(data, freq, size);
  freeTree(root);
  //  printf("%x", StatHeap);
  free(StatHeap->array);
  free(StatHeap);
}

// Functie om een array van grootte n af te drukken
void printArr(int arr[], int n)
{
  int i;
  for (i = 0; i < n; ++i)
    printf("%d", arr[i]);
  printf("\n");
}

// Functie om te controleren of deze knoop een blad is
int isLeaf(struct MinHeapNode *root)
{
  return !(root->left) && !(root->right);
}
// Functie om de Huffman-codes te genereren en af te drukken
void printCodes(struct MinHeapNode *root, int arr[], int top)
{
  // AantalBitsx = 0;
  // m5.Lcd.print("*");
  if (root->left)
  {
    arr[top] = 0;
    printCodes(root->left, arr, top + 1);
  }

  if (root->right)
  {
    arr[top] = 1;
    printCodes(root->right, arr, top + 1);
  }

  if (isLeaf(root))
  {
    //      printf("%02X: ", root->data);
    //  printArr(arr, top);
    nAantalbits[AantalBitsx] = top;
    nChar[AantalBitsx++] = root->data;
  }
}
// Functie om twee min-heap knopen te verwisselen
void swapMinHeapNode(struct MinHeapNode **a, struct MinHeapNode **b)
{
  struct MinHeapNode *t = *a;
  *a = *b;
  *b = t;
}

// Standaard minHeapify functie
void minHeapify(struct MinHeap *minHeap, int idx)
{
  int smallest = idx;
  int left = 2 * idx + 1;
  int right = 2 * idx + 2;

  if (left < minHeap->size && minHeap->array[left]->freq < minHeap->array[smallest]->freq)
    smallest = left;

  if (right < minHeap->size && minHeap->array[right]->freq < minHeap->array[smallest]->freq)
    smallest = right;

  if (smallest != idx)
  {
    swapMinHeapNode(&minHeap->array[smallest], &minHeap->array[idx]);
    minHeapify(minHeap, smallest);
  }
}

// Functie om te controleren of de grootte van de heap 1 is
int isSizeOne(struct MinHeap *minHeap)
{
  return (minHeap->size == 1);
}

// Functie om de minimum waarde knoop uit de heap te halen
struct MinHeapNode *extractMin(struct MinHeap *minHeap)
{
  struct MinHeapNode *temp = minHeap->array[0];
  minHeap->array[0] = minHeap->array[minHeap->size - 1];
  --minHeap->size;
  minHeapify(minHeap, 0);
  return temp;
}

// Functie om een nieuwe min-heap knoop te maken
struct MinHeapNode *newNode(unsigned char data, unsigned freq)
{
  struct MinHeapNode *temp = (struct MinHeapNode *)malloc(sizeof(struct MinHeapNode));
  temp->left = temp->right = NULL;
  temp->data = data;
  temp->freq = freq;
  return temp;
}

// Functie om te decoderen
void decode(struct MinHeapNode *root, char *encodedStr)
{
  struct MinHeapNode *current = root;
  for (int i = 0; encodedStr[i] != '\0'; i++)
  {
    if (encodedStr[i] == '0')
      current = current->left;
    else
      current = current->right;

    if (!current->left && !current->right)
    {
      //   printf("%02X.", current->data);
      current = root;
    }
  }
  //  printf("\n");
}

// Functie om een nieuwe knoop toe te voegen aan de min-heap
void insertMinHeap(struct MinHeap *minHeap, struct MinHeapNode *minHeapNode)
{
  ++minHeap->size;
  int i = minHeap->size - 1;

  while (i && minHeapNode->freq < minHeap->array[(i - 1) / 2]->freq)
  {
    minHeap->array[i] = minHeap->array[(i - 1) / 2];
    i = (i - 1) / 2;
  }
  minHeap->array[i] = minHeapNode;
}

// Functie om een min-heap te bouwen
void buildMinHeap(struct MinHeap *minHeap)
{
  int n = minHeap->size - 1;
  int i;
  for (i = (n - 1) / 2; i >= 0; --i)
  {
    minHeapify(minHeap, i);
  }
}

// Functie om een min-heap te maken van gegeven capaciteit
struct MinHeap *createMinHeap(unsigned capacity)
{
  struct MinHeap *minHeap = (struct MinHeap *)malloc(sizeof(struct MinHeap));
  // m5.lcd.printf("%X", minHeap);
  minHeap->size = 0;
  minHeap->capacity = capacity;
  minHeap->array = (struct MinHeapNode **)malloc(minHeap->capacity * sizeof(struct MinHeapNode *));
  return minHeap;
}

// Functie om een min-heap te maken en te vullen met gegeven data
struct MinHeap *createAndBuildMinHeap(unsigned char data[], int freq[], int size)
{
  struct MinHeap *minHeap = createMinHeap(size);

  for (int i = 0; i < size; ++i)
  {
    minHeap->array[i] = newNode(data[i], freq[i]);
  }

  minHeap->size = size;
  buildMinHeap(minHeap);

  return minHeap;
}
void freeTree(struct MinHeapNode *minHeap)
{
  if (minHeap == NULL)
  {
    return;
  }
  if (minHeap->left != NULL)
  {
    //      printf("Free %x\n", minHeap->left);
    freeTree(minHeap->left);
  }
  if (minHeap->right != NULL)
  {
    //        printf("Free %x\n", minHeap->right);
    freeTree(minHeap->right);
  }
  //    printf("Real free %x\n", minHeap);
  free(minHeap);
}
// Functie om de Huffman-boom te bouwen (zoals eerder gegeven)
struct MinHeapNode *buildHuffmanTree(unsigned char data[], int freq[], int size)
{
  struct MinHeapNode *left, *right, *top;
  struct MinHeap *minHeap = createAndBuildMinHeap(data, freq, size);
  StatHeap = minHeap;
  while (!isSizeOne(minHeap))
  {
    left = extractMin(minHeap);
    right = extractMin(minHeap);

    top = newNode('$', left->freq + right->freq);
    top->left = left;
    top->right = right;

    insertMinHeap(minHeap, top);
  }

  return extractMin(minHeap);
}