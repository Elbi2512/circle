/*
    ff_emu.c

    Functions to emulate the FatFilesystem routines as used by AtoMMC.

    Note this is by no means a complete emulation of FATFS, but
    replicates / emulates enough of the functionality to allow emulation
    of the AtoMMC interface firmware.

    I had to split the emulation over two files because of a clash of
    structure names used by fatfs calls and the underlying os.

    2012-06-12, Phill Harvey-Smith.
*/

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

#define SHORT_NAME_LEN 12

extern PS2Keyboard kbd;
DIR dj;
EMUDIR emu;
// File root;
char MMCPath[PATHSIZE + 1];
char BaseMMCPath[PATHSIZE + 1];
File *openfil;
char openname[SHORT_NAME_LEN + 1];
extern char globalData[256];
void HexDumpHead(char *, int);
// FRESULT f_opena(File *, char *, BYTE);
#ifdef DEBUGFF
void HexDump(const void *Buff,
             int Length);

void HexDumpHead(const void *Buff,
                 int Length);

#endif
// extern BYTE existFile(fs::FS &fs, const char *path);
extern BYTE readFile(fs::FS &fs, const char *path);
// extern void sdInitL();
// extern void sdClose();

/* static BYTE file_exists(char name[])
{
    BYTE status;
    Serial.printf("%s: File exist found %d\n", __func__, __LINE__);
    // kbd.disIRQ();
    sdInitL();
    if (existFile(SD, name))
    //(SD.exists(name) == true)
    {
        Serial.printf("%s: File found %d\n", __func__, __LINE__);
        status = FR_OK;
    }
    else
    {
        Serial.printf("%s: File not found %d\n", __func__, __LINE__);
        status = FR_NO_PATH;
    }
    sdClose();
    return status;
}
*/

static void update_FIL(File *file,
                       int fp,
                       int updatefp)
{
    struct stat statbuf;
    Serial.printf("%s: %d. FilePointer:%d, Updatefp:%d\n\r", __func__, __LINE__, fp, updatefp);
    /*
    if ((fp) || (0 == updatefp))
    {
        if (updatefp)
            fil->fs = (FATFS *)fp;
        else
            fp = (int)fil->fs;

        if (0 == fstat(fp, &statbuf))
        {
            fil->fsize = statbuf.st_size;
        }
        fil->fptr = lseek(fp, 0, SEEK_CUR);
    }
    */
}

static FRESULT get_result(int err_no)
{
    // debuglog("get_result errno=%d [%04X]\n",err_no,err_no);
    switch (err_no)
    {
    case ENOENT:
        return FR_NO_PATH;
    case EACCES:
        return FR_DENIED;
    case EBUSY:
        return FR_DENIED;
    case EFAULT:
    case EIO:
        return FR_DISK_ERR;

    default:
        return FR_OK;
    }
}
FRESULT f_chdrive(
    BYTE drv /* Drive number */
)
{
    return FR_OK;
}
/*
FRESULT f_mount(
    BYTE vol, // Logical drive number to be mounted/unmounted
    FATFS *fs // Pointer to new file system object (NULL for unmount)
)
{
    return FR_OK;
}
*/

FRESULT f_chdir(
    char *path /* Pointer to the directory path */
)
{
    char newpath[PATHSIZE + 1];
    char fullpath[PATHSIZE + 1];
    FRESULT result = FR_NO_PATH;

    // add new directory to current
    snprintf(newpath, PATHSIZE, "%s/%s", MMCPath, path);

    // Resolve the newpath
    if (NULL != saferealpath(newpath, fullpath))
    {
        // Check that the new path is BELOW the base mmcpath
        if (0 == strncmp(BaseMMCPath, fullpath, strlen(BaseMMCPath)))
        {
            // And that it exists
            if (0 == access(fullpath, FR_OK))
            {
                strcpy(MMCPath, fullpath);
                result = FR_OK;
            }
        }
    }

    // //Serial.printf("f_chdir(path)\nnewpath=%s\nfullpath=%s\nbasepath=%s\n",path,newpath,fullpath,BaseMMCPath);

    return result;
}
// lb: hier kwam hij vandaan..

FRESULT f_read(
    File *fp,   /* Pointer to the file object */
    char *buff, /* Pointer to data buffer */
    size_t btr, /* Number of bytes to read */
    size_t *br  /* Pointer to number of bytes read */
)
{
    Serial.printf("Functie: %s, Regel: %d\r\n", __func__, __LINE__);
    // kbd.disIRQ();
    FRESULT status = (FRESULT)0;
    DWORD ptrpos;
    int bytesread;
    int error;

    ptrpos = fp->position();

    bytesread = fp->readBytes(buff, btr);
    *br = bytesread;

    Serial.printf("f_read bytes(%d) buff=[%04X],result=%d\n", bytesread, buff, ptrpos);
    HexDumpHead(buff, btr);
    // for (;;) {}
    update_FIL(fp, 0, 0);

    if (bytesread < 0)
    {
        error = errno;
        status = (FRESULT)error;
    }
    status = FR_OK;
    return status;
}

FRESULT f_write(
    File *fp,      /* Pointer to the file object */
    uint8_t *buff, /* Pointer to the data to be written */
    size_t btw,    /* Number of bytes to write */
    size_t *bw     /* Pointer to number of bytes written */
)
{
    DWORD ptrpos;
    int written;
    int error;
    int len = btw;
    FRESULT status;
    // Serial.println(__func__);
    // kbd.disIRQ();

    ptrpos = fp->position();

    // SP9 START

    written = fp->write(buff, btw);
    *bw = written;

    // debuglog("f_write(%d) offset=%d[%04X],result=%d\n",btw,ptrpos,ptrpos,written);
    Serial.printf("__func__(%d) offset=%d[%04X],result=%d\n", written, buff, ptrpos, *bw);
    HexDumpHead((char *)buff, written);

    if (written < 0)
    {
        error = errno;
        // debuglog("errno: %s [%d]\n",strerror(error),error);
        status = (FRESULT)error; /* Return correct error for RAF */
    }

    update_FIL(fp, 0, 0);
    status = FR_OK;
    // kbd.enaIRQ();
    return status;
}

// SP9 END

FRESULT f_close(
    File *fp /* Pointer to the file object to be closed */
)
{
    // Serial.println(__func__);
    // kbd.disIRQ();
    // int result = 0;

    if (0 != (int)fp)
        fp->close();

    // debuglog("f_close():result=%d\n",result);

    fp = NULL;
    openfil = NULL;
    // kbd.enaIRQ();
    return FR_OK;
}

FRESULT f_unlink(char *path) /* Pointer to the file or directory path */
{
    char del_path[PATHSIZE + 1];
    int open_mode = 0;

    /* CHANGED FOR SP4 */

    int result = 0;
    FRESULT status;

    int newfile;

    // Get real path of file and check to see if it exists
    snprintf(del_path, PATHSIZE, "%s/%s", MMCPath, path);

    // debuglog("f_unlink(%s)\n",del_path);
    // Serial.println(__func__);
    // kbd.disIRQ();

    result = unlink(del_path);

    if (result == 0)
        status = FR_OK;
    else

        status = get_result(errno);

    /* END SP4*/
    // kbd.enaIRQ();
    return status;
}

FRESULT f_opendir(
    DIR *dj,   /* Pointer to directory object to create */
    char *path /* Pointer to the directory path */

)
{
    File file;
    FRESULT status;
    // Serial.println(__func__);
    // kbd.disIRQ();

    // root = SD.open("/");
    //  Serial.println("fs opened");
    // File file = root.openNextFile();
    // Serial.println("fs openednextfile");

    if (file)
    {
        // Serial.print("FILE: ");
        // Serial.println(file.name());
        status = FR_OK;
    }
    else
    {
        status = FR_NO_PATH;
    }
    // kbd.enaIRQ();
    return status;
}

FRESULT f_readdir(
    DIR *dj,     /* Pointer to the open directory object */
    FILINFO *fno /* Pointer to file information to return */
)
{
    File file;
    FRESULT status;
    // Serial.println(__func__);
    // // kbd.disIRQ();
    // Serial.print("Finename: ");
    // Serial.println(fno->fname);
    /*  // If a file found copy it's details, else set size to 0 and filename to ''
    if (findnext(&emu))
    {
        fno->fsize = emu.fsize;
        fno->fattrib = emu.fattrib;
        strncpy(fno->fname, emu.filename, FNAMELEN);
    }
    else
    {
        fno->fsize = 0;
        fno->fname[0] = 0;
    }

    return FR_OK;
    */

    // If a file found copy it's details, else set size to 0 and filename to ''
    /*
    file = root.openNextFile();
    if (file)
    {
        fno->fsize = file.size();
        // emu.fsize;
        // Bestaat deze in fs?? fno->fattrib = file-> emu.fattrib;
        strncpy(fno->fname, file.name(), FNAMELEN);
    }
    else
    {
        fno->fsize = 0;
        fno->fname[0] = 0;
    }
    // kbd.enaIRQ();
    */
    return FR_OK;
}

FRESULT f_lseek(
    File *fp, /* Pointer to the file object */
    DWORD ofs /* File pointer from top of file */
)
{
    // Serial.println(__func__);
    // kbd.disIRQ();

    fp->seek(ofs);
    update_FIL(fp, 0, 0);
    // kbd.enaIRQ();
    return FR_OK;
}

static void get_fileinfo(             /* No return code */
                         DIR *dj,     /* Pointer to the directory object */
                         FILINFO *fno /* Pointer to the file information to be filled */
)
{
}

void get_fileinfo_special(FILINFO *fno)
{
    //   get_fileinfo(&dj, fno);

    Serial.printf("%s, %d. get_fileinfo_special()\n\r", __func__, __LINE__);
    /*
    if (NULL != openfil)
    {

        fno->fsize = openfil->fsize;
        //		fno->fptr	= openfil->fptr;
        fno->fdate = 0;
        fno->ftime = 0;
        fno->fattrib = get_fat_attribs((int)openfil->fs);
         //Serial.printf("size=%d, attr=%d\n", fno->fsize, fno->fattrib);
    }
    */
}

// #ifdef DEBUGFF
void HexDump(char *Buff,
             int Length)
{
    char LineBuff[80];
    char *LineBuffPos;
    int LineOffset;
    int CharOffset;
    char *BuffPtr;

    BuffPtr = Buff;

    for (LineOffset = 0; LineOffset < Length; LineOffset += 16, BuffPtr += 16)
    {
        LineBuffPos = LineBuff;
        LineBuffPos += sprintf(LineBuffPos, "%4.4X ", LineOffset);

        for (CharOffset = 0; CharOffset < 16; CharOffset++)
        {
            if ((LineOffset + CharOffset) < Length)
                LineBuffPos += sprintf(LineBuffPos, "%02X ", BuffPtr[CharOffset]);
            else
                LineBuffPos += sprintf(LineBuffPos, "   ");
        }

        for (CharOffset = 0; CharOffset < 16; CharOffset++)
        {
            if ((LineOffset + CharOffset) < Length)
            {
                if (isprint(BuffPtr[CharOffset]))
                    LineBuffPos += sprintf(LineBuffPos, "%c", BuffPtr[CharOffset]);
                else
                    LineBuffPos += sprintf(LineBuffPos, " ");
            }
            else
                LineBuffPos += sprintf(LineBuffPos, ".");
        }
        Serial.printf("%s\n", LineBuff);
    }
    Serial.printf("\n\n");
}
// #endif

void HexDumpHead(char *Buff,
                 int Length)
{
    Serial.printf("Addr 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F ASCII\n");
    Serial.printf("----------------------------------------------------------\n");

    HexDump(Buff, Length);
};
