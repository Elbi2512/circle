/*-------------------------------------------*/
/* Integer type definitions for FatFs module */
/*-------------------------------------------*/

#ifndef _INTEGER

#if 0
#include <windows.h>
#else

/* These types must be 16-bit, 32-bit or larger integer */
typedef int				INT;
typedef unsigned int	UINT;

/* These types must be 8-bit integer */
typedef signed char		CHAR;
typedef unsigned char	UCHAR;
typedef unsigned char	BYTE;

/* These types must be 16-bit integer */
typedef short			SHORT;
typedef unsigned short	USHORT;
typedef uint16_t	WORD;
typedef unsigned short	WCHAR;

/* These types must be 32-bit integer */
typedef uint32_t			LONG;
typedef uint32_t	ULONG;
typedef uint32_t	DWORD;

/* Boolean type */
typedef enum { FALSE = 0, TRUE } BOOL;

#endif

#define _INTEGER
#endif
