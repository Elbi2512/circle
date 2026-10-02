#include <Arduino.h>
// #include <M5Core2.h>
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"
#include "SD.h"
#include "SPI.h"
#include "def/msg.h"
#include "def\hardware.h"
#include "def/hardware.h"
#include <string.h>
#include "atommc/diskio.h"
#include "atommc/ff.h"
#include "atommc/atmmc2.h"
#include "atommc/atmmc2def.h"
#include "atommc/wildcard.h"
#include "atommc/atmmc2io.h"
#include "atom.h"
#include "roms.h"
#include "FS.h"
#include "fontdataa.h"
#include "Ticker.h"

// #include <esp_bt.h>
#include "driver/timer.h"
#include "soc/timer_group_struct.h"
#include "atom.h"
#include "PS2Keyboard.h"
// #define DEBUG

const int enPin = 0;          // De EN-knop is verbonden met GPIO 0
static uint32_t scanTime = 0; /** 0 = scan forever */
void loep();
#define SCREENWIDTH ILI9341_TFTHEIGHT // Native display orientation is
#define SCREENHEIGHT ILI9341_TFTWIDTH // vertical, so swap width/height

Ticker vduer;
unsigned char video[6 * 1024];
void do_keyboard(uint8_t, u_int8_t);
extern unsigned short crc32(unsigned short crc, const unsigned char *buf, size_t len);

extern int fileOpen(char *);
extern int filenum;
extern int drawscr;
extern File existFile(fs::FS &fs, const char *path);

TaskHandle_t Task1;
TaskHandle_t Task2;

hw_timer_t *timer = NULL;

volatile bool bHz = false;
volatile unsigned char nHz;

portMUX_TYPE timerMux0 = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE timerMux1 = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE timerMux2 = portMUX_INITIALIZER_UNLOCKED;
volatile u_int8_t scanline = 0;
volatile uint8_t vbl; // 60Hz
hw_timer_t *SyncTimer = NULL;
hw_timer_t *ScanlineTimer = NULL;

void IRAM_ATTR timerInterrupt()
{
  portENTER_CRITICAL_ISR(&timerMux2);
  bHz = true;
  if (nHz++ > 24)
  {
    nHz = 0;
  }
  portEXIT_CRITICAL_ISR(&timerMux2);
}

void IRAM_ATTR onTimerSync()
{
  portENTER_CRITICAL_ISR(&timerMux0);
  scanline = 0;
  vbl = 0;
  portEXIT_CRITICAL_ISR(&timerMux0);
}

void IRAM_ATTR onTimerScanline()
{
  portENTER_CRITICAL_ISR(&timerMux1);
  if (scanline++ > 192)
  {
    vbl = 1;
  }
  else
    vbl = 0;
  portEXIT_CRITICAL_ISR(&timerMux1);
}

// extern uint8_t *rom;
File dir;
int numTabs = 1;
// screen size in portrait mode
#define LX 240
#define LY 320

extern int lines;
// extern void drawlines(int);
extern uint8_t *ram; //, *svideo; //, *rom;
// Pins for PS/2 Interface (some USB keyboards WORK)
static const int DATA_PIN = 32;  // USB D-;
static const int CLOCK_PIN = 33; // USB D+;
uint8_t shift, ctrl, alt;

extern void initmem();
extern void loadroms();
extern void reset6502();
extern void init8255();
int debugon = true;
extern void resetvia();
//
extern void atom_reset(int);
extern void atom_run();

struct sizeType
{
  unsigned long local = 0;
  unsigned long complete = 0;
};

byte key[128];
struct AtomFrame
{
  unsigned char b;
  unsigned char Page;
  unsigned char crcL;
  unsigned char crcH;
  unsigned char PageData[256];
};
struct AtomFrame s1;
void feedTheDog()
{
  // feed dog 0
  TIMERG0.wdt_wprotect = TIMG_WDT_WKEY_VALUE; // write enable
  TIMERG0.wdt_feed = 1;                       // feed dog
  TIMERG0.wdt_wprotect = 0;                   // write protect
  // feed dog 1
  TIMERG1.wdt_wprotect = TIMG_WDT_WKEY_VALUE; // write enable
  TIMERG1.wdt_feed = 1;                       // feed dog
  TIMERG1.wdt_wprotect = 0;                   // write protect
}

/*
void llistDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels)
      {
        llistDir(fs, file.name(), levels - 1);
      }
    }
    else
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void lcreateDir(fs::FS &fs, const char *path)
{
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path))
  {
    Serial.println("Dir created");
  }
  else
  {
    Serial.println("mkdir failed");
  }
}

void lremoveDir(fs::FS &fs, const char *path)
{
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path))
  {
    Serial.println("Dir removed");
  }
  else
  {
    Serial.println("rmdir failed");
  }
}

void lreadFile(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available())
  {
    Serial.write(file.read());
  }
  file.close();
}

void lwriteFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    Serial.println("File written");
  }
  else
  {
    Serial.println("Write failed");
  }
  file.close();
}

void lappendFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message))
  {
    Serial.println("Message appended");
  }
  else
  {
    Serial.println("Append failed");
  }
  file.close();
}

void lrenameFile(fs::FS &fs, const char *path1, const char *path2)
{
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2))
  {
    Serial.println("File renamed");
  }
  else
  {
    Serial.println("Rename failed");
  }
}

void ldeleteFile(fs::FS &fs, const char *path)
{
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path))
  {
    Serial.println("File deleted");
  }
  else
  {
    Serial.println("Delete failed");
  }
}

void ltestFileIO(fs::FS &fs, const char *path)
{
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file)
  {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len)
    {
      size_t toRead = len;
      if (toRead > 512)
      {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    Serial.printf("%u bytes read for %u ms\n", flen, end);
    file.close();
  }
  else
  {
    Serial.println("Failed to open file for reading");
  }

  file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }

  size_t i;
  start = millis();
  for (i = 0; i < 2048; i++)
  {
    file.write(buf, 512);
  }
  end = millis() - start;
  Serial.printf("%u bytes written for %u ms\n", 2048 * 512, end);
  file.close();
}
*/

void vdu()
{
  //  long leen = millis();
  static int llus = -1;
  if (gfxmode == 0)
  {
    s1.b = 0;
    if (memcmp(video, &ram[0x8000], 256) != 0)
    {
      memcpy(video, &ram[0x8000], 256);
      s1.Page = 0x80;
      memcpy(&s1.PageData, &ram[0x8000], 256);
      Serial.write(&s1.b, sizeof(s1));
    }
    if (memcmp(&video[256], &ram[0x8100], 256) != 0)
    {
      memcpy(&video[256], &ram[0x8100], 256);
      s1.Page = 0x81;
      memcpy(&s1.PageData, &ram[0x8100], 256);
      Serial.write(&s1.b, sizeof(s1));
    }
  }
  else if (gfxmode == 15)
  {
    if (llus++ < 20)
    {
      s1.b = 4;
      int idx = llus * 256;
      if (memcmp(&video[llus * 256], &ram[0x8000 + idx], 256) != 0)
      {
        memcpy(&video[idx], &ram[0x8000 + idx], 256);
        s1.Page = 0x80 + llus;
        memcpy(&s1.PageData, &ram[0x8000 + idx], 256);
        Serial.write(&s1.b, sizeof(s1));
      }
    }
    else
    {
      llus = -1;
    }
  }
  /*
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
  */

  // M5.Lcd.setCursor(0, 0);
  // m5.lcd.printf("Tijd: %d, %d, %d", millis(), leen, millis() - leen);
}

// Task1code: do the atom
void Task1code(void *pvParameters)
{
  loep();
}

// Task2code: jank de videodata naar de laptop
void Task2code(void *pvParameters)
{
  //  long leen = millis();
  unsigned short crc;
  for (;;)
  {
    //   long leen = millis();
    //   M5.Lcd.setCursor(0, 40);
    //   M5.Lcd.printf("Hz: %02d: bHz: %d  ", nHz, bHz);
    yield();
    if (bHz == true)
    {
      bHz = false;
      if (gfxmode == 0)
      {
        s1.b = 0;
        long tijd;
        if (memcmp(video, &ram[0x8000], 256) != 0)
        {
          tijd = micros();
          memcpy(video, &ram[0x8000], 256);
          s1.Page = 0x80;
          memcpy(&s1.PageData, &ram[0x8000], 256);
          crc = crc32(0, s1.PageData, 256);
          s1.crcH = crc / 256;
          s1.crcL = crc % 256;
          Serial.write(&s1.b, sizeof(s1));
          // send command
          Serial.flush(true); // wait for all data to be sent out UART
                              // delay(4);
          long d = micros() - tijd;
          Serial2.println(d);
        }
        if (memcmp(&video[256], &ram[0x8100], 256) != 0)
        {
          tijd = micros();
          memcpy(&video[256], &ram[0x8100], 256);
          s1.Page = 0x81;
          memcpy(&s1.PageData, &ram[0x8100], 256);
          crc = crc32(0, s1.PageData, 256);
          s1.crcH = crc / 256;
          s1.crcL = crc % 256;
          Serial.write(&s1.b, sizeof(s1));
          // send command
          Serial.flush(true); // wait for all data to be sent out UART
                              //   delay(4);
          long d = micros() - tijd;
          Serial2.println(d);
        }
      }
    }
    else if (gfxmode == 15)
    {
      for (int nHz = 0; nHz < 24; nHz++)
      {
        s1.b = 4;
        int idx = nHz * 256;
        if (memcmp(&video[idx], &ram[0x8000 + idx], 256) != 0)
        {
          memcpy(&video[idx], &ram[0x8000 + idx], 256);
          s1.Page = 0x80 + nHz;
          memcpy(&s1.PageData, &ram[0x8000 + idx], 256);
          crc = crc32(0, s1.PageData, 256);
          s1.crcH = crc / 256;
          s1.crcL = crc % 256;
          Serial.write(&s1.b, sizeof(s1));
          // send command
          //   Serial.flush(true); // wait for all data to be sent out UART
          // delay(2);
        }
      }
      /*     long koos = millis();
           if (koos - leen != 0)
           {
             M5.Lcd.setCursor(0, 40);
             M5.Lcd.printf("t %d  ", koos - leen);
           }
           */
    }
  }
}

sizeType getDirSize(fs::FS &fs, const char *dirname)
{
  sizeType dirSize;
  sizeType gotSize;
  File root = fs.open(dirname);
  // Serial.println(dirname);
  /*
  if (!root)
  {
    Serial.println("Failed to open directory");
    return dirSize;
  }
  if (!root.isDirectory())
  {
    Serial.println("Not a directory");
    return dirSize;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      dirSize.local += file.size();
      dirSize.complete += file.size();
    }
    else
    {
      gotSize = getDirSize(fs, file.path());
      dirSize.complete += gotSize.complete;
    }
    file = root.openNextFile();
  }
  */
  return dirSize;
}

void listDir(fs::FS &fs, const char *dirname, uint8_t level)
{
  boolean hasSubs = false;
  //  sizeType dirSize = getDirSize(fs, dirname); // hier worden alle mappen doorlopen
  //  Serial.println("------------------- LOCAL FILES ---------------------");
  // Serial.printf("DIR : %-28s \n", dirname);

  File root = fs.open(dirname); // bovenaan beginnen, root map openen om inhoud te bereiken
  if (!root)
  {
    //  Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    // Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile(); // eerste map in de lijst
  while (file)                     // zolang er entries zijn...
  {
    if (!file.isDirectory())
    {
      //  Serial.printf("FILE: %-36s %10d\n", file.name(), file.size());
      yield();
    }
    file = root.openNextFile(); // deze is even belangrijk!
  }
  // klaar met alle entries
  root = fs.open(dirname);
  file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      hasSubs = true;
      listDir(fs, file.path(), level + 1); // recursie met 1 laag dieper de files
    }
    file = root.openNextFile(); // hier weer vanaf de root positie
  }
  /*  if (level == 0 || hasSubs)
    {
      Serial.println("=============== SIZE INCL. SUBDIRS ==================");
      Serial.printf("DIR : %-28s \n", dirname);
      Serial.println("=====================================================");
    }
    */
}
#define val 100
void setup()
{
  /* M5.begin();
   M5.Lcd.fillScreen(BLACK);          // Set the screen background color to black.                                 //
   M5.Lcd.setTextColor(GREEN, BLACK); // Sets the foreground color and background color of the
                                      // displayed text.
   M5.Lcd.setTextSize(2);             // Set the font size.
   M5.Lcd.println("Rebooted");
 */
  pinMode(enPin, INPUT_PULLUP); // Gebruik de interne pull-up weerstand
  Serial.begin(900000);
  Serial2.begin(115200);
  Serial2.println("Reboot");

  SyncTimer = timerBegin(0, 80, true);                 // timer 0 12.5 nS * 1333 = ms
  timerAttachInterrupt(SyncTimer, &onTimerSync, true); // edge irq
  timerAlarmWrite(SyncTimer, 16666, true);
  timerAlarmEnable(SyncTimer);

  ScanlineTimer = timerBegin(1, 80, true);                     // timer 1 12.5 nS * 1333 = ms
  timerAttachInterrupt(ScanlineTimer, &onTimerScanline, true); // edge irq
  timerAlarmWrite(ScanlineTimer, 64, true);
  timerAlarmEnable(ScanlineTimer);

  timer = timerBegin(2, 80, true);                    // Timer 0, clock divisor 80
  timerAttachInterrupt(timer, &timerInterrupt, true); // Attach the interrupt handling function
  timerAlarmWrite(timer, 40000, true);                // Interrupt 25 x every 1 second
  timerAlarmEnable(timer);                            // Enable the alarm

  // vduer.attach_ms(60, vdu);
  for (int i = 0; i < 6 * 1024; i++)
    video[i] = 0x2a;
  // Serial.println();
  /*
  if (!SD.begin())
  { // Initialize the SD card.
    M5.Lcd.println("Card failed, or not present");
    while (1)
      ;
  }
  M5.Lcd.println("TF card initialized.");
  // Serial.println("initialisation done.");
  dir = SD.open("/");
  */
  // Serial.println("End of setup");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  // Optional: set the transmit power, default is 3db

  // Serial.printf("HEAP BEGIN %d\n", ESP.getFreeHeap()); //

  lines = 400;

  initmem();
  // Serial.printf("HEAP after initmem  %X \n", ESP.getFreeHeap());
  loadroms();
  // Serial.printf("HEAP after loadroms  %X \n", ESP.getFreeHeap());
  reset6502();
  // Serial.printf("HEAP after reset6502  %X \n", ESP.getFreeHeap());
  init8255();
  debugon = false;
  resetvia();
  //
  atom_reset(0);
  // Serial.print("Setup: MAIN Executing on core ");
  // Serial.println(xPortGetCoreID());
  // Serial.print("Free Heap: ");
  // Serial.println(system_get_free_heap_size(), HEX);

  for (int c = 0; c < 128; c++)
  {
    keylookup[c] = c;
    key[c] = 0;
  }

  // create a task that will be executed in the Task1code() function, with priority 1 and executed on core 0
  xTaskCreatePinnedToCore(
      Task1code, // Task function.
      "Task1",   // name of task.
      10000,     // Stack size of task
      NULL,      // parameter of the task
      1,         // priority of the task
      &Task1,    // Task handle to keep track of created task
      0);        // pin task to core 0
  delay(500);

  // create a task that will be executed in the Task2code() function, with priority 1 and executed on core 1
  xTaskCreatePinnedToCore(
      Task2code, // Task function.
      "Task2",   // name of task.
      10000,     // Stack size of task
      NULL,      // parameter of the task
      1,         // priority of the task
      &Task2,    // Task handle to keep track of created task
      1);        // pin task to core 1
  delay(500);
}

void do_keyboard(uint8_t scan, uint8_t ShCrRe)
{
  bool bAan = ShCrRe & 0x10;
  uint8_t keyt;
  // bAan zet de scancode aan of uit
  // ik doe een xor om weer uit te zetten. Er zijn max 4 toetsen, ik gebruik er nu nog maar 1
  // static u_int8_t scanOud = 0; //, ShCrReOud = 0;
  // control = 2
  // shift = 1
  // alt = 4

  shift = ShCrRe & 0x01;
  ctrl = ShCrRe & 0x2;
  alt = ShCrRe & 0x4;

  switch (scan)
  {
  case 0xbe: // punt
    scan = KEY_STOP;
    break;
  case 0xbc: // komma
    scan = KEY_COMMA;
    break;
  case 0xbb: // =
    scan = KEY_EQUALS;
    break;
  case 0xbd: // -
    scan = KEY_MINUS;
    break;
  case 0xdb: // bracket open
    scan = KEY_OPENBRACE;
    break;
  case 0xdd: // close brace
    scan = KEY_CLOSEBRACE;
    break;
  case 0xdc: //
    scan = KEY_BACKSLASH;
    break;
  case 0xba: // ;
    scan = KEY_SEMICOLON;
    break;
  case 0xde: // :
    scan = KEY_SEMICOLON - 1;
    break;
  case 0xc0: // @a
    scan = KEY_MONKEYTALE;
    break;
  case 0x28: // @a
    scan = KEY_UP;
    if (!key[scan])
      shift = true;
    else
      shift = ShCrRe & 0x01;
    break;
  case 0x25: // Key left
    scan = KEY_RIGHT;
    if (!key[scan])
      shift = true;
    else
      shift = ShCrRe & 0x01;
    break;

  default:
    break;
  }
  /* poging om hangende toets te vinden..
  for (int i = 0; i < 128; i++)
  {
    key[scan] = false;
  } */
  if (scan < 128)
  {
    key[scan] = bAan;
    //  M5.Lcd.clearDisplay();
    // M5.Lcd.setCursor(120, 20);
    // M5.Lcd.printf("LL%03d:  %d%d%d: %d", scan, shift, ctrl, alt, bAan); // 4
    //  M5.Lcd.printf("Scancode: %d\r\n", scan);
  }

  /*
    switch (keyt)
    {
    case 58:
      // list();
      Serial.println("F1");
      // vga.scroll(1, GREEN);
      break;
    }

  case 0x07:
    atom_reset(0);
  case 0x11:
    alt = bAan;
    break;
  case 0x12:
  case 0x59:
    shift = bAan;
    break;
  case 0x14:
    ctrl = bAan;
    break;
  case 0x6b:
    key[0x74] = bAan;
    shift = bAan;
    break;
  case 0x75:
    key[0x72] = bAan;
    shift = bAan;
    break;
  }
  */
}

void loop()
{
}

void loep()
{
  static int line = 0, col = 0;
  unsigned char c[4];

  int index;
  static long starttime = millis();
  // Serial.println("eerste keer in de loop");
  for (;;)
  {
    atom_run();

    // M5.update(); // buttones
    // read the incoming byte:
    while (Serial.available() > 0)
    {
      if (millis() - starttime > 100)
      {
        index = 0;
        starttime = millis();
      }

      if (Serial.available() > 1)
      {
        c[index++] = Serial.read();
        c[index++] = Serial.read();
        // incomingByte = Serial.read();
        // M5.Lcd.setCursor(0, 0);
        // M5.Lcd.printf("%02x %02x: ", c[1], c[0]);
        // control = 1 // 11 12 b0010
        // shift = 2 // 10 11 b0001
        // alt = 4 // rechts: 12 1a // b0100
        // capslock = 1b
        // a = 41 12
        // bij x bxxx 1xxxx is toets aan
        //    M5.Lcd.setCursor(20, 20);
        do_keyboard(c[1], c[0]);
      } // 3
    } // while >0
    //   M5.Lcd.setCursor(0, 0);

    // M5.update();
    /*
        if (M5.BtnC.wasPressed())
        */
    if (digitalRead(enPin) == LOW)
    {
      {
        //   static int i = 0;
        // M5.Lcd.fillScreen(BLACK); // Set the screen background color to black.

        for (int j = 0; j < 2; j++)
        {
          s1.b = 0;
          s1.Page = 0x80 + j;
          for (int k = 0; k < 256; k++)
          {
            s1.PageData[k] = ram[0x8000 + k + (j * 256)];
            // yield();
          }
          long times = millis(), timest;
          unsigned short crc = crc32(0, s1.PageData, 256);
          s1.crcH = crc / 256;
          s1.crcL = crc % 256;
          Serial.write(&s1.b, sizeof(s1));

          timest = millis() - times;
          //    M5.Lcd.printf("Time %d:%d \r\n", j + 0x80, timest);
          delay(100);
        }
        // SD.end();
        // M5.shutdown();
      }
      /*
      if (M5.BtnA.wasPressed())
      {
        // SD.end();
        init8255();
        atom_reset(0);
        M5.Lcd.clear();
        M5.Lcd.setCursor(20, 20);
        M5.Lcd.printf("Reset");
      }
      if (M5.BtnB.wasPressed())
      {
        M5.Lcd.clear();
        M5.Lcd.setCursor(20, 20);
  */
      /*
     if (!SD.begin())
     { // Initialize the SD card.
       M5.Lcd.println("Card failed, or not present");
       while (1)
         ;
     } */
      while (digitalRead(enPin) == LOW)
      {
        yield();
      }
    }
    yield();
  } // for
}