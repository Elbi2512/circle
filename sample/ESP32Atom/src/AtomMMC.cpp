/*
	AtoMMC interface functions for Atomulator.
	
	2012-06-11, P.Harvey-Smith.
*/
#include <arduino.h>
#include <stdio.h>
#include <string.h>
#include "atom.h"
#include "atommc.h"
#include "atommc/integer.h"
#include "atommc/ff.h"
#include "atommc/atmmc2.h"
#include "atommc/ff_emudir.h"

#define EESIZE 1024
#define EEFILENAME "mmc_eeprom.rom"

void LoadEE(void);

BYTE WASWRITE;
BYTE MMC_to_Atom;
BYTE Atom_to_MMC;
BYTE LatchedAtomAddr;
BYTE TRISB; // So port code compiles ok !
BYTE LATB;
BYTE PORTB;

BYTE eeprom[EESIZE];
char windowData[512];
char globalData[256];

#ifdef INCLUDE_SDDOS
//char sectorData[512];
#endif

char configByte;
char blVersion;
extern char MMCPath[PATHSIZE + 1];
static char EEPath[PATHSIZE + 1];

void InitMMC(void)
{
    //rpclog("InitMMC()\n");
    //Serial.println(__func__);
    // Setup base MMC path
    //saferealpath(BaseMMCPath, MMCPath);
    //strcpy(BaseMMCPath, MMCPath);

    // Load EEPROM
    //  snprintf(EEPath, PATHSIZE, "%s/%s", BaseMMCPath, EEFILENAME);
    //LoadEE();
    configByte = 0xff;
    //eeprom[EE_SYSFLAGS];

    // Initialise AtoMMC code.
    //at_initprocessor();
    //rpclog("InitMMC():Done\n");
}

uint8_t ReadMMC(uint16_t addr)
{
    uint8_t Current;
 //   Serial.println(__func__);
    //Serial.printf("Adress: %04X, data: ", addr);
    //Serial.printf("PC=%04X ReadMMC(%04X)=", pc, addr);
    WASWRITE = 0;
    Current = MMC_to_Atom;
    at_process();
    //Serial.printf("MMC to Atom: %02X\n\r", MMC_to_Atom);
    return Current;
}

void WriteMMC(uint16_t addr, uint8_t data)
{
   // Serial.println(__func__);
    //Serial.printf("Adress: %04X, data: %02x\r\n", addr, data);
    //Serial.printf("PC=%04X WriteMMC(%04X,%02X)\n", pc, addr, data);
    WASWRITE = 1;
    LatchedAtomAddr = (addr & 0x0F);
    Atom_to_MMC = data;
    at_process();
}

void LoadEE(void)
{
    /*
    FILE *EEFile;
    Serial.println(__func__);
    // Make sure EEPROM initialised to 0xFF if file not present
    memset(eeprom, 0xFF, EESIZE);

    // Load EEPROM from file
    EEFile = fopen(EEPath, "rb");
    if (EEFile != NULL)
    {
        fread(eeprom, 1, EESIZE, EEFile);
        fclose(EEFile);
    }
    */
}

void SaveEE(void)
{
    /*
    FILE *EEFile;
    Serial.println(__func__);
    EEFile = fopen(EEPath, "wb");
    if (EEFile != NULL)
    {
        fwrite(eeprom, 1, EESIZE, EEFile);
        fclose(EEFile);
    }
    */
}

void FinalizeMMC(void)
{
    Serial.println(__func__);
    //SaveEE();
}