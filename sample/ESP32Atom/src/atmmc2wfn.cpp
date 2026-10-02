#include <Arduino.h>
#include <string.h>
#include "atommc/diskio.h"
#include "atommc/ff.h"
#include "atommc/atmmc2.h"
#include "atommc/atmmc2def.h"
#include "atommc/wildcard.h"
#include "atommc/atmmc2io.h"
#include "atom.h"
#include "FS.h"
#include "ff.h"
#include "SD.h"
// #include "SPIFFS.h"
#include "PS2Keyboard.h"
#include "def\hardware.h"
void HexDumpHead(char *, int);
//extern PS2Keyboard kbd;
#pragma udata fildata

int res;
extern File dir;
extern int numTabs;

// File fres;

// extern void list();
void listFilesInDir(File);
BYTE globalIndex;
WORD globalAmount;
BYTE globalDataPresent;
BYTE rwmode;
extern FRESULT f_opena(File *, char *, BYTE);

int filenum = -1;

FILINFO filinfodata[4];
// FIL fildata[4];
File filedata[4];

// extern BYTE windowData[];

#define WILD_LEN 16

char WildPattern[WILD_LEN + 1];
int fileOpen(BYTE);
extern FRESULT f_opena(File *, char *, BYTE);

#ifdef INCLUDE_SDDOS

unsigned char *sectorData = &(fildata[3].buf);

DWORD sectorInBuffer = 0xffffffff;

imgInfo driveInfo[4];

BYTE globalCurDrive;
DWORD globalLBAOffset;

#endif

#pragma udata

// use only immediately after open
// extern void get_fileinfo_special(FILINFO *);

void at_initprocessor(void)
{
   // Serial.println(__func__);
   int i;
   rwmode = 0;
   // Serial.println("at_initprocessor");
   // fatfs.win = windowData;

#ifdef INCLUDE_SDDOS
   memset(&driveInfo[0], 0xff, sizeof(imgInfo) * 4);
#endif

   // f_chdrive(0);
   // f_mount(0, &fatfs);

   for (i = 0; i < 4; i++)
   {
      // filedata[i]. = ;
   }
}

void GetWildcard(void)
{
   // Serial.println(__func__);
   int Idx = 0;
   int WildPos = -1;
   int LastSlash = -1;
   // Serial.println("GetWildcard");
   // Serial.printf("GetWildcard() %s\n", (const char *)globalData);

   while ((Idx < strlen((const char *)globalData)) && (WildPos < 0))
   {
      // Check for wildcard character
      if ((globalData[Idx] == '?') || (globalData[Idx] == '*'))
         WildPos = Idx;

      // Check for path seperator
      if ((globalData[Idx] == '\\') || (globalData[Idx] == '/'))
         LastSlash = Idx;

      Idx++;
   }

   Serial.printf("GetWildcard() Idx=%d, WildPos=%d, LastSlash=%d\n", Idx, WildPos, LastSlash);

   if (WildPos > -1)
   {
      if (LastSlash > -1)
      {
         // Path followed by wildcard
         // Terminate dir filename at last slash and copy wildcard
         globalData[LastSlash] = 0x00;
         strncpy(WildPattern, (const char *)&globalData[LastSlash + 1], WILD_LEN);
      }
      else
      {
         // Wildcard on it's own
         // Copy wildcard, then set path to null
         strncpy(WildPattern, (const char *)globalData, WILD_LEN);
         globalData[0] = 0x00;
      }
   }
   else
   {
      // No wildcard, show all files
#if (PLATFORM == PLATFORM_PIC)
      strcpypgm2ram((char *)&WildPattern[0], (const rom far char *)"*");
#elif (PLATFORM == PLATFORM_AVR)
      strncpy_P(WildPattern, PSTR("*"), WILD_LEN);
#elif (PLATFORM == PLATFORM_ATMU)
      strncpy(WildPattern, "*", WILD_LEN);
#endif
   }

   Serial.printf("GetWildcard() globalData=%s WildPattern=%s\n", (const char *)globalData, WildPattern);
}

int fileOpen(BYTE mode)
{
   int ret;
   File *file;
   char *buf;
   // char b1 = mode;
   Serial.printf("fileOpen: mode: %x\n", mode);

   if (filenum == 0)
   {
      file = &filedata[0];
      // The scratch file is fixed, so we are backwards compatible with 2.9 firmware
      ret = f_opena(file, buf, mode);
      // ret = f_opena(dir, buf, mode);

      //&filedata[0], globalData, mode);
   }
   else
   {
      // If a random access file is being opened, search for the first available FIL
      filenum = 0;
      if (!filedata[1])
      {
         filenum = 1;
      }
      else if (!filedata[2])
      {
         filenum = 2;
      }
      else if (!filedata[3])
      {
         filenum = 3;
      }
      if (filenum > 0)
      {
         file = &filedata[filenum];
         // kbd.disIRQ();
         ret = f_opena(file, globalData, mode);
         // vTaskDelay(2);
         //  kbd.enaIRQ();
         if (!ret)
         {
            // No error, so update the return value to indicate the file num
            ret = FILENUM_OFFSET | filenum;
         }
      }
      else
      {
         // All files are open, return too many open files
         ret = (FRESULT)ERROR_TOO_MANY_OPEN;
      }
   }
   // Serial.printf("fileOpen() ret: %d\n", STATUS_COMPLETE | ret);
   return STATUS_COMPLETE | ret;

   // lb 20
   /*
   int ret;
   // File ret;
   File file;
   char *open_mode;

   Serial.println(__func__);
   if (filenum == 0)
   {
      // The scratch file is fixed, so we are backwards compatible with 2.9 firmware
      // ret = f_open(&fildata[0], (const char *)globalData, mode);
      unsigned char bMode = 0;
      char tekst[20];
      sprintf(tekst, "/%s", (const char *)globalData);
      Serial.println(tekst);
      // Serial.printf("mode: %s\r\n", mode);
      // Serial.println(__LINE__);
      // res = SPIFFS.open("/test.txt", FILE_WRITE);

      // kbd.disIRQ();
      //      file = SPIFFS.open(tekst, mode);
      // The scratch file is fixed, so we are backwards compatible with 2.9 firmware
      //(char *)mode
      uint64_t cardSize = SD.cardSize() / (1024 * 1024);
      if (SD.exists(tekst) == true)
      {
         Serial.printf("Open ffile: %s, met modus: %s\n", tekst, "r");
         file = SD.open(tekst, "r");
      }
      else
      {
         Serial.printf("Open write ffile: %s, met modus: %s\n", tekst, "w");
         file = SD.open(tekst, "w");
      }
   }
   */
   /*
         newfile = open(open_path, open_mode | O_BINARY, S_IRWXU);
         update_FIL(fp, newfile, 1);

         //debuglog("Openmode:%04X\n",open_mode);

         if (newfile > 0)
         {
            openfil = fp;
            return FR_OK;
         }
         else
            return FR_INVALID_NAME;

      }

      ret = f_opena(&file, tekst, (char *)mode);
      //
   if (!file)
   {
      Serial.println("There was an error opening the file for writing");
   }

   filedata[filenum] = file;
   // Serial.print("FileNum: ");
   // Serial.println(filenum);
   // Serial.print("FileHandle: ");
   // Serial.println(file);
   // Serial.print("Filename: ");
   // Serial.println(file.name());
   // Serial.println(__LINE__);
   //  FILE_READ FILE_APPEND FILE_WRITE

     else
     {
        int ret;
        // If a random access file is being opened, search for the first available FIL
        filenum = 0;
        if (!fildata[1].fs)
        {
           filenum = 1;
        }
        else if (!fildata[2].fs)
        {
           filenum = 2;
        }
        else if (!fildata[3].fs)
        {
           filenum = 3;
        }
        if (filenum > 0)
        {
           ret = 0;
           //f_open(&fildata[filenum], (const char *)globalData, mode);
           if (!ret)
           {
              // No error, so update the return value to indicate the file num
              ret = FILENUM_OFFSET | filenum;
           }
        }
        else
        {
           // All files are open, return too many open files
           ret = ERROR_TOO_MANY_OPEN;
        }
     }*/
   // Serial.println(__LINE__);
   // Serial.println(STATUS_COMPLETE | res);
   //  ret = STATUS_COMPLETE | fres;
   //  return;
   //  STATUS_COMPLETE | res;
   //  kbd.enaIRQ();
   return STATUS_COMPLETE;
}

void wfnDirectoryOpen(void)
{
   // Serial.println(__func__);
   //  Separate wildcard and path
   // kbd.disIRQ();

   GetWildcard();
   /*
   res = 0;
   f_opendir(&dir, (char *)globalData);
   if (FR_OK != res)
   {
      WriteDataPort(STATUS_COMPLETE | res);
      return;
   }
*/
   // kbd.enaIRQ();
   WriteDataPort(STATUS_OK);
}

void listFilesInDir(File ddir)
{
   FILINFO *filinfo = &filinfodata[0];
   char len;
   int Match;
   Serial.println(__func__);

   while (true)
   {
      File entry = ddir.openNextFile();

      if (!entry)
      {
         int res = 0x0;
         Serial.println("Entry leeg");
         ddir.rewindDirectory();
         WriteDataPort(STATUS_COMPLETE | res);
         // kbd.enaIRQ();
         return;
      }
      char n = 0;

      // res = 0;
      //  f_readdir(&dir, filinfo);

      Serial.print(entry.name());
      Match = wildcmp(WildPattern, entry.name());
      Serial.printf("\nWildPattern=%s, entry.name()=%s, Match=%d\n", WildPattern, entry.name(), Match);
      if (Match)
      {
         len = (char)strlen(entry.name());

         if (entry.isDirectory())
         {
            Serial.println("/");
            // listFilesInDir(entry, numTabs + 1);
            n = 1;
            globalData[0] = '<';
         }
         Serial.println(__LINE__);
         strcpy((char *)&globalData[n], (const char *)entry.name());

         // if (filinfo->fattrib & AM_DIR)
         //{
         //   globalData[len + 1] = '>';
         //   globalData[len + 2] = 0;
         //   len += 2; // brackets

         // just for giggles put the attribute & filesize in the buffer
         //
         globalData[len + 1] = 0;
         // filinfo->fattrib;
         // Serial.println(__LINE__);
         size_t size = entry.size();
         // Serial.printf("Size: %d\n", size);
         memcpy(&globalData[len + 2], &size, sizeof(size_t));
         // Serial.println(__LINE__);
         WriteDataPort(STATUS_OK);
         entry.close();
         Serial.println(__LINE__);
         // kbd.enaIRQ();
         return;
      }
   }
   Serial.println(__LINE__);
   // kbd.enaIRQ();
}

void wfnDirectoryRead(void)
{
   Serial.println(__func__);
   listFilesInDir(dir);
}

void wfnSetCWDirectory(void)
{
   char tekst[40];
   sprintf(tekst, "/%s", (const char *)globalData);
   Serial.printf("%s, CWD: %s\r\n", __func__, tekst);
   /*
   // File ddir = SPIFFS.open(dir);
   dir = SPIFFS.open(tekst);
   if (!dir)
   {
      dir = SPIFFS.open("/");
      Serial.println("Onbekende map");
   }
   */
   WriteDataPort(STATUS_COMPLETE | 0); // f_chdir((const XCHAR *)globalData));
}

void wfnFileOpenRead(void)
{
   Serial.printf("%s: %d\n", __func__, __LINE__);
   res = fileOpen(FA_OPEN_EXISTING | FA_READ);
   // Serial.printf("fres: %d\r\n", res);
   // res = fres;
   // Serial.printf("res: %d\r\n", res);
   if (filenum < 4)
   {
      // FILINFO *filinfo = &filinfodata[filenum];
      // get_fileinfo_special(filinfo);
   }
   // Serial.println(__LINE__);
   WriteDataPort(STATUS_COMPLETE | res);
}

void wfnFileOpenWrite(void)
{
   Serial.printf("%s: %d\n", __func__, __LINE__);
   res = fileOpen(FA_CREATE_NEW | FA_WRITE);
   // Serial.printf("fres: %d\r\n", fres);
   //  res = fres;
   //  Serial.printf("res: %d\r\n", res);
   WriteDataPort(STATUS_COMPLETE | res);
   // Serial.println(__LINE__);
}

void wfnFileOpenRAF(void)
{
   Serial.printf("%s: %d\n", __func__, __LINE__);
   res = fileOpen(FA_OPEN_ALWAYS | FA_WRITE);
   // res = fres;
   WriteDataPort(STATUS_COMPLETE | res);
   // Serial.println(__LINE__);
}

void wfnFileGetInfo(void)
{
   Serial.printf("%s: %d\n", __func__, __LINE__);
   /*  FIL *fil = &fildata[filenum];
   FILINFO *filinfo = &filinfodata[filenum];
   union
   {
      DWORD dword;
      char byte[4];
   } dwb;
   //dwb.dword = fil->fsize;
   globalData[0] = dwb.byte[0];
   globalData[1] = dwb.byte[1];
   globalData[2] = dwb.byte[2];
   globalData[3] = dwb.byte[3];

   //dwb.dword = (DWORD)(fil->org_clust - 2) * fatfs.csize + fatfs.database;
   globalData[4] = dwb.byte[0];
   globalData[5] = dwb.byte[1];
   globalData[6] = dwb.byte[2];
   globalData[7] = dwb.byte[3];

   dwb.dword = fil->fptr;
   globalData[8] = dwb.byte[0];
   globalData[9] = dwb.byte[1];
   globalData[10] = dwb.byte[2];
   globalData[11] = dwb.byte[3];

   //globalData[12] = filinfo->fattrib & 0x3f;
*/
   WriteDataPort(STATUS_OK);
   // Serial.println(__LINE__);
}

extern FRESULT f_read(File *, char *, size_t, size_t *);

void wfnFileRead(void)
{
   Serial.printf("%s: %d\n", __func__, __LINE__);

   int ret;
   File *file = &filedata[filenum];
   UINT read=2;
   if (globalAmount == 0)
   {
      globalAmount = 256;
   }
   Serial.printf("f_read(%d) File=%x, Globaldata[%04X],globalAmount=%d\n\r", __LINE__, file, globalData, globalAmount);
   ret = f_read(file, globalData, globalAmount, &read);
 //      Serial.printf("f_read(%d) offset=%d[%04X],result=%d\n", btr, ptrpos, ptrpos, *br);
    HexDumpHead(globalData, ret);

   if (filenum > 0 && ret == 0 && globalAmount != read)
   {
      WriteDataPort(STATUS_EOF);
   }
   else
   {
      WriteDataPort(STATUS_COMPLETE | ret);
   }

   /*
      // int ret;
      //  FIL *fil = &fildata[filenum];
      File *file = &filedata[filenum];
      size_t read;
      int leen;
      if (globalAmount == 0)
      {
         globalAmount = 256;
      }
      // kbd.disIRQ();
      Serial.print("File grootte: ");
      Serial.println(file->size());

      // ret = 0;
      f_read(file, globalData, globalAmount, &read);
      // Read and display data
      // fread(buffer, strlen(c) + 1, 1, fp);
      */
   /*
   //Serial.printf("readSPIFFS size=%d, filename: %s, filehandle: %d\n", leen, file->name(), *file);
   int n = file->size();
   char *buff = (char *)malloc(n + 1);
   //Serial.printf("readSPIFFS size=%d\n", n);

   read = file->readBytes(buff, n);
   //Serial.print("Gelezen: ");
   //Serial.println(read);

   for (int i = 0; i < read; i++)
   {
      //uint8_t koos = file->read();
      //buff[i] = koos;
      //Serial.printf("%X ", buff[i]);
   }
   // read = file->size();
   //Serial.println();
   for (int j = 0; j < read; j++)
   {
      globalData[j] = buff[j];
      //Serial.print(globalData[j], HEX);
      //Serial.print(" ");
   }
   //Serial.println();
   //file->close();
   free(buff);

   //read = file->readBytes((char *)globalData, leen);
   //globalAmount);
   //Serial.print("Global ammount: ");
   //Serial.println(globalAmount);

   //Serial.printf("Bytes gelezen: %d, file: ", read);
   //byte i = f1.readBytes((char *)ibuffer, 64); // i = number of bytes placed in buffer from file f1
   //
   if (filenum > 0 && globalAmount != read)
   {
      // Serial.println("STATUS_EOF ");
      WriteDataPort(STATUS_EOF);
   }
   else
   {
      // uint8_t r = (uint8_t)read;
      res = read;
      // Serial.print("STATUS_COMPLETE | read ");
      // Serial.println(STATUS_COMPLETE); // | read);
      WriteDataPort(STATUS_COMPLETE); // | res);
   }
   // Serial.printf("Bytes gelezen: %d, file: ", read);
   // Serial.print(*file);
   // Serial.printf(", filenum: %d\r\n", filenum);
   // Serial.println(__LINE__);
   // kbd.enaIRQ();
   */
}

/*
static bool file_exists(char name[])
{
   bool bReturn;

   //Serial.printf("file_exists(%s)\n", name);

   bReturn = exists(name);
      return bReturn;
}
*/
extern FRESULT f_write(File *, uint8_t *, size_t, size_t *);

void wfnFileWrite(void)
{
   Serial.printf("%s: %d\n\r", __func__, __LINE__);
   // kbd.disIRQ();
   // FIL *fil = &fildata[filenum];
   File *file = &filedata[filenum];

   size_t written;
   if (globalAmount == 0)
   {
      globalAmount = 256;
   }
   // WriteDataPort(STATUS_COMPLETE | f_write(fil, (void*)globalData, globalAmount, &written));

   WriteDataPort(STATUS_COMPLETE | 0);
   f_write(file, (uint8_t *)globalData, globalAmount, &written);
   // written = file->write(globalData, globalAmount);
    Serial.printf("Bytes geschreven: %d, file: ", written);
   // Serial.print(*file);
   // Serial.printf(", filenum: %d\r\n", filenum);
   // kbd.enaIRQ();
}

void wfnFileClose(void)
{
   Serial.printf("%s: %d\n", __func__, __LINE__);
   // FIL *fil = &fildata[filenum];
   File *file = &filedata[filenum];

   WriteDataPort(STATUS_COMPLETE | 0);
   //
   //f_close(file);
   
   file->close();
  }

void wfnFileDelete(void)
{
   Serial.printf("%s: %d\n", __func__, __LINE__);
   // kbd.disIRQ();
   // FIL *fil = &fildata[filenum];
   //  f_close(fil);
   File *file = &filedata[filenum];
   file->close();
   // kbd.enaIRQ();
   WriteDataPort(STATUS_COMPLETE | 0);
   f_unlink((const char *)&globalData[0]);
}

void wfnFileSeek(void)
{
   Serial.printf("%s: %d\n", __func__, __LINE__);
   // kbd.disIRQ();
   // FIL *fil = &fildata[filenum];
   File *file = &filedata[filenum];

   union
   {
      DWORD dword;
      char byte[4];
   } dwb;

   dwb.byte[0] = globalData[0];
   dwb.byte[1] = globalData[1];
   dwb.byte[2] = globalData[2];
   dwb.byte[3] = globalData[3];

   WriteDataPort(STATUS_COMPLETE | 0); //
   file->seek(dwb.dword);
   // kbd.enaIRQ();
}


#define MK_WORD(x, y) ((WORD)(x) << 8 | (y))

// Read Eeprom
#define COM_RE MK_WORD('R', 'E')

// Write Eeprom
#define COM_WE MK_WORD('W', 'E')

void wfnExecuteArbitrary(void)
{
  
}
