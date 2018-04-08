/* Copyright (C) 1982 by Jeffrey Mogul */

/*
 * sunmon.h
 *
 * Header file for Sun MC68000 ROM Monitor
 *
 * Created:
 *	Jeffrey Mogul @ Stanford	14 May 1981
 *	out of the ashes of code originally written by Luis Trabb-Pardo
 *
 * Philip Almquist  November 4, 1985
 *	- Make start address of the globram data be independent of the type
 *	  of RAM being used (since we've had only one kind for so long that
 *	  lots of software assumes the address would never change).
 */

#ifndef	SUNMONDEFINED
#define SUNMONDEFINED

#include "buserr.h"
#include "m68vectors.h"

/*
 * Monitor version number: MAJVERSION.MINVERSION
 *	(This could be in an automatically generated subfile?)
 */

extern char SunIdent[];

/*
 * Version macros
 */
#define	MAKEVERSION(maj, min)	(((maj)<<8)+(min))
#define	MIN_VERSION(ver)	((ver)&0xFF)
#define	MAJ_VERSION(ver)	(((ver)>>8)&0xFF)

/*
 * C language "improvements"
 */
#define then 
#define	true	1
#define	false	0
#define	loop	for(;;)

typedef	int	typroc();	/* change this */
typedef	short	word;
typedef	long	longword;
typedef	char	byte;

/*
 * Various memory layout parameters
 */
#define INITSP	0x1000		/* initial stack pointer, for C */
#define INITSPa	1000		/* for asm() use */
asm("INITSPa = 0x1000");

#define	USERCODE 0x1000		/* starting address for user programs */
asm("USERCODE = 0x1000");

/*
 * STRTDATA is the starting address of monitor global data.
 * Unfortunately, this address is widely enough known that it
 * would be hard to change.
 */
#define STRTDATA 0x200

/*
 * RAM Refresh
 *
 * The current hardware does not support the use of "User Interrupt
 * Vectors"; therefore, the RAM Refresh routine starts where they
 * lie when the RAM chips used are small enough that the refresh
 * routine would end before 0x200.  For larger RAM chips the routine
 * goes at 0x300 (the globram data in version 1.1 ends at 2a8; this
 * allows a little space for it to grow).
 */

#ifdef TIRAM		/* TI's rams are weird */
#define RAMREFOPS 64	/* operations to do refresh */
#define RAMREFTIME MS_1	/* time between refreshes */
#else
#define RAMREFOPS 128	/* operations to do refresh */
#define RAMREFTIME MS_2	/* time between refreshes */
#endif TIRAM

/*
 * Set start address for refresh routine
 */
#if RAMREFOPS>128
# define RAMREFR	((typroc*)0x300)
#else
# define RAMREFR	((typroc*)EVEC_USERINT(0))
#endif

/*
 * Size of line input buffer - this should be at least 80,
 * to leave room for a full-length S-record.
 */
#define BUFSIZE	80

/*
 * These are the "erase" and "kill" characters for
 * input processing; they can be changed.
 */
#define	CERASE1 '\b'	/* backspace */
#define CERASE2 0x7F	/* delete */
#define CKILL	'\025'	/* ctrl/U */

/*
 * These are the default mode-changing characters
 */
#define CENDTRANSP	'\036'	/* end transparent mode, ctrl/^ */
#define	CSTLD		'\\'	/* Start Load */

#define UPCASE	0x5F	/* mask to force upper case letters */
#define	NOPARITY 0x7F	/* mask to strip off parity */

/*
 * Exception cause codes
 *
 * necmon passes one of these to monitor() [in bmon] on special
 * exceptions.
 *
 * These definitions get used in asm()'s and thus must not have
 * trailing tabs!  (or more than one leading tab)
 */
    	    	    	    	/* Default start of user code */
#define	EXC_RESET	0
asm("EXC_RESET=0");		/* Reset switch hit (or soft reset) */
#define	EXC_ABORT	1
asm("EXC_ABORT=1");		/* Abort switch hit */
#define	EXC_BREAK	2
asm("EXC_BREAK=2");		/* Breakpoint trap taken */
#define	EXC_EXIT	3
asm("EXC_EXIT=3");		/* Exit trap taken */
#define	EXC_TRACE	4
asm("EXC_TRACE=4");		/* Trace trap taken */
#define	EXC_EMT	5
asm("EXC_EMT=5");		/* Emulator trap */
#define EXC_BUSERR	6
asm("EXC_BUSERR=6");		/* Bus Error */
#define EXC_ADRERR	7
asm("EXC_ADRERR=7");		/* Address Error */
#define EXC_DOG	8
asm("EXC_DOG=8");		/* WatchDog Timeout */
#endif SUNMONDEFINED
