#include <Arduino.h>
#include "atommc/integer.h"
#include "def\hardware.h"
#include <FS.h>
// #include <SPIFFS.h>
#include "PS2keyboard.h"
// #include "SD.h"
// #include "SPI.h"

#include "atommc/ff_emudir.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h> // Required for OSX and lseek()
#include "SD.h"
#include <FS.h>
// #include <SPIFFS.h>

#include "atommc/integer.h"
#include "ff.h"
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include "atommc/ff_emudir.h"
#include "atom.h"
#include "PS2Keyboard.h"

// This sequence of defines is needed as MinGW specifically needs binary
// files to be opened with O_BINARY, which does not exist on other platforms.
#ifndef O_BINARY
#ifdef _O_BINARY
#define O_BINARY _O_BINARY
#else
#define O_BINARY 0
#endif
#endif

extern char globalData[256];
extern uint8_t irq_num;

void errorHalt(String errormsg);
// void IRAM_ATTR kb_interruptHandler(void);
File existFile(fs::FS &fs, const char *path);

extern PS2Keyboard kbd;

#define SCK 14
#define MISO 2
#define MOSI 12
#define CS 13

BYTE readFile(fs::FS &fs, const char *path);
// fs::FS RootSD;

static BYTE file_exists(char name[])
{
    if (SD.exists(name))
    {
        Serial.printf("%s: File found %d\n", __func__, __LINE__);
        return FR_OK;
    }
    else
    {
        Serial.printf("%s: File not found %d\n", __func__, __LINE__);
        return FR_NO_PATH;
    }

    // kbd.disIRQ();
    /*
    if (SPIFFS.exists(name) == true)
    {
        // Serial.printf("%s: File found %d\n", __func__, __LINE__);
        return FR_OK;
    }
    else
    {
        // Serial.printf("%s: File not found %d\n", __func__, __LINE__);
        return FR_NO_PATH;
    }
    */
}

FRESULT f_opena(File *fp, char *path, BYTE mode)
{
    Serial.printf("%s, regel:%d\r\n", __func__, __LINE__);
    BYTE exists;
    char open_mode = 0;
    int newfile;
    FRESULT status = (FRESULT)0;
    char tekst[20];

    // Get real path of file and check to see if it exists
    //  //Serial.printf("Fopenpath: %s/%s\n", (const char *)globalData, path);
    mode &= (FA_READ | FA_WRITE | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW);
    // Serial.printf("Mode: %x\n", mode);

    sprintf(tekst, "/%s", (const char *)globalData);
    Serial.println(tekst);
    Serial.printf("mode: %x\r\n", mode);
    // Serial.println(__LINE__);
    // res = SPIFFS.open("/test.txt", FILE_WRITE);
    // Serial.println(__LINE__);
    exists = file_exists(tekst);
    Serial.printf("Exists: %d\r\n", exists);
    // Serial.println(__LINE__);
    if (FR_OK == exists)
    {
        if (mode & FA_CREATE_NEW)
        {
            // Serial.println(__LINE__);
            // kbd.enaIRQ();
            return FR_EXIST;
        }

        if (mode & FA_CREATE_ALWAYS)
        {
            // Serial.println(__LINE__);
            open_mode = O_CREAT;
        }
        if (mode & (FA_READ | FA_WRITE))
        {
            // Serial.println(__LINE__);
            if (mode & FA_WRITE)
                open_mode |= O_RDWR;
            else
                open_mode |= O_RDONLY;
            // Serial.println(__LINE__);
        }
        // Serial.println(__LINE__);
    }
    else
    {
        // Serial.println(__LINE__);
        if (mode & (FA_OPEN_ALWAYS | FA_CREATE_NEW | FA_CREATE_ALWAYS))
        {
            // Serial.println("Poging om file aan te maken");
            // Serial.println(mode, HEX);
            open_mode = O_CREAT | O_RDWR;
        }
        else
        {
            // Serial.println("File not found");
            // kbd.enaIRQ();
            return FR_NO_FILE;
        }
    }
    Serial.printf("%s: %d\n", __func__, __LINE__);
    Serial.printf("File openmode: %x\n\r", open_mode);

    if (open_mode == 0)
    {
        Serial.printf("Line:%d, Open file: %s, met modus: %s\n", __LINE__, tekst, "r");
        *fp = SD.open(tekst, FILE_READ);
    }
    if (open_mode == 2)
    {
        Serial.printf("Line:%d, Open file: %s, met modus: %s\n", __LINE__, tekst, "r");
        *fp = SD.open(tekst, FILE_WRITE);
    }
    Serial.printf("Status: %d, %s: %d\n", status, __func__, __LINE__);

    return status;
}

void llistDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("%s: %d\n\r", __func__, __LINE__);
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

void vervolg();

/* void sdInit()
{
    SPIClass spi = SPIClass(VSPI);
    spi.begin(SCK, MISO, MOSI, CS);

    if (!SD.begin(CS, spi, 80000000))
    {
        Serial.println("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE)
    {
        Serial.println("No SD card attached");
        return;
    }

    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC)
    {
        Serial.println("MMC");
    }
    else if (cardType == CARD_SD)
    {
        Serial.println("SDSC");
    }
    else if (cardType == CARD_SDHC)
    {
        Serial.println("SDHC");
    }
    else
    {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    vervolg();
    //   RootSD = SD;
}
*/
void vervolg()
{
    Serial.printf("%s: %d\n\r", __func__, __LINE__);
    File b = existFile(SD, "/MENU");
    Serial.printf("Besand bestaat: %d\r\n", b);
    b = existFile(SD, "/LB");
    Serial.printf("Besand bestaat: %d\r\n", b);
    Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
    /*
    listDir(SD, "/", 0);
    Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));
    b = existFile(SD, "/LB");
    Serial.printf("Besand bestaat: %d\r\n", b);
    Serial.printf("%X\r\n", SD);
    // SD.end();
    */
    // kbd.enaIRQ();
}

void listAllFiles()
{
    /*
    // kbd.disIRQ();
    File root = SD.open("/");
    Serial.println("fs opened");
    File file = root.openNextFile();
    Serial.println("fs openednextfile");

    while (file)
    {
        Serial.print("FILE: ");
        Serial.println(file.name());
        file = root.openNextFile();
    }
    vTaskDelay(2);
    // kbd.enaIRQ();
   */
}

void createDir(fs::FS &fs, const char *path)
{
    Serial.println(__func__);
    Serial.printf("Creating Dir: %s\n", path);
    // kbd.disIRQ();
    if (fs.mkdir(path))
    {
        Serial.println("Dir created");
    }
    else
    {
        Serial.println("mkdir failed");
    }
    // kbd.enaIRQ();
}

void removeDir(fs::FS &fs, const char *path)
{
    Serial.println(__func__);
    Serial.printf("Removing Dir: %s\n", path);
    // kbd.disIRQ();
    if (fs.rmdir(path))
    {
        Serial.println("Dir removed");
    }
    else
    {
        Serial.println("rmdir failed");
    }
    // kbd.enaIRQ();
}

BYTE readFile(fs::FS &fs, const char *path)
{
    Serial.println(__func__);
    Serial.printf("Reading file: %s\n", path);
    // kbd.disIRQ();
    File file = fs.open(path);
    if (!file)
    {
        Serial.println("Failed to open file for reading");
        return 0;
    }

    Serial.print("Read from file: ");
    while (file.available())
    {
        Serial.write(file.read());
    }
    file.close();
    // kbd.enaIRQ();
    Serial.println(__LINE__);
    return true;
}

// void IRAM_ATTR mount_spiffs()
File existFile(fs::FS &fs, const char *path)
{
    Serial.println(__func__);
    Serial.printf("Reading file: %s\n", path);
    Serial.println(__LINE__);
    // kbd.disIRQ();
    File f = (File)0;
    //   filename.replace("\n", " ");
    // filename.trim();
    /*
    if (!SPIFFS.exists(path))
    {
        return (File)0;
    }
    f = SPIFFS.open(path);
    */
    vTaskDelay(2);

    return f;
    /*
        File file = fs.open(path);
        Serial.println(__LINE__);
        if (!file)
        {
            Serial.println("Failed to open file for reading");
            return false;
        }

        Serial.print("Read from file: ");
        while (file.available())
        {
            static int i = 0;
            Serial.printf(" %02X", file.read());
            if (i++ > 16)
            {
                i = 0;
                Serial.println();
            }
        }
        Serial.println();
        file.close();
        // kbd.enaIRQ();
        vTaskDelay(2);
        return true;
        */
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("%s: %d\n\r", __func__, __LINE__);

    Serial.printf("Writing file: %s\n", path);
    // kbd.disIRQ();
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
    // kbd.enaIRQ();
}

void appendFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("%s: %d\n\r", __func__, __LINE__);
    Serial.printf("Appending to file: %s\n", path);
    // kbd.disIRQ();
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
    // kbd.enaIRQ();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    Serial.printf("%s: %d\n\r", __func__, __LINE__);
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    // kbd.disIRQ();
    if (fs.rename(path1, path2))
    {
        Serial.println("File renamed");
    }
    else
    {
        Serial.println("Rename failed");
    }
    // kbd.enaIRQ();
}

void deleteFile(fs::FS &fs, const char *path)
{
    Serial.printf("%s: %d\n\r", __func__, __LINE__);
    Serial.printf("Deleting file: %s\n", path);
    // kbd.disIRQ();
    if (fs.remove(path))
    {
        Serial.println("File deleted");
    }
    else
    {
        Serial.println("Delete failed");
    }
    // kbd.enaIRQ();
}
