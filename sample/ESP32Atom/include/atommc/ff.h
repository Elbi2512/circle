#include "FS.h"
//#include "SPIFFS.h"
/* Character code support macros */

#define IsUpper(c) (((c) >= 'A') && ((c) <= 'Z'))
#define IsLower(c) (((c) >= 'a') && ((c) <= 'z'))

#define IsDBCS1(c) 0
#define IsDBCS2(c) 0

typedef unsigned char BYTE;
// typedef size_t WORD;

//#define WORD size_t
/* typedef struct _FILINFO_
{
	size_t fsize; // File size 
//WORD fdate;		// Last modified date *
//WORD ftime;		// Last modified time *
//BYTE fattrib;	/ Attribute *
char fname[13]; // Short file name (8.3 format) 
}
FILINFO;
*/
/* File function return code (FRESULT) */
/*
typedef enum
{
	FR_OK = 0,			// 0 *
	FR_DISK_ERR,		// 1 *
	FR_INT_ERR,			// 2 *
	FR_NOT_READY,		// 3 *
	FR_NO_FILE,			// 4 *
	FR_NO_PATH,			// 5 *
	FR_INVALID_NAME,	// 6 *
	FR_DENIED,			// 7 *
	FR_EXIST,			// 8 *
	FR_INVALID_OBJECT,	// 9 *
	FR_WRITE_PROTECTED, // 10 *
	FR_INVALID_DRIVE,	// 11 *
	FR_NOT_ENABLED,		// 12 *
	FR_NO_FILESYSTEM,	// 13 *
	FR_MKFS_ABORTED,	// 14 *
	FR_TIMEOUT			// 15 *
} FRESULT;
*/
/*--------------------------------------------------------------*/
/* FatFs module application interface                           */

//FRESULT f_mount(BYTE, FATFS *);										  // Mount/Unmount a logical drive
//FRESULT f_open(File, char *, char);					   // Open or create a file
//FRESULT f_read(File, void *, size_t, size_t *);		   // Read data from a file
//FRESULT f_write(File, const void *, size_t, size_t *); // Write data to a file
//FRESULT f_lseek(File, size_t);						   // Move file pointer of a file object
//FRESULT f_close(File);								   // Close an open file object
//FRESULT f_opendir(DIR *, char *);				   // Open an existing directory
//FRESULT f_readdir(DIR *, FILINFO *);			   // Read a directory item
//FRESULT f_stat(char *, FILINFO *);				   // Get file status
//FRESULT f_getfree(char *, DWORD *, FATFS **);						  // Get number of free clusters on the drive
//FRESULT f_truncate(File *);											  // Truncate file
//FRESULT f_sync(File *);												  // Flush cached data of a writing file
//FRESULT f_unlink(char *); // Delete an existing file or directory
//FRESULT f_mkdir(char *);											  // Create a new directory
//FRESULT f_chmod(char *, BYTE, BYTE);								  // Change attriburte of the file/dir
//FRESULT f_utime(char *, const FILINFO *);							  // Change timestamp of the file/dir
//FRESULT f_rename(char *, char *);									  // Rename/Move a file or directory
//FRESULT f_forward(FIL *, UINT (*)(const BYTE *, UINT), UINT, UINT *); // Forward data to the stream
//FRESULT f_mkfs(BYTE, BYTE, WORD);									  // Create a file system on the drive
//FRESULT f_chdir(char *);											  // Change current directory
//FRESULT f_chdrive(BYTE);											  // Change current drive

/* File access control and file status flags (FIL.flag) */

#define FA_READ 0x01
#define FA_OPEN_EXISTING 0x00
#if _FS_READONLY == 0
#define FA_WRITE 0x02
#define FA_CREATE_NEW 0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS 0x10
#define FA__WRITTEN 0x20
#define FA__DIRTY 0x40
#endif
#define FA__ERROR 0x80

/* FAT sub type (FATFS.fs_type) */

#define FS_FAT12 1
#define FS_FAT16 2
#define FS_FAT32 3

/* File attribute bits for directory entry */

#define AM_RDO 0x01	 /* Read only */
#define AM_HID 0x02	 /* Hidden */
#define AM_SYS 0x04	 /* System */
#define AM_VOL 0x08	 /* Volume label */
#define AM_LFN 0x0F	 /* LFN entry */
#define AM_DIR 0x10	 /* Directory */
#define AM_ARC 0x20	 /* Archive */
#define AM_MASK 0x3F /* Mask of defined bits */

/* FatFs refers the members in the FAT structures with byte offset instead
/ of structure member because there are incompatibility of the packing option
/ between various compilers. */

#define BS_jmpBoot 0
#define BS_OEMName 3
#define BPB_BytsPerSec 11
#define BPB_SecPerClus 13
#define BPB_RsvdSecCnt 14
#define BPB_NumFATs 16
#define BPB_RootEntCnt 17
#define BPB_TotSec16 19
#define BPB_Media 21
#define BPB_FATSz16 22
#define BPB_SecPerTrk 24
#define BPB_NumHeads 26
#define BPB_HiddSec 28
#define BPB_TotSec32 32
#define BS_55AA 510

#define BS_DrvNum 36
#define BS_BootSig 38
#define BS_VolID 39
#define BS_VolLab 43
#define BS_FilSysType 54

#define BPB_FATSz32 36
#define BPB_ExtFlags 40
#define BPB_FSVer 42
#define BPB_RootClus 44
#define BPB_FSInfo 48
#define BPB_BkBootSec 50
#define BS_DrvNum32 64
#define BS_BootSig32 66
#define BS_VolID32 67
#define BS_VolLab32 71
#define BS_FilSysType32 82

#define FSI_LeadSig 0
#define FSI_StrucSig 484
#define FSI_Free_Count 488
#define FSI_Nxt_Free 492

#define MBR_Table 446

#define DIR_Name 0
#define DIR_Attr 11
#define DIR_NTres 12
#define DIR_CrtTime 14
#define DIR_CrtDate 16
#define DIR_FstClusHI 20
#define DIR_WrtTime 22
#define DIR_WrtDate 24
#define DIR_FstClusLO 26
#define DIR_FileSize 28
#define LDIR_Ord 0
#define LDIR_Attr 11
#define LDIR_Type 12
#define LDIR_Chksum 13
#define LDIR_FstClusLO 26
