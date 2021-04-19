/*
 * tkWinPrint.c --
 *
 *      This module implements Win32 printer access.
 *
 * Copyright © 1998-2019 Harald Oehlmann, Elmicron GmbH
 * Copyright © 2009 Michael I. Schwartz.
 * Copyright © 2018 Microsoft Corporation.
 * Copyright © 2021 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


/* 
 *  This section contains windows-specific includes and structures
 *  global to the file.
 *  Windows-specific functions will be found in a section at the
 *  end of the file.
 */
#if defined(__WIN32__) || defined (__WIN32S__) || defined (WIN32S)
/* Suppress Vista Warnings.  */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commdlg.h>



#include <tcl.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>    /* For floor(), used later.  */
#include "tkWinHDC.h"

/* 
 *  This value structure is intended for ClientData in all Print functions.
 *  Major philosophical change:
 *  Instead of relying on windows to maintain the various dialog structures,
 *  relevant parts of this printer_values structure will be copied in and out of
 *  the windows structures before the dialog calls.
 *  This will allow the PrintAttr function to behave properly, setting and getting
 *  various aspects of the printer settings without concern about other
 *  side effects in the program.
 * 
 *  The DEVMODE and DEVNAMES structures are static rather than
 *  global movable objects in order to simplify access. The
 *  global objects will be allocated and freed as needed,
 *  when the appropriate functions are called.
 * 
 *  If performance suffers drastically, or so many device drivers
 *  require extra device-specific information that the base information
 *  is insufficient, this is subject to change.
 *  If changed, the printer_values structure will maintain its
 *  own handle to the devmode and devnames, still copying them
 *  as needed to the dialogs.
 * 
 *  Really, this structure should be attached to all printer HDCs,
 *  and the hash table should track which printer_values structure
 *  is associated with the given hDC.
 *  Added the new member hdcname to track the named hDC.
 */

#define PVMAGIC 0x4e495250

static struct printer_values
{
  unsigned long magic; /* Give some indication if this is a "real" structure.  */
  HDC hDC;             /* Default printer context--override via args?.  */
  char hdcname[19+1];  /* Name of hdc.  */
  PRINTDLG pdlg;       /* Printer dialog and associated values.  */
  PAGESETUPDLG pgdlg;  /* Printer setup dialog and associated values.  */
  DEVMODE *pdevmode;   /* Allocated when the printer_values is built.  */
  char extra_space[1024+1];       /* space just in case....  */
  int space_count;                /* How much extra space.  */
  char devnames_filename[255+1];  /* Driver filename.  */
  char devnames_port[255+1];      /* Output port.  */
  char devnames_printername[255+1];  /* Full printer name.  */
  Tcl_HashTable attribs;   /* Hold the attribute name/value pairs..  */
  int in_job;          /* Set to 1 after job start and before job end.  */
  int in_page;         /* Set to 1 after page start and before page end.  */
  DWORD errorCode;      /* Under some conditions, save the Windows error code.  */
} default_printer_values;

/* 
 *  These declarations are related to creating, destroying, and
 *  managing printer_values structures.
 */
struct printer_values *current_printer_values = &default_printer_values;
static int is_valid_printer_values ( const struct printer_values *ppv );
static struct printer_values *make_printer_values(HDC hdc);
static void delete_printer_values (struct printer_values *ppv);

/* 
 *  These declarations and variables are related to managing a 
 *  list of hdcs created by this extension, and their associated
 *  printer value structures.
 */

static Tcl_HashTable printer_hdcs;
static void add_dc(HDC hdc, struct printer_values *pv);
static struct printer_values *delete_dc (HDC hdc);
static struct printer_values *find_dc_by_hdc(HDC hdc);

static HDC GetPrinterDC (const char *printer);
static int SplitDevice(LPSTR device, LPSTR *dev, LPSTR *dvr, LPSTR *port);

int Winprint_Init (Tcl_Interp *Interp);

/* 
 *  Internal function prototypes
 */
static int Print (ClientData unused, Tcl_Interp *interp, int argc, const char  * argv, int safe);
static int PrintList (ClientData unused, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintSend (ClientData unused, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintRawData (HANDLE printer, Tcl_Interp *interp, LPBYTE lpData, DWORD dwCount);
static int PrintRawFileData (HANDLE printer, Tcl_Interp *interp, const char *filename, int binary);
static int PrintStart (HDC hdc, Tcl_Interp *interp, const char *docname);
static int PrintFinish (HDC hdc, Tcl_Interp *interp);
static int Version(ClientData unused, Tcl_Interp *interp, int argc, const char  * argv);
static long WinVersion(void);
static void ReportWindowsError(Tcl_Interp * interp, DWORD errorCode);
static int PrinterGetDefaults(struct printer_values *ppv, const char *printer_name, int set_default_devmode);
static void StorePrintVals(struct printer_values *ppv, PRINTDLG *pdlg, PAGESETUPDLG *pgdlg);
static void RestorePrintVals (struct printer_values *ppv, PRINTDLG *pdlg, PAGESETUPDLG *pgdlg);
static void SetDevModeAttribs (Tcl_HashTable *att, DEVMODE *dm);
static void SetDevNamesAttribs (Tcl_HashTable *att, struct printer_values *dn);
static void SetPrintDlgAttribs (Tcl_HashTable *att, PRINTDLG *pdlg);
static void SetPageSetupDlgAttribs (Tcl_HashTable *att, PAGESETUPDLG *pgdlg);
static void SetHDCAttribs (Tcl_HashTable *att, HDC hDC);
static const char *set_attribute(Tcl_HashTable *att, const char *key, const char *value);
static const char *get_attribute(Tcl_HashTable *att, const char *key);
static int         del_attribute(Tcl_HashTable *att, const char *key);
static int PrintPageAttr (HDC hdc, int *hsize,   int *vsize,
                          int *hscale,  int *vscale,
                          int *hoffset, int *voffset,
                          int *hppi,    int *vppi);
static int is_valid_hdc (HDC hdc);
static void RestorePageMargins (const char *attrib, PAGESETUPDLG *pgdlg);

/* New functions from Mark Roseman.  */
static int PrintOpen(ClientData data, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintOpenDefault (ClientData data, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintClose(ClientData data, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintDialog(ClientData data, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintJob(ClientData data, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintPage(ClientData data, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintAttr(ClientData data, Tcl_Interp *interp, int argc, const char  * argv);
static int PrintOption(ClientData data, Tcl_Interp *interp, int argc, const char  * argv);
static int JobInfo(int state, const char *name, const char  * outname);
/* End new functions.  */

/* Functions to give printer contexts names.  */
static void init_printer_dc_contexts(Tcl_Interp *interp);
static void delete_printer_dc_contexts(Tcl_Interp *inter);
static const char *make_printer_dc_name(Tcl_Interp *interp, HDC hdc, struct printer_values *pv);
static int printer_name_valid(Tcl_Interp *interp, const char *name);
static HDC get_printer_dc(Tcl_Interp *interp, const char *string);
static int GetPrinterWithName(char *name, LPSTR *dev, LPSTR *dvr, LPSTR *port, int wildcard);


/* 
 *  Internal static data structures (ClientData)
 */
static char msgbuf[255+1];
int autoclose = 1;           /* Default is old behavior--one open printer at a time.  */ 

static struct {
  char *tmpname;
} option_defaults =
  {
    0
  };

/*
 *----------------------------------------------------------------------
 *
 * WinVersion --
 *
 *  WinVersion returns an integer representing the current version
 *  of Windows.
 *
 * Results:
 *	 Returns Windows version.
 *
 *----------------------------------------------------------------------
 */

static long WinVersion(void)
{
  static OSVERSIONINFO osinfo;
  if ( osinfo.dwOSVersionInfoSize == 0 )
    {
      osinfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
      GetVersionEx(&osinfo);  /* Should never fail--only failure is if size too small.  */
    }
  return osinfo.dwPlatformId;
}


/*
 *----------------------------------------------------------------------
 *
 * ReportWindowsError --
 *
 *  This function sets the Tcl error code to the provided
 Windows error message in the default language.
 *
 * Results:
 *	 Sets error code.
 *
 *----------------------------------------------------------------------
 */

static void ReportWindowsError(Tcl_Interp * interp, DWORD errorCode)
{
  LPVOID lpMsgBuf;
  FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL 
		 );
  Tcl_AppendResult(interp,(char *)lpMsgBuf,0);
  // Free the buffer.
  LocalFree( lpMsgBuf );
    
}

/* 
 *  The following two functions manage the hash table for
 *  attribute/value pairs.
 *  The keys are assumed managed by the Hash structure, but the
 *  values are 'strdup'ed, and managed by these routines.
 *  Other than cleanup, there seems to be no reason to delete attributes,
 *  so this part is ignored.
 */
 
/*
 *----------------------------------------------------------------------
 *
 * set_attribute --
 *
 *  Sets the value of a printer attribute.
 *
 * Results:
 *	 Sets attribute.
 *
 *----------------------------------------------------------------------
 */
 
 
static const char *set_attribute(Tcl_HashTable *att, const char *key, const char *value)
{
  Tcl_HashEntry *data;
  int status;
  char *val = 0;
    
  data = Tcl_CreateHashEntry(att, key, &status);
  if ( status == 0)  /* Already existing item!.  */
    if ( (val = (char *)Tcl_GetHashValue(data)) != 0 )
      Tcl_Free(val);
    
  /* In any case, now set the new value.  */
  if ( value != 0 && (val = (char *)Tcl_Alloc(strlen(value)+1)) != 0 )
    {
      strcpy (val, value);
      Tcl_SetHashValue(data, val);
    }
  return val;
}

/*
 *----------------------------------------------------------------------
 *
 * get_attribute --
 *
 *  Retrieve the value of a printer attribute.
 *
 * Results:
 *	 Gets attribute.
 *
 *----------------------------------------------------------------------
 */
 
static const char *get_attribute(Tcl_HashTable *att, const char *key)
{
  Tcl_HashEntry *data;
    
  if ( ( data = Tcl_FindHashEntry(att, key) ) != 0 )
    return (char *)Tcl_GetHashValue(data);
  return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * del_attribute --
 *
 *  Remove a printer attribute key/value from the hash table.
 *
 * Results:
 *	 Removes attribute.
 *
 *----------------------------------------------------------------------
 */
 
 
static int del_attribute(Tcl_HashTable *att, const char *key)
{
  Tcl_HashEntry *data;
    
  if ( ( data = Tcl_FindHashEntry(att, key) ) != 0 )
    {
      char *val;
      if ( (val = (char *)Tcl_GetHashValue(data) ) != 0 )
	Tcl_Free(val);
      Tcl_DeleteHashEntry(data);
      return 1;
    }
  return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * is_valid_printer_values --
 *
 *  This function verifies that there is a printer values structure,
 *  and that it has the magic number in it.
 *
 * Results:
 *	 Verifies printer structure.
 *
 *----------------------------------------------------------------------
 */
 
static int is_valid_printer_values ( const struct printer_values *ppv )
{
  if (ppv && ppv->magic == PVMAGIC)
    return 1;
  return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * make_printer_values --
 *
 *  Create and initialize a printer_values structure.
 *
 * Results:
 *	 Create printer structure.
 *
 *----------------------------------------------------------------------
 */

static struct printer_values *make_printer_values(HDC hdc)
{
  struct printer_values *ppv;
  if ( (ppv = (struct printer_values *)Tcl_Alloc(sizeof(struct printer_values)) ) == 0 )
    return 0;
  memset(ppv, 0, sizeof(struct printer_values) );
  ppv->magic = PVMAGIC;
  ppv->hDC   = hdc;
  Tcl_InitHashTable(&(ppv->attribs), TCL_STRING_KEYS);
  return ppv;
}

/*
 *----------------------------------------------------------------------
 *
 * delete_printer_values  --
 *
 *  Cleans up a printer_values structure.
 *
 * Results:
 *	 Cleans printer structure.
 *
 *----------------------------------------------------------------------
 */


static void delete_printer_values (struct printer_values *ppv)
{
  if ( is_valid_printer_values(ppv) )
    {
      ppv->magic = 0L;  /* Prevent re-deletion....  */
      Tcl_DeleteHashTable(&ppv->attribs);
      if ( ppv->pdevmode ) {
	Tcl_Free( (char *) ppv->pdevmode );
	ppv->pdevmode = 0;
      }
      Tcl_Free((char *)ppv);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetPrinterWithName  --
 *
 *  Returns the triple needed for creating a DC.
 *
 * Results:
 *	 Returns data to create device context.
 *
 *----------------------------------------------------------------------
 */

static int GetPrinterWithName(char *name, LPSTR *dev, LPSTR *dvr, LPSTR *port, int wildcard)
{
  /* The following 3 declarations are only needed for the Win32s case.  */
  static char devices_buffer[256];
  static char value[256];
  char *cp;
    
  /* First ensure dev, dvr, and port are initialized empty
   *  This is not needed for normal cases, but at least one report on
   *  WinNT with at least one printer, this is not initialized.
   *  Suggested by Jim Garrison <garrison@qualcomm.com>
   .  */
  *dev = *dvr = *port = "";
    
  /* 
   * The result should be useful for specifying the devices and/or OpenPrinter and/or lp -d.  
   * Rather than make this compilation-dependent, do a runtime check.  
   */
  switch ( WinVersion() )
    {
    case VER_PLATFORM_WIN32s:  /* Windows 3.1.  */
      /* Getting the printer list isn't hard... the trick is which is right for WfW?
       *  [PrinterPorts] or [devices]?
       *  For now, use devices.
       .  */
      /* First, get the entries in the section.  */
      GetProfileString("devices", 0, "", (LPSTR)devices_buffer, sizeof devices_buffer);
        
      /* Next get the values for each entry; construct each as a list of 3 elements.  */
      for (cp = devices_buffer; *cp ; cp+=strlen(cp) + 1)
        {
	  GetProfileString("devices", cp, "", (LPSTR)value, sizeof value);
	  if ( ( wildcard != 0 && Tcl_StringMatch(value, name) ) ||
	       ( wildcard == 0 && lstrcmpi (value, name) == 0 )  )
            {
	      static char stable_val[80];
	      strncpy (stable_val, value,80);
	      stable_val[79] = '\0';
	      return SplitDevice(stable_val, dev, dvr, port);
            }
        }
      return 0;
      break;
    case VER_PLATFORM_WIN32_WINDOWS:   /* Windows 95, 98.  */
    case VER_PLATFORM_WIN32_NT:        /* Windows NT.  */
    default:
      /* Win32 implementation uses EnumPrinters.  */
         
      /* There is a hint in the documentation that this info is stored in the registry.
       *  if so, that interface would probably be even better!
       *  NOTE: This implementation was suggested by Brian Griffin <bgriffin@model.com>,
       *        and replaces the older implementation which used PRINTER_INFO_4,5.
       */
      {
	DWORD bufsiz = 0;
	DWORD needed = 0;
	DWORD num_printers = 0;
	PRINTER_INFO_2 *ary = 0;
	DWORD i;
            
	/* First, get the size of array needed to enumerate the printers.  */
	if ( EnumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_FAVORITE, 
			  NULL, 
			  2, (LPBYTE)ary, 
			  bufsiz, &needed, 
			  &num_printers) == FALSE )
	  {
	    /* Expected failure--we didn't allocate space.  */
	    DWORD err = GetLastError();
	    /* If the error isn't insufficient space, we have a real problem..  */
	    if ( err != ERROR_INSUFFICIENT_BUFFER )
	      return 0;
	  }
            
	/* Now that we know how much, allocate it.  */
	if ( needed > 0 && (ary = (PRINTER_INFO_2 *)Tcl_Alloc(needed) ) != 0 )
	  bufsiz = needed;
	else
	  return 0;
            
	if ( EnumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_FAVORITE, NULL, 
			  2, (LPBYTE)ary, 
			  bufsiz, &needed, 
			  &num_printers) == FALSE )
	  {
	    /* Now we have a real failure!  */
	    return 0;
	  }
            
	for (i=0; i<num_printers; i++) 
	  {
	    if (  (wildcard != 0 && (Tcl_StringMatch(ary[i].pPrinterName, name) ||
				     Tcl_StringMatch(ary[i].pPortName,    name) )   ||
		   (                 (lstrcmpi(ary[i].pPrinterName, name) == 0 ||
				      lstrcmpi(ary[i].pPortName,    name) == 0) ) ) )
	      {
		static char stable_name[80];
		static char stable_port[80];
		static char stable_dvr[80];
		strncpy (stable_name, ary[i].pPrinterName, 80);
		strncpy (stable_port, ary[i].pPortName, 80);
		strncpy (stable_dvr,  ary[i].pDriverName, 80);
		stable_name[79] = stable_port[79] = stable_dvr[79] = '\0';
		*dev = stable_name;
		*dvr = stable_dvr;
		*port = stable_port;
		break;
	      }
	  }
	Tcl_Free((char *)ary);
      }
      break;
    }
  return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * PrinterGetDefaults  --
 *
 *  Stores the appropriate printer default
 *  values into the attributes and structure values.
 *
 * Results:
 *	Gets default values for printer.
 *
 *----------------------------------------------------------------------
 */

 
#define GETDEFAULTS_UNSUPPORTED     (0)
#define GETDEFAULTS_NOSUCHPRINTER   (-1)
#define GETDEFAULTS_CANTCREATEDC    (-2)
#define GETDEFAULTS_CANTOPENPRINTER (-3)
#define GETDEFAULTS_WINDOWSERROR    (-4)

static int PrinterGetDefaults(struct printer_values *ppv, 
                              const char *printer_name, 
                              int set_default_devmode)
{
  HANDLE pHandle;
  int result = 1;
    
  switch ( WinVersion() )
    {
    case VER_PLATFORM_WIN32s:
      return GETDEFAULTS_UNSUPPORTED;
    }
    
  if ( ppv->hDC == NULL )
    {
      /*
       *  Use the name to create a DC if at all possible:
       *  This may require using the printer list and matching on the name.
       .  */
      char *dev, *dvr, *port;
      if ( GetPrinterWithName ((char *)printer_name, &dev, &dvr, &port, 1) == 0 ) {
	return GETDEFAULTS_NOSUCHPRINTER;  /* Can't find a printer with that name.  */
      }
      if ( (ppv->hDC = CreateDC(dvr, dev, NULL, NULL) ) == NULL ) {
	return GETDEFAULTS_CANTCREATEDC;  /* Can't get defaults on non-existent DC.  */
      }
      if ( OpenPrinter((char *)printer_name, &pHandle, NULL) == 0 ) {
	return GETDEFAULTS_CANTOPENPRINTER;
      }
    }
    
    
  /* Use DocumentProperties to get the default devmode.  */
  if ( set_default_devmode > 0 || ppv->pdevmode == 0 )
    /* First get the required size:.  */
    {
      LONG siz = 0L;
        
      char *cp;
        
      siz = DocumentProperties (GetActiveWindow(),
				pHandle,
				(char *)printer_name,
				NULL,
				NULL,
				0);
        
      if ( siz > 0 && (cp = Tcl_Alloc(siz)) != 0 )
        {
	  if ( (siz = DocumentProperties (GetActiveWindow(),
					  pHandle,
					  (char *)printer_name,
					  (DEVMODE *)cp,
					  NULL,
					  DM_OUT_BUFFER)) >= 0 )
            {
	      if ( ppv->pdevmode != 0 )
		Tcl_Free ( (char *)(ppv->pdevmode) );
	      ppv->pdevmode = (DEVMODE *)cp;
	      SetDevModeAttribs ( &ppv->attribs, ppv->pdevmode);
            } else {
	    /* added 8/7/02 by Jon Hilbert <jhilbert@hilbertsoft.com>
	       This call may fail when the printer is known to Windows but unreachable
	       for some reason (e.g. network sharing property changes). Add code to 
	       test for failures here..  */
	    /* call failed -- get error code.  */
	    ppv->errorCode = GetLastError();
	    result = GETDEFAULTS_WINDOWSERROR;
	    /* release the DC.  */
	    DeleteDC(ppv->hDC);
	    ppv->hDC = 0;
	  }
        }
    }
  if (pHandle)
    ClosePrinter(pHandle);
    
  if (result == 1)  /* Only do this if the attribute setting code succeeded.  */
    SetHDCAttribs (&ppv->attribs, ppv->hDC);
    
  return result;  /* A return of 0 or less indicates failure.  */
}

/*
 *----------------------------------------------------------------------
 *
 * MakeDevMode  --
 *
 *  Creates devmode structure for printer.
 *
 * Results:
 *	Sets structure.
 *
 *----------------------------------------------------------------------
 */


static void MakeDevmode (struct printer_values *ppv, HANDLE hdevmode)
{
  DEVMODE *pdm;
    
  if (ppv->pdevmode)
    {
      Tcl_Free((char *)(ppv->pdevmode));
      ppv->pdevmode = 0;
    }
    
  if ( (pdm = (DEVMODE *)GlobalLock(hdevmode)) != NULL )
    {
      if ( (ppv->pdevmode = (DEVMODE *)Tcl_Alloc(pdm->dmSize + pdm->dmDriverExtra)) != NULL )
	memcpy (ppv->pdevmode, pdm, pdm->dmSize + pdm->dmDriverExtra);
      GlobalUnlock(hdevmode);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CopyDevname  --
 *
 *  Unlock and copy the devnames portion of the printer dialog.
 *
 * Results:
 *	Returns devnames.
 *
 *----------------------------------------------------------------------
 */

static void CopyDevnames (struct printer_values *ppv, HANDLE hdevnames)
{
  DEVNAMES *pdn;
    
  if ( (pdn = (DEVNAMES *)GlobalLock(hdevnames)) != NULL )
    {
      strcpy(ppv->devnames_filename,    (char *)pdn + pdn->wDriverOffset);
      strcpy(ppv->devnames_printername, (char *)pdn + pdn->wDeviceOffset);
      if (ppv && ppv->pdevmode) {
	/* As reported by Steve Bold, protect against unusually long printer names.  */
	strncpy(ppv->pdevmode->dmDeviceName, (char *)pdn + pdn->wDeviceOffset,sizeof(ppv->pdevmode->dmDeviceName));
	ppv->pdevmode->dmDeviceName[sizeof(ppv->pdevmode->dmDeviceName)-1] = '\0';
      }
      strcpy(ppv->devnames_port,        (char *)pdn + pdn->wOutputOffset);
      GlobalUnlock(hdevnames);
    }
}

/* A macro for converting 10ths of millimeters to 1000ths of inches.  */
#define MM_TO_MINCH(x) ( (x) / 0.0254 )
#define TENTH_MM_TO_MINCH(x) ( (x) / 0.254 )
#define MINCH_TO_TENTH_MM(x) ( 0.254  * (x) )

static const struct paper_size { int size; long wid; long len; } paper_sizes[] = {
  { DMPAPER_LETTER, 8500, 11000 },
  { DMPAPER_LEGAL, 8500, 14000 },
  { DMPAPER_A4, (long)MM_TO_MINCH(210), (long)MM_TO_MINCH(297) },
  { DMPAPER_CSHEET, 17000, 22000 },
  { DMPAPER_DSHEET, 22000, 34000 },
  { DMPAPER_ESHEET, 34000, 44000 },
  { DMPAPER_LETTERSMALL, 8500, 11000 },
  { DMPAPER_TABLOID, 11000, 17000 },
  { DMPAPER_LEDGER, 17000, 11000 },
  { DMPAPER_STATEMENT, 5500, 8500 },
  { DMPAPER_A3, (long)MM_TO_MINCH(297), (long)MM_TO_MINCH(420) },
  { DMPAPER_A4SMALL, (long)MM_TO_MINCH(210), (long)MM_TO_MINCH(297) },
  { DMPAPER_A5, (long)MM_TO_MINCH(148), (long)MM_TO_MINCH(210) },
  { DMPAPER_B4, (long)MM_TO_MINCH(250), (long)MM_TO_MINCH(354) },
  { DMPAPER_B5, (long)MM_TO_MINCH(182), (long)MM_TO_MINCH(257) },
  { DMPAPER_FOLIO, 8500, 13000 },
  { DMPAPER_QUARTO, (long)MM_TO_MINCH(215), (long)MM_TO_MINCH(275) },
  { DMPAPER_10X14, 10000, 14000 },
  { DMPAPER_11X17, 11000, 17000 },
  { DMPAPER_NOTE, 8500, 11000 },
  { DMPAPER_ENV_9, 3875, 8875 },
  { DMPAPER_ENV_10, 4125, 9500 },
  { DMPAPER_ENV_11, 4500, 10375 },
  { DMPAPER_ENV_12, 4750, 11000 },
  { DMPAPER_ENV_14, 5000, 11500 },
  { DMPAPER_ENV_DL, (long)MM_TO_MINCH(110), (long)MM_TO_MINCH(220) },
  { DMPAPER_ENV_C5, (long)MM_TO_MINCH(162), (long)MM_TO_MINCH(229) },
  { DMPAPER_ENV_C3, (long)MM_TO_MINCH(324), (long)MM_TO_MINCH(458) },
  { DMPAPER_ENV_C4, (long)MM_TO_MINCH(229), (long)MM_TO_MINCH(324) },
  { DMPAPER_ENV_C6, (long)MM_TO_MINCH(114), (long)MM_TO_MINCH(162) },
  { DMPAPER_ENV_C65, (long)MM_TO_MINCH(114), (long)MM_TO_MINCH(229) },
  { DMPAPER_ENV_B4, (long)MM_TO_MINCH(250), (long)MM_TO_MINCH(353) },
  { DMPAPER_ENV_B5, (long)MM_TO_MINCH(176), (long)MM_TO_MINCH(250) },
  { DMPAPER_ENV_B6, (long)MM_TO_MINCH(176), (long)MM_TO_MINCH(125) },
  { DMPAPER_ENV_ITALY, (long)MM_TO_MINCH(110), (long)MM_TO_MINCH(230) },
  { DMPAPER_ENV_MONARCH, 3825, 7500 },
  { DMPAPER_ENV_PERSONAL, 3625, 6500 },
  { DMPAPER_FANFOLD_US, 14825, 11000 },
  { DMPAPER_FANFOLD_STD_GERMAN, 8500, 12000 },
  { DMPAPER_FANFOLD_LGL_GERMAN, 8500, 13000 },
};


/*
 *----------------------------------------------------------------------
 *
 * GetDevModeAttribs  --
 *
 *  Sets the devmode copy based on the attributes (syncronization).
 *
 * Results:
 *	Sets devmode copy.
 *
 *----------------------------------------------------------------------
 */

static void GetDevModeAttribs (Tcl_HashTable *att, DEVMODE *dm)
{
  /* This function sets the devmode based on the attributes.
   *  The attributes set are:
   *  page orientation
   *    Paper sizes (Added 8/1/02 by Jon Hilbert)
   * 
   *  Still needed:
   *    Scale
   *    Paper names
   *    Print quality
   *    duplexing
   *    font downloading
   *    collation
   *    gray scale
   *    ??Print to file
   * 
   *  Taken care of elsewhere
   *    #copies
   .  */
  const char *cp;
    
  if ( cp = get_attribute(att, "page orientation") )
    {
      dm->dmFields |= DM_ORIENTATION;
      if ( strcmp(cp, "portrait") == 0 )
	dm->dmOrientation = DMORIENT_PORTRAIT;
      else
	dm->dmOrientation = DMORIENT_LANDSCAPE;
    } 
  /* --------------  added 8/1/02 by Jon Hilbert; modified 2/24/03 by Jon Hilbert.  */
  else if ( cp = get_attribute(att, "page dimensions") )
    {
      long width,length;
      dm->dmFields |= (DM_PAPERLENGTH | DM_PAPERWIDTH | DM_PAPERSIZE );
      sscanf(cp, "%ld %ld", &width, &length);
      dm->dmPaperWidth = (short)MINCH_TO_TENTH_MM(width);
      dm->dmPaperLength = (short)MINCH_TO_TENTH_MM(length);
      // indicate that size is specified by dmPaperWidth,dmPaperLength
      dm->dmPaperSize = 0;  
    }  
}

/*
 *----------------------------------------------------------------------
 *
 * SetDevModeAttribs  --
 *
 *  Copy attributes from devmode in dialog to attribute hash table.
 *
 * Results:
 *	Sets attributes.
 *
 *----------------------------------------------------------------------
 */


static void SetDevModeAttribs (Tcl_HashTable *att, DEVMODE *dm)
{
  char tmpbuf[2*11+2+1];
    
  /*
   *  Some printers print multiple copies--if so, the devmode carries the number
   *  of copies, while ppv->pdlg->nCopies may be set to one.
   *  We wish the user to see the number of copies.
   */
  sprintf(tmpbuf, "%d", dm->dmCopies);
  set_attribute(att, "copies", tmpbuf);
    
  /* Everything depends on what flags are set.  */
  if ( dm->dmDeviceName[0] )
    set_attribute(att, "device", dm->dmDeviceName);
  if ( dm->dmFields & DM_ORIENTATION )
    set_attribute(att, "page orientation", 
		  dm->dmOrientation==DMORIENT_PORTRAIT?"portrait":"landscape");
  if ( dm->dmFields & DM_YRESOLUTION )
    {
      sprintf(tmpbuf, "%d %d", dm->dmYResolution, dm->dmPrintQuality);
      set_attribute(att, "resolution", tmpbuf);
    }
  else if ( dm->dmFields & DM_PRINTQUALITY)
    {
      /* The result may be positive (DPI) or negative (preset value).  */
      if ( dm->dmPrintQuality > 0 )
        {
	  sprintf(tmpbuf, "%d %d", dm->dmPrintQuality, dm->dmPrintQuality);
	  set_attribute(att, "resolution", tmpbuf);
        }
      else
        {
	  static struct PrinterQuality {
	    short res;
	    const char *desc;
	  } print_quality[] =
            {
	      { DMRES_HIGH, "High" },
	      { DMRES_MEDIUM, "Medium" },
	      { DMRES_LOW, "Low" },
	      { DMRES_DRAFT, "Draft" }
            };
	  int i;
	  const char *cp = "Unknown";
            
	  for (i = 0; i < sizeof(print_quality) / sizeof(struct PrinterQuality); i++)
            {
	      if ( print_quality[i].res == dm->dmPrintQuality )
                {
		  cp = print_quality[i].desc;
		  break;
                }
            }
	  set_attribute(att, "resolution", cp);
        }
    }
    
  /* If the page size is provided by the paper size, use the page size to update
   *  the previous size from the HDC.
   */
  if ( (dm->dmFields & DM_PAPERLENGTH) && (dm->dmFields & DM_PAPERWIDTH ) )
    {
      sprintf(tmpbuf, "%ld %ld", (long)TENTH_MM_TO_MINCH(dm->dmPaperWidth),
	      (long)TENTH_MM_TO_MINCH(dm->dmPaperLength) );
      set_attribute(att, "page dimensions", tmpbuf);
    }
  else if ( dm->dmFields & DM_PAPERSIZE )
    {
      /* If we are in this case, we must also check for landscape vs. portrait;
       *  unfortunately, Windows does not distinguish properly in this subcase
       .  */
      int i;
      for ( i=0; i < sizeof(paper_sizes)/sizeof (struct paper_size); i++)
        {
	  if ( paper_sizes[i].size == dm->dmPaperSize )
            {
	      if ( dm->dmOrientation == DMORIENT_PORTRAIT )
                {
		  sprintf(tmpbuf, "%ld %ld", paper_sizes[i].wid, paper_sizes[i].len);
		  set_attribute(att, "page dimensions", tmpbuf);
                }
	      else if ( dm->dmOrientation == DMORIENT_LANDSCAPE )
                {
		  sprintf(tmpbuf, "%ld %ld", paper_sizes[i].len, paper_sizes[i].wid);
		  set_attribute(att, "page dimensions", tmpbuf);
                }
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SetDevNamesAttribs  --
 *
 *  Converts dialog terms to attributes.
 *
 * Results:
 *	Sets attributes.
 *
 *----------------------------------------------------------------------
 */

static void SetDevNamesAttribs (Tcl_HashTable *att, struct printer_values *dn)
{
  /* Set the "device", "driver" and "port" attributes - (belt and suspenders).  */
  if (dn->devnames_printername != NULL && strlen(dn->devnames_printername) > 0 )
    set_attribute(att,"device",dn->devnames_printername);
  if (dn->devnames_filename != NULL && strlen(dn->devnames_filename)>0)
    set_attribute(att,"driver",dn->devnames_filename);
  if (dn->devnames_port != NULL && strlen(dn->devnames_port)>0)
    set_attribute(att,"port",dn->devnames_port);
}

/*
 *----------------------------------------------------------------------
 *
 * GetPageDlgAttribs --
 *
 *  Gets page dialog attributes.
 *
 * Results:
 *	Gets attributes.
 *
 *----------------------------------------------------------------------
 */

static void GetPageDlgAttribs (Tcl_HashTable *att, PAGESETUPDLG *pgdlg)
{
  const char *cp;
    
  if ( cp = get_attribute(att, "page margins") ) {
    RestorePageMargins(cp, pgdlg);
  }
    
}

/*
 *----------------------------------------------------------------------
 *
 * GetPrintDlgAttribs--
 *
 *  Gets print dialog attributes.
 *
 * Results:
 *	Gets attributes.
 *
 *----------------------------------------------------------------------
 */
 
static void GetPrintDlgAttribs (Tcl_HashTable *att, PRINTDLG *pdlg)
{
  const char *cp;
    
  if ( cp = get_attribute(att, "copies") )
    pdlg->nCopies = atoi(cp);
    
  /* Add minimum and maximum page numbers to enable print page selection.  */
  if ( cp = get_attribute(att, "minimum page") )
    {
      pdlg->nMinPage = atoi(cp);
      if ( pdlg->nMinPage <= 0 )
	pdlg->nMinPage = 1;
    }
    
  if ( cp = get_attribute(att, "maximum page") )
    {
      pdlg->nMaxPage = atoi(cp);
      if ( pdlg->nMaxPage < pdlg->nMinPage )
	pdlg->nMaxPage = pdlg->nMinPage;
    }
    
  if ( cp = get_attribute(att, "first page") )
    {
      pdlg->nFromPage = atoi(cp);
      if (pdlg->nFromPage > 0)
        {
	  pdlg->Flags &= (~PD_ALLPAGES);
	  pdlg->Flags |= PD_PAGENUMS;
	  if ( pdlg->nMinPage > pdlg->nFromPage )
	    pdlg->nMinPage = 1;
        }
    }
    
  if ( cp = get_attribute(att, "last page") )
    {
      pdlg->nToPage   = atoi(cp);
      if ( pdlg->nToPage > 0 )
        {
	  pdlg->Flags &= (~PD_ALLPAGES);
	  pdlg->Flags |= PD_PAGENUMS;
	  if ( pdlg->nMaxPage < pdlg->nToPage )
	    pdlg->nMaxPage = pdlg->nToPage;
        }
    }
    
  /* Added to match the radiobuttons on the windows dialog.  */
  if ( cp = get_attribute(att, "print flag" ) )
    {
      if (lstrcmpi(cp, "all") == 0 )
	pdlg->Flags &= (~(PD_PAGENUMS|PD_SELECTION));
      else if ( lstrcmpi(cp, "selection") == 0 )
        {
	  pdlg->Flags |= PD_SELECTION;
	  pdlg->Flags &= (~(PD_PAGENUMS|PD_NOSELECTION));
        }
      else if ( lstrcmpi(cp, "pagenums") == 0 )
        {
	  pdlg->Flags |= PD_PAGENUMS;
	  pdlg->Flags &= (~(PD_SELECTION|PD_NOPAGENUMS));
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SetPrintDlgAttribs--
 *
 *  Sets print dialog attributes.
 *
 * Results:
 *	Sets attributes.
 *
 *----------------------------------------------------------------------
 */
 
static void SetPrintDlgAttribs (Tcl_HashTable *att, PRINTDLG *pdlg)
{
  char tmpbuf[11+1];
    
  /* 
   *  This represents the number of copies the program is expected to spool
   *  (e.g., if collation is on)
   .  */
  sprintf(tmpbuf, "%d", pdlg->nCopies);
  set_attribute(att, "copiesToSpool", tmpbuf);
    
  /* Set the to and from page if they are nonzero.  */
  if ( pdlg->nFromPage > 0 )
    {
      sprintf(tmpbuf, "%d", pdlg->nFromPage);
      set_attribute(att, "first page", tmpbuf);
    }
    
  if ( pdlg->nToPage > 0 )
    {
      sprintf(tmpbuf, "%d", pdlg->nToPage);
      set_attribute(att, "last page", tmpbuf);
    }
    
  if ( pdlg->Flags & PD_PAGENUMS )
    set_attribute(att, "print flag", "pagenums");
  else if ( pdlg->Flags & PD_SELECTION )
    set_attribute(att, "print flag", "selection");
  else if ( ( pdlg->Flags & (PD_PAGENUMS | PD_SELECTION)) == 0 )
    set_attribute(att, "print flag", "all");
}

/*
 *----------------------------------------------------------------------
 *
 * SetPageSetupDlgAttribs--
 *
 *  Sets page setup dialog attributes.
 *
 * Results:
 *	Sets attributes.
 *
 *----------------------------------------------------------------------
 */
 
static void SetPageSetupDlgAttribs (Tcl_HashTable *att, PAGESETUPDLG *pgdlg)
{
  char tmpbuf[4*11 + 3 + 1];
  /* According to the PAGESETUPDLG page, the paper size and margins may be
   *  provided in locale-specific units. We want thousandths of inches
   *  for consistency at this point. Look for the flag:
   .  */
  int metric = (pgdlg->Flags & PSD_INHUNDREDTHSOFMILLIMETERS)?1:0;
  double factor = 1.0;
    
  if ( metric )
    factor = 2.54;
    
  sprintf(tmpbuf, "%ld %ld", (long)(pgdlg->ptPaperSize.x / factor), 
	  (long)(pgdlg->ptPaperSize.y / factor));
  set_attribute(att, "page dimensions", tmpbuf);
  sprintf(tmpbuf, "%ld %ld %ld %ld", (long)(pgdlg->rtMargin.left / factor),  
	  (long)(pgdlg->rtMargin.top / factor),
	  (long)(pgdlg->rtMargin.right / factor), 
	  (long)(pgdlg->rtMargin.bottom / factor));
  set_attribute(att, "page margins", tmpbuf);
  sprintf(tmpbuf, "%ld %ld %ld %ld", (long)(pgdlg->rtMinMargin.left / factor),  
	  (long)(pgdlg->rtMinMargin.top / factor),
	  (long)(pgdlg->rtMinMargin.right / factor), 
	  (long)(pgdlg->rtMinMargin.bottom / factor));
  set_attribute(att, "page minimum margins", tmpbuf);
}

/*
 *----------------------------------------------------------------------
 *
 * SetHDCAttribs --
 *
 *  Sets HDC attributes.
 *
 * Results:
 *	Sets attributes.
 *
 *----------------------------------------------------------------------
 */
 
static void SetHDCAttribs (Tcl_HashTable *att, HDC hDC)
{
  char tmpbuf[2*11+2+1];
  int hsize, vsize, hscale, vscale, hoffset, voffset, hppi, vppi;
    
  sprintf(tmpbuf, "0x%lx", hDC);
  set_attribute(att, "hDC", tmpbuf);
    
  if ( PrintPageAttr(hDC, &hsize, &vsize, 
		     &hscale, &vscale, 
		     &hoffset, &voffset, 
		     &hppi, &vppi) == 0 &&
       hppi > 0 && vppi > 0 )
    {
      sprintf(tmpbuf, "%d %d", (int)(hsize*1000L/hppi), (int)(vsize*1000L/vppi));
      set_attribute(att, "page dimensions", tmpbuf);
      sprintf(tmpbuf, "%d %d", hppi, vppi);
      set_attribute(att, "pixels per inch", tmpbuf);
        
      /* Perhaps what's below should only be done if not already set....  */
      sprintf(tmpbuf, "%d %d %d %d", (int)(hoffset*1000L/hppi), (int)(voffset*1000L/vppi),
	      (int)(hoffset*1000L/hppi), (int)(voffset*1000L/vppi));
      set_attribute(att, "page minimum margins", tmpbuf);
      set_attribute(att, "page margins", "1000 1000 1000 1000");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * StorePrintVals --
 *
 *  Stores the new DEVMODE and DEVNAMES structures
 *  if needed, and converts relevant portions of the structures
 *  to attribute/value pairs.
 *
 * Results:
 *	Sets attributes.
 *
 *----------------------------------------------------------------------
 */
 
static void StorePrintVals(struct printer_values *ppv, PRINTDLG *pdlg, PAGESETUPDLG *pgdlg)
{

  /* 
   *  If pdlg or pgdlg are nonzero, attribute/value pairs are
   *  extracted from them as well.
   *  A companion function is intended to convert attribute/value
   *  pairs in the ppv->attribs hash table to set the appropriate
   *  dialog values.
   *  All values in the hash table are strings to simplify getting
   *  and setting by the user; the job of converting to and from
   *  the platform-specific notion is left to the conversion function.
   */

  /* First, take care of the hDC structure.  */
  if ( pdlg != NULL )
    {
      const char *cp;
      if ( ppv->hDC != NULL )
        {
	  delete_dc (ppv->hDC);
	  DeleteDC(ppv->hDC);
        }
      if ( ppv->hdcname[0] != '\0')
        {
	  if (hdc_delete)
	    hdc_delete(0, ppv->hdcname);
	  ppv->hdcname[0] = '\0';
        }
      ppv->hDC = pdlg->hDC;
      /* Only need to do this if the hDC has changed.  */
      if (ppv->hDC)
        {
	  SetHDCAttribs(&ppv->attribs, ppv->hDC);
	  if (cp = make_printer_dc_name(0, ppv->hDC, ppv))
            {
	      strncpy(ppv->hdcname, cp, sizeof (current_printer_values->hdcname));
	      set_attribute(&ppv->attribs, "hdcname", cp);
            }
	  ppv->hdcname[sizeof (current_printer_values->hdcname) - 1] = '\0';
        }
    }
    
  /* Next, get the DEVMODE out of the pdlg if present;
   *  if not, try the page dialog; if neither, skip this step
   .  */
  if ( pdlg != NULL && pdlg->hDevMode != NULL)
    {
      MakeDevmode(ppv, pdlg->hDevMode);
      GlobalFree(pdlg->hDevMode);
      pdlg->hDevMode = NULL;
      SetDevModeAttribs(&ppv->attribs, ppv->pdevmode);
    }
  else if (pgdlg != NULL && pgdlg->hDevMode != NULL)
    {
      MakeDevmode (ppv, pgdlg->hDevMode);
      GlobalFree(pgdlg->hDevMode);
      pgdlg->hDevMode = NULL;
      SetDevModeAttribs(&ppv->attribs, ppv->pdevmode);
    }
    
  /* Next, get the DEVNAMES out of the pdlg if present;
   *  if not, try the page dialog; if neither, skip this step
   .  */
  if ( pdlg != NULL && pdlg->hDevNames != NULL)
    {
      CopyDevnames(ppv, pdlg->hDevNames);
      GlobalFree(pdlg->hDevNames);
      pdlg->hDevNames = NULL;
      SetDevNamesAttribs(&ppv->attribs, ppv);
    }
  else if (pgdlg != NULL && pgdlg->hDevNames != NULL)
    {
      CopyDevnames(ppv, pgdlg->hDevNames);
      GlobalFree(pgdlg->hDevNames);
      pgdlg->hDevNames = NULL;
      SetDevNamesAttribs(&ppv->attribs, ppv);
    }
    
  /* Set attributes peculiar to the print dialog.  */
  if (pdlg != NULL)
    SetPrintDlgAttribs(&ppv->attribs, pdlg);
    
  /* Set attributes peculiar to the page setup dialog.  */
  if (pgdlg != NULL)
    SetPageSetupDlgAttribs(&ppv->attribs, pgdlg);
}


/*
 *----------------------------------------------------------------------
 *
 * RestorePageMargins  --
 *
 *  Restores page margins.
 *
 * Results:
 *	Page margins are restored.
 *
 *----------------------------------------------------------------------
 */

static void RestorePageMargins (const char *attrib, PAGESETUPDLG *pgdlg)
{

  /*
   * This function is domain-specific (in the longer term, probably
   *  an attribute to determine read-only vs. read-write and which
   *  dialog it's relevant to and a function to do the conversion
   *  would be appropriate).
   *  Fix for metric measurements submitted by Michael Thomsen <miksen@ideogramic.com>.
   */
  RECT r;
  double left, top, right, bottom;
    
  /* According to the PAGESETUPDLG page, the paper size and margins may be
   *  provided in locale-specific units. We want thousandths of inches
   *  for consistency at this point. Look for the flag:
   .  */
  int metric = (default_printer_values.pgdlg.Flags & PSD_INHUNDREDTHSOFMILLIMETERS)?1:0;
  double factor = 1.0;
    
  if ( metric )
    factor = 2.54;
    
  if ( sscanf(attrib, "%lf %lf %lf %lf", &left, &top, &right, &bottom) == 4 ) {
    r.left   = (long) (floor(left  * factor + 0.5));
    r.top    = (long) (floor(top  * factor + 0.5));
    r.right  = (long) (floor(right  * factor + 0.5));
    r.bottom = (long) (floor(bottom  * factor + 0.5));
    pgdlg->rtMargin = r;
    pgdlg->Flags |= PSD_MARGINS|PSD_INTHOUSANDTHSOFINCHES;  
  }
}

/*
 *----------------------------------------------------------------------
 *
 * RestorePrintVals  --
 *
 *  Sets the attributes in ppv->attribs into the
 *  print dialog or page setup dialog as requested.
 *
 * Results:
 *	Sets attributes.
 *
 *----------------------------------------------------------------------
 */
 
static void RestorePrintVals (struct printer_values *ppv, PRINTDLG *pdlg, PAGESETUPDLG *pgdlg)
{
  if (pdlg)
    {
      /*
       *  Values to be restored:
       *  copies
       *  first page
       *  last page
       .  */
      GetPrintDlgAttribs(&ppv->attribs, pdlg);
        
      /* Note: if DEVMODE is not null, copies is taken from the DEVMODE structure.  */
      if (ppv->pdevmode )
	ppv->pdevmode->dmCopies = pdlg->nCopies;
        
    }
    
  if (pgdlg)
    {
      /*
       *  Values to be restored:
       *  page margins
       .  */
      GetPageDlgAttribs(&ppv->attribs, pgdlg);
    }
}

/* 
 *  To make the print command easier to extend and administer,
 *  the subcommands are in a table.
 *  Since I may not make the correct assumptions about what is
 *  considered safe and unsafe, this is parameterized in the
 *  function table.
 *  For now the commands will be searched linearly (there are only
 *  a few), but keep them sorted, so a binary search could be used.
 */
typedef int (*tcl_prtcmd) (ClientData, Tcl_Interp *, int, const char  * );
struct prt_cmd
{
  const char *name;
  tcl_prtcmd func;
  int safe;
};

static struct prt_cmd printer_commands[] =
  {
    { "attr",    PrintAttr,    1 },
    { "close",   PrintClose,   1 },
    { "dialog",  PrintDialog,  1 },
    { "job",     PrintJob,     1 },
    { "list",    PrintList,    1 },
    { "open",    PrintOpen,    1 },
    { "option",  PrintOption,  0 },
    { "page",    PrintPage,    1 },
    { "send",    PrintSend,    1 },
    { "version", Version,      1 },
  };

/* 
 *  We can also build the global usage message dynamically.
 */
static void top_usage_message(Tcl_Interp *interp, int argc, const char  * argv, int safe)
{
  int i;
  int last = sizeof printer_commands / sizeof (struct prt_cmd);
  int first=1;
  Tcl_AppendResult(interp, "printer [", 0);
  for (i=0; i < last; i++)
    {
      if ( printer_commands[i].safe >= safe )
        {
	  if (first)
            {
	      Tcl_AppendResult(interp, " ", printer_commands[i].name, 0);
	      first = 0;
            }
	  else
	    Tcl_AppendResult(interp, " | ", printer_commands[i].name, 0);
        }
      if ( i == (last - 1) )
	Tcl_AppendResult(interp, " ]", 0);
    }
  if (argc)
    {
      Tcl_AppendResult(interp, "\n(Bad command: ", 0 );
      for (i=0; i<argc; i++)
	Tcl_AppendResult(interp, argv[i], " ", 0 );
      Tcl_AppendResult(interp, ")", 0);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Print  --
 *
 *  Takes the print command, parses it, and calls
 *  the correct subfunction.
 *
 * Results:
 *	Executes print command/subcommand.
 *
 *----------------------------------------------------------------------
 */

static int Print (ClientData defaults, Tcl_Interp *interp, int argc, const char  * argv, int safe)
{
  int i;
  if ( argc == 0 )
    {
      top_usage_message(interp, argc+1, argv-1, safe);
      return TCL_ERROR;
    }
    
  /* 
   * Linear search for now--could be a binary search. 
   * Exact match for now--could be case-insensitive, leading match. 
   */
  for (i=0; i < (sizeof printer_commands / sizeof (struct prt_cmd) ); i++)
    if ( printer_commands[i].safe >= safe )
      if ( strcmp(argv[0], printer_commands[i].name) == 0 )
	return printer_commands[i].func(defaults, interp, argc-1, argv+1);
    
  top_usage_message(interp, argc+1, argv-1, safe);
  return TCL_ERROR;  
}


/*
 *----------------------------------------------------------------------
 *
 * printer  --
 *
 *  Core command.
 *
 * Results:
 *	Executes print command/subcommand.
 *
 *----------------------------------------------------------------------
 */

static int printer (ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  if ( argc > 1 )
    {
      argv++;
      argc--;
      return Print(data, interp, argc, argv, 0);
    }
    
  top_usage_message(interp, argc, argv, 0);
  return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Winprint_Init  --
 *
 *  Initializes this command.
 *
 * Results:
 *	Command is initialized.
 *
 *----------------------------------------------------------------------
 */
 
int Winprint_Init(Tcl_Interp * interp) {

  Tcl_CreateObjCommand(interp, "::tk::print::_print", printer,
		       (ClientData)( & current_printer_values), 0);

  /* Initialize the attribute hash table.  */
  init_printer_dc_contexts(interp);

  /* Initialize the attribute hash table.  */
  Tcl_InitHashTable( & (current_printer_values -> attribs), TCL_STRING_KEYS);

  /* Initialize the list of HDCs hash table.  */
  Tcl_InitHashTable( & printer_hdcs, TCL_ONE_WORD_KEYS);

  /* Initialize the default page settings.  */
  current_printer_values -> pgdlg.lStructSize = sizeof(PAGESETUPDLG);
  current_printer_values -> pgdlg.Flags |= PSD_RETURNDEFAULT;

  return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * SplitDevice  --
 *
 *   Divide the default printing device into its component parts.
 *
 * Results:
 *	Device components are returned.
 *
 *----------------------------------------------------------------------
 */

static int SplitDevice(LPSTR device, LPSTR *dev, LPSTR *dvr, LPSTR *port)
{
  static char buffer[256];
  if (device == 0 )
    {
      switch ( WinVersion() )
        {
        case VER_PLATFORM_WIN32s:
	  GetProfileString("windows", "device", "", (LPSTR)buffer, sizeof buffer);
	  device = (LPSTR)buffer;
	  break;
        case VER_PLATFORM_WIN32_WINDOWS:
        case VER_PLATFORM_WIN32_NT:
        default:
	  device = (LPSTR)"WINSPOOL,Postscript,";
	  break;
        }
    }
    
  *dev = strtok(device, ",");
  *dvr = strtok(NULL, ",");
  *port = strtok(NULL, ",");
    
  if (*dev)
    while (  * dev == ' ')
      (*dev)++;
  if (*dvr)
    while (  * dvr == ' ')
      (*dvr)++;
  if (*port)
    while (  * port == ' ')
      (*port)++;
    
  return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GetPrinterDC --
 *
 *   Build a compatible printer DC for the default printer.
 *
 * Results:
 *	Returns DC.
 *
 *----------------------------------------------------------------------
 */


static HDC GetPrinterDC (const char *printer)
{
  HDC hdcPrint;
    
  LPSTR lpPrintDevice = "";
  LPSTR lpPrintDriver = "";
  LPSTR lpPrintPort   = "";
    
  SplitDevice ((LPSTR)printer, &lpPrintDevice, &lpPrintDriver, &lpPrintPort);
  switch ( WinVersion() )
    {
    case VER_PLATFORM_WIN32s:
      hdcPrint = CreateDC (lpPrintDriver,
			   lpPrintDevice,
			   lpPrintPort,
			   NULL);
      break;
    case VER_PLATFORM_WIN32_WINDOWS:
    case VER_PLATFORM_WIN32_NT:
    default:
      hdcPrint = CreateDC (lpPrintDriver, 
			   lpPrintDevice, 
			   NULL, 
			   NULL);
      break;
    }
    
  return hdcPrint;
}

/* End of support for file printing.  */


/*
 *----------------------------------------------------------------------
 *
 * PrintStatusToStr --
 *
 *   Convert a status code to a string.
 *   Function created by Brian Griffin <bgriffin@model.com>
 *
 * Results:
 *	Returns status code.
 *
 *----------------------------------------------------------------------
 */

static const char *PrintStatusToStr( DWORD status ) 
{
  switch (status) {
  case PRINTER_STATUS_PAUSED:            return "Paused";
  case PRINTER_STATUS_ERROR:             return "Error";
  case PRINTER_STATUS_PENDING_DELETION:  return "Pending Deletion";
  case PRINTER_STATUS_PAPER_JAM:         return "Paper jam";
  case PRINTER_STATUS_PAPER_OUT:         return "Paper out";
  case PRINTER_STATUS_MANUAL_FEED:       return "Manual feed";
  case PRINTER_STATUS_PAPER_PROBLEM:     return "Paper problem";
  case PRINTER_STATUS_OFFLINE:           return "Offline";
  case PRINTER_STATUS_IO_ACTIVE:         return "IO Active";
  case PRINTER_STATUS_BUSY:              return "Busy";
  case PRINTER_STATUS_PRINTING:          return "Printing";
  case PRINTER_STATUS_OUTPUT_BIN_FULL:   return "Output bit full";
  case PRINTER_STATUS_NOT_AVAILABLE:     return "Not available";
  case PRINTER_STATUS_WAITING:           return "Waiting";
  case PRINTER_STATUS_PROCESSING:        return "Processing";
  case PRINTER_STATUS_INITIALIZING:      return "Initializing";
  case PRINTER_STATUS_WARMING_UP:        return "Warming up";
  case PRINTER_STATUS_TONER_LOW:         return "Toner low";
  case PRINTER_STATUS_NO_TONER:          return "No toner";
  case PRINTER_STATUS_PAGE_PUNT:         return "Page punt";
  case PRINTER_STATUS_USER_INTERVENTION: return "User intervention";
  case PRINTER_STATUS_OUT_OF_MEMORY:     return "Out of memory";
  case PRINTER_STATUS_DOOR_OPEN:         return "Door open";
  case PRINTER_STATUS_SERVER_UNKNOWN:    return "Server unknown";
  case PRINTER_STATUS_POWER_SAVE:        return "Power save";
  case 0:                                return "Ready";
  default:                               break;
  }
  return "Unknown";
}

/*
 *----------------------------------------------------------------------
 *
 * PrintList --
 *
 *  Returns the list of available printers in
 *  a format convenient for the print command.
 *  Brian Griffin <bgriffin@model.com> suggested and implemented
 *  the -verbose flag, and the new Win32 implementation.
 *
 * Results:
 *	Returns printer list.
 *
 *----------------------------------------------------------------------
 */
 
static int PrintList (ClientData unused, Tcl_Interp *interp, int argc, const char  * argv)
{
  char *usgmsg = "::tk::print::_print list [-match matchstring] [-verbose]";
  const char *match = 0;
  const char *illegal = 0;
    
  /* The following 3 declarations are only needed for the Win32s case.  */
  static char devices_buffer[256];
  static char value[256];
  char *cp;
    
  int i;
  int verbose = 0;
    
  for (i=0; i<argc; i++)
    {
      if (strcmp(argv[i], "-match") == 0)
	match = argv[++i];
      else if ( strcmp(argv[i], "-verbose") == 0 )
	verbose = 1;
      else
	illegal = argv[i];
    }
    
  if (illegal)
    {
      Tcl_SetResult(interp, usgmsg, TCL_STATIC);
      return TCL_ERROR;
    }
    
  /* 
   * The result should be useful for specifying the devices and/or OpenPrinter and/or lp -d. 
   * Rather than make this compilation-dependent, do a runtime check.  
   */
  switch ( WinVersion() )
    {
    case VER_PLATFORM_WIN32_NT:        /* Windows NT.  */
    default:
      /* Win32 implementation uses EnumPrinters.  */
      /* There is a hint in the documentation that this info is stored in the registry.
       *  if so, that interface would probably be even better!
       *  NOTE: This implementation was suggested by Brian Griffin <bgriffin@model.com>,
       *        and replaces the older implementation which used PRINTER_INFO_4,5
       .  */
      {
	DWORD bufsiz = 0;
	DWORD needed = 0;
	DWORD num_printers = 0;
	PRINTER_INFO_2 *ary = 0;
	DWORD i;
            
	/* First, get the size of array needed to enumerate the printers.  */
	if ( EnumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_FAVORITE, 
			  NULL, 
			  2, (LPBYTE)ary, 
			  bufsiz, &needed, 
			  &num_printers) == FALSE )
	  {
	    /* Expected failure--we didn't allocate space.  */
	    DWORD err = GetLastError();
	    /* If the error isn't insufficient space, we have a real problem..  */
	    if ( err != ERROR_INSUFFICIENT_BUFFER )
	      {
		sprintf (msgbuf, "EnumPrinters: unexpected error code: %ld", (long)err);
		Tcl_SetResult(interp, msgbuf, TCL_VOLATILE);
		return TCL_ERROR;
	      }
	  }
            
	if ( needed > 0 ) {
	  if ( (ary = (PRINTER_INFO_2 *)Tcl_Alloc(needed) ) != 0 )
	    bufsiz = needed;
	  else
	    {
	      sprintf (msgbuf, "EnumPrinters: Out of memory in request for %ld bytes", (long)needed);
	      Tcl_SetResult(interp, msgbuf, TCL_VOLATILE);
	      return TCL_ERROR;
	    }
	} else {  /* No printers to report!.  */
	  return TCL_OK;
	}
            
	/* Now that we know how much, allocate it -- if there is a printer!.  */
	if ( EnumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_FAVORITE, NULL, 
			  2, (LPBYTE)ary, 
			  bufsiz, &needed, 
			  &num_printers) == FALSE )
	  {
	    /* Now we have a real failure!.  */
	    sprintf(msgbuf, "::tk::print::_print list: Cannot enumerate printers: %ld", (long)GetLastError());
	    Tcl_SetResult(interp, msgbuf, TCL_VOLATILE);
	    return TCL_ERROR;
	  }
            
	/* Question for UTF: Do I need to convert all visible output?
	 *  Or just the printer name and location?
	 .  */
            
	/* Question for Win95: Do I need to provide the port number?.  */
	for (i=0; i<num_printers; i++) 
	  {
	    if (match == 0 || Tcl_StringMatch(ary[i].pPrinterName, match) ||
		Tcl_StringMatch(ary[i].pPortName,    match) )
	      {
		if (verbose)
		  {
		    Tcl_AppendResult(interp, "{", 0);  /* New list for each printer.  */
		    /* The verbose list is a set of name/value pairs.  */
		    Tcl_AppendResult(interp, "{", 0);
		    Tcl_AppendElement(interp, "Name");
#if TCL_MAJOR_VERSION > 8 || ( TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION >= 1 )
		    {
		      const char *ostring;
		      Tcl_DString tds;
		      Tcl_DStringInit(&tds);
		      Tcl_UtfToExternalDString(NULL, ary[i].pPrinterName, -1, &tds);
		      ostring = Tcl_DStringValue(&tds);
		      Tcl_AppendElement(interp, ostring);
		      Tcl_DStringFree(&tds);
		    }
#else
		    Tcl_AppendElement(interp, ary[i].pPrinterName);
#endif
		    Tcl_AppendResult(interp, "} ", 0);
		    Tcl_AppendResult(interp, "{", 0);
		    Tcl_AppendElement(interp, "Status");
		    Tcl_AppendElement(interp, PrintStatusToStr(ary[i].Status) );
		    Tcl_AppendResult(interp, "} ", 0);
		    if ( ary[i].pDriverName && ary[i].pDriverName[0] != '\0')
		      {
			Tcl_AppendResult(interp, "{", 0);
			Tcl_AppendElement(interp, "Driver");
			Tcl_AppendElement(interp, ary[i].pDriverName );
			Tcl_AppendResult(interp, "} ", 0);
		      }
		    if ( ary[i].pServerName && ary[i].pServerName[0] != '\0')
		      {
			Tcl_AppendResult(interp, "{", 0);
			Tcl_AppendElement(interp, "Control");
			Tcl_AppendElement(interp, "Server" );
			Tcl_AppendResult(interp, "} ", 0);
			Tcl_AppendResult(interp, "{", 0);
			Tcl_AppendElement(interp, "Server");
			Tcl_AppendElement(interp, ary[i].pServerName );
			Tcl_AppendResult(interp, "} ", 0);
		      }
		    else
		      {
			Tcl_AppendResult(interp, "{", 0);
			Tcl_AppendElement(interp, "Control");
			Tcl_AppendElement(interp, "Local" );
			Tcl_AppendResult(interp, "} ", 0);
			Tcl_AppendResult(interp, "{", 0);
			Tcl_AppendElement(interp, "Port");
			Tcl_AppendElement(interp, ary[i].pPortName );
			Tcl_AppendResult(interp, "} ", 0);
		      }
		    if ( ary[i].pLocation && ary[i].pLocation[0] != '\0')
		      {
			Tcl_AppendResult(interp, "{", 0);
			Tcl_AppendElement(interp, "Location");
#if TCL_MAJOR_VERSION > 8 || ( TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION >= 1 )
			{
			  const char *ostring;
			  Tcl_DString tds;
			  Tcl_DStringInit(&tds);
			  Tcl_UtfToExternalDString(NULL, ary[i].pLocation, -1, &tds);
			  ostring = Tcl_DStringValue(&tds);
			  Tcl_AppendElement(interp, ostring);
			  Tcl_DStringFree(&tds);
			}
#else
			Tcl_AppendElement(interp, ary[i].pLocation);
#endif
			Tcl_AppendResult(interp, "} ", 0);
		      }
		    Tcl_AppendResult(interp, "{", 0);
		    Tcl_AppendElement(interp, "Queued Jobs");
		    sprintf(msgbuf, "%ld", (long)ary[i].cJobs);
		    Tcl_AppendElement(interp, msgbuf );
		    Tcl_AppendResult(interp, "} ", 0);
		    /* End of this printer's list.  */
		    Tcl_AppendResult(interp, "}\n", 0);
		  }
		else              
		  Tcl_AppendElement(interp, ary[i].pPrinterName);
	      }
	  }
	Tcl_Free((char *)ary);
      }
      break;
    }
  return TCL_OK;
}

#define PRINT_FROM_FILE 0
#define PRINT_FROM_DATA 1

/*
 *----------------------------------------------------------------------
 *
 * PrintSend --
 *
 *  Main routine for sending data or files to a printer.
 *
 * Results:
 *	Sends data to printer.
 *
 *----------------------------------------------------------------------
 */

static int PrintSend (ClientData defaults, Tcl_Interp *interp, int argc, const char  * argv)
{
  static char *usgmsg = 
    "::tk::print::_print send "
    "[-postscript|-nopostscript] "
    "[-binary|-ascii] "
    "[-printer printer] "
    "[-datalen nnnnnn] "
    "[-file|-data] file_or_data ... ";
  int ps = 0;      /* The default is nopostscript.  */
  int binary = 1;  /* The default is binary.  */
  long datalen = 0L;
    
  const char *printer = 0;
  const char *hdcString = 0;
  static char last_printer[255+1];
  int debug = 0;
  int printtype = PRINT_FROM_FILE;
  struct printer_values  * ppv = *(struct printer_values  * ) defaults;
  struct printer_values  * oldppv = 0;
  int self_created = 0;  /* Remember if we specially created the DC.  */
  int direct_to_port = 0;
  HANDLE hdc = NULL;
    
  while ( argc > 0 )
    {
      if (argv[0][0] == '-')
        {
	  /* Check for -postscript / -nopostscript flag.  */
	  if (strcmp(argv[0], "-postscript") == 0)
	    ps = 1;
	  else if (strcmp(argv[0], "-nopostscript") == 0)
	    ps = 0;
	  else if (strcmp(argv[0], "-ascii") == 0)
	    binary = 0;
	  else if (strcmp(argv[0], "-binary") == 0)
	    binary = 1;
	  else if ( strcmp(argv[0], "-printer") == 0)
            {
	      argc--;
	      argv++;
	      printer = argv[0];
            }
	  else if ( strcmp(argv[0], "-file") == 0)
	    printtype = PRINT_FROM_FILE;
	  else if ( strcmp(argv[0], "-data") == 0) {
	    printtype = PRINT_FROM_DATA;
	  }
	  else if ( strcmp(argv[0], "-datalen") == 0 ) 
            {
	      argc--;
	      argv++;
	      datalen = atol(argv[0]);
            }
	  else if ( strcmp(argv[0], "-debug") == 0)
	    debug++;
	  else if ( strcmp(argv[0], "-direct") == 0 )
	    direct_to_port = 1;
        }
      else
	break;
      argc--;
      argv++;
    }
    
  if (argc <= 0)
    {
      Tcl_SetResult(interp,usgmsg, TCL_STATIC);
      return TCL_ERROR;
    }
    
    
  /*  
   * Ensure we have a good HDC. If not, we'll have to abort.
   * First, go by printer name, if provided.
   * Next, use the last printer we opened, if any
   * Finally, use the default printer.
   * If we still don't have a good HDC, we've failed.
   *
   */ 
  if ( hdc == NULL  )
    {
      if ( printer )
	OpenPrinter((char *)printer, &hdc, NULL);
      else if ( last_printer[0] != '\0' )
	OpenPrinter(last_printer, &hdc, NULL);
      else if ( current_printer_values != 0 && current_printer_values->devnames_printername[0] != '\0')
	OpenPrinter(current_printer_values->devnames_printername, &hdc, NULL);
      else 
        {
        }
        
      if ( hdc == NULL )  /* STILL can't get a good printer DC.  */
        {
	  Tcl_SetResult (interp, "Error: Can't get a valid printer context", TCL_STATIC);
	  return TCL_ERROR;
        }
    }
    
  /* Now save off a bit of information for the next call....  */
  if (printer)
    strncpy ( last_printer, printer, sizeof(last_printer) - 1);
  else if ( ppv && ppv->devnames_printername[0] )
    strncpy ( last_printer, ppv->devnames_printername, sizeof(last_printer) - 1 );
    
  /* *
   * Everything left is a file or data. Just print it.
   *  */
  while (argc > 0)
    {
      static const char init_postscript[] = "\r\nsave\r\ninitmatrix\r\n";
      static const char fini_postscript[] = "\r\nrestore\r\n";
        
      const char *docname;
        
      if ( argv[0][0] == '-') {
	if ( strcmp(argv[0], "-datalen") == 0 ) 
	  {
	    argc--;
	    argv++;
	    datalen = atol(argv[0]);
	    continue;
	  }            
	else if ( strcmp(argv[0], "-file") == 0) {
	  argc--;
	  argv++;
	  printtype = PRINT_FROM_FILE;
	  continue;
	}
	else if ( strcmp(argv[0], "-data") == 0) {
	  argc--;
	  argv++;
	  printtype = PRINT_FROM_DATA;
	  continue;
	}
      }
        
      switch (printtype) {
      case PRINT_FROM_FILE:
	docname = argv[0];
	break;
      case PRINT_FROM_DATA:
      default:
	docname = "Tcl Print Data";
	if (datalen == 0L ) {
	  Tcl_AppendResult(interp, "Printer warning: ::tk::print::_print send ... -data requires a -datalen preceding argument. Using strlen as a poor substitute.\n", 0);
	  datalen = strlen(argv[0]);
	}
	break;
      }
        
      if ( PrintStart(hdc, interp, docname) == 1 ) {
	if (ps) {
	  DWORD inCount = strlen(init_postscript);
	  DWORD outCount = 0;
	  if ( WritePrinter(hdc,(LPVOID)init_postscript,inCount,&outCount) == 0 ||
	       inCount != outCount ) {
	    Tcl_AppendResult(interp,"Printer error: Postscript init failed\n", 0);
	  }
	}
            
	switch (printtype) {
	case PRINT_FROM_FILE:
	  if ( PrintRawFileData(hdc,interp,argv[0],binary) == 0 ) {
	    Tcl_AppendResult(interp,"Printer error: Could not print file ", argv[0], "\n", 0);
	  }
	  break;
	case PRINT_FROM_DATA:
	default:
	  if ( PrintRawData(hdc,interp,(LPBYTE)argv[0],datalen) == 0 ) {
	    Tcl_AppendResult(interp,"Printer error: Could not print raw data\n", 0);
	  }
	  datalen=0L;  /* reset the data length, so it is not reused.  */
	  break;
	}
            
	if (ps) {
	  DWORD inCount = strlen(fini_postscript);
	  DWORD outCount = 0;
	  if ( WritePrinter(hdc,(LPVOID)fini_postscript,inCount,&outCount) == 0 ||
	       inCount != outCount ) {
	    Tcl_AppendResult(interp,"Printer error: Postscript finish failed\n", 0);
	  }
	}
            
	PrintFinish(hdc, interp);
      }
      argv++;
      argc--;
    }
    
  ClosePrinter(hdc);
    
  return TCL_OK;
}

/*
 *  Support for file printing
 */

/*
 *----------------------------------------------------------------------
 *
 * PrintRawData --
 *
 *  Prints raw data to a printer.
 *
 * Results:
 *	Sends data to printer.
 *
 *----------------------------------------------------------------------
 */
 
static int PrintRawData (HANDLE printer, Tcl_Interp *interp, LPBYTE lpData, DWORD dwCount)
{
  int retval = 0;
  DWORD dwBytesWritten = 0;
    
  /* Send the data.  */
  if ( WritePrinter( printer, lpData, dwCount, &dwBytesWritten) == 0 ) {
    /* Error writing the data.  */
    Tcl_AppendResult(interp, "Printer error: Cannot write data to printer");
  } else if ( dwBytesWritten != dwCount ) {    
    /* Wrong number of bytes were written....  */
    sprintf(msgbuf, "%ld written; %ld requested", dwBytesWritten, dwCount);
    Tcl_AppendResult(interp, "Printer error: Wrong number of bytes were written", 
		     msgbuf, "\n", 0);
  } else
    retval = 1;
    
  return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintRawFileData --
 *
 *  Prints raw file data to a printer.
 *
 * Results:
 *	Sends file data to printer.
 *
 *----------------------------------------------------------------------
 */

static int PrintRawFileData (HANDLE printer, Tcl_Interp *interp, const char *filename, int binary)
{
  int retval = 0;
  DWORD dwBytesWritten = 0;
  DWORD dwBytesRequested = 0;
    
  Tcl_Channel channel;
    
  struct {
    WORD len;  /* Defined to be 16 bits.....  */
    char buffer[128+1];
  } indata;
    
  if ( (channel = Tcl_OpenFileChannel(interp, (char *)filename, "r", 0444)) == NULL)
    {
      /* Can't open the file!.  */
      return 0;
    }
    
  if ( binary )
    Tcl_SetChannelOption(interp, channel, "-translation", "binary");
    
  /* Send the data.  */
  while ( (indata.len = Tcl_Read(channel, indata.buffer, sizeof(indata.buffer)-1)) > 0)
    {
      DWORD dwWritten = 0;
      dwBytesRequested += indata.len;
      indata.buffer[indata.len] = '\0';
      if ( WritePrinter( printer, indata.buffer, indata.len, &dwWritten) == 0 )
        {
	  /* Error writing the data.  */
	  Tcl_AppendResult(interp, "Printer error: Can't write data to printer\n", 0);
	  Tcl_Close(interp, channel);
	  break;
        }
      dwBytesWritten += dwWritten;
      if ( dwWritten != indata.len ) {
	sprintf(msgbuf, "%ld requested; %ld written", (long)indata.len, dwWritten);
	Tcl_AppendResult(interp, "Printer warning: Short write: ", msgbuf, "\n", 0);
      }
    }

  if ( dwBytesWritten == dwBytesRequested )
    retval = 1;
    
  Tcl_Close(interp, channel);
    
  return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintStart --
 *
 *  Sets up the job and starts the DocPrinter and PagePrinter.
 *
 * Results:
 *	Returns 1 upon success, and 0 if anything goes wrong.
 *
 *----------------------------------------------------------------------
 */


static int PrintStart (HDC printer, Tcl_Interp *interp, const char *docname)
{
  DOC_INFO_1 DocInfo;
  DWORD dwJob;
    
  /* Fill in the document information with the details.  */
  if ( docname != 0 ) 
    DocInfo.pDocName = (LPTSTR)docname;
  else
    DocInfo.pDocName = (LPTSTR)"Tcl Document";
  DocInfo.pOutputFile = 0;
  DocInfo.pDatatype = "RAW";
    
  /* Start the job.  */
  if ( (dwJob = StartDocPrinter(printer, 1, (LPSTR)&DocInfo)) == 0 ) {
    /* Error starting doc printer.  */
    Tcl_AppendResult(interp, "Printer error: Cannot start document printing\n", 0);
    return 0;
  }
  /* Start the first page.  */
  if ( StartPagePrinter(printer) == 0 ) {
    /* Error starting the page.  */
    Tcl_AppendResult(interp, "Printer error: Cannot start document page\n", 0);
    EndDocPrinter(printer);
    return 0;
  }
  return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintFinish --
 *
 *  Finishes the print job.
 *
 * Results:
 *	Print job ends.
 *
 *----------------------------------------------------------------------
 */

static int PrintFinish (HDC printer, Tcl_Interp *interp)
{
  /* Finish the last page.  */
  if ( EndPagePrinter(printer) == 0 ) {
    Tcl_AppendResult(interp, "Printer warning: Cannot end document page\n", 0);
    /* Error ending the last page.  */
  }
  /* Conclude the document.  */
  if ( EndDocPrinter(printer) == 0 ) {
    Tcl_AppendResult(interp, "Printer warning: Cannot end document printing\n", 0);
    /* Error ending document.  */
  }
    
  JobInfo(0,0,0);
    
  return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintOpenDefault  --
 *
 *  Opens the default printer.
 *
 * Results:
 *	Default printer opened. 
 *
 *----------------------------------------------------------------------
 */

static int PrintOpenDefault (ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  struct printer_values *ppv = *(struct printer_values  * )data;
  if ( autoclose && ppv && ppv->hDC)
    {
      char tmpbuf[11+1+1];
      char *args[3];
      sprintf(tmpbuf, "0x%lx", ppv->hDC);
      args[0] = "-hDC";
      args[1] = tmpbuf;
      args[2] = 0;
      PrintClose(data, interp, 2, args);
    }
  *(struct printer_values  * )data = ppv
    = make_printer_values(0);  /* Get a default printer_values context.  */
    
  /* This version uses PrintDlg, and works under Win32s.  */
  {
    HWND tophwnd;
    int retval;
        
    /* The following is an attempt to get the right owners notified of
     *  repaint requests from the dialog. It doesn't quite work.
     *  It does make the dialog box modal to the toplevel it's working with, though.
     .  */
    if ( (ppv->pdlg.hwndOwner = GetActiveWindow()) != 0 )
      while ( (tophwnd = GetParent(ppv->pdlg.hwndOwner) ) != 0 )
	ppv->pdlg.hwndOwner = tophwnd;
        
    /*
     *  Since we are doing the "default" dialog, we must put NULL in the
     *  hDevNames and hDevMode members.
     *  Use '::tk::printer::_print  dialog select' for selecting a printer from a list
     .  */
    ppv->pdlg.lStructSize = sizeof( PRINTDLG );
    ppv->pdlg.Flags = PD_RETURNDEFAULT | PD_RETURNDC;
    ppv->pdlg.hDevNames = 0;
    ppv->pdlg.hDevMode  = 0;
        
    retval = PrintDlg ( &(ppv->pdlg) );
        
    if ( retval == 1 )
      {
	const char *name;
	if ( ppv->hdcname[0] && hdc_delete )
	  hdc_delete(interp, ppv->hdcname);
	ppv->hdcname[0] = '\0';
	/* StorePrintVals creates and stores the hdcname as well.  */
	StorePrintVals(ppv, &ppv->pdlg, 0);
	if  ( (name = get_attribute (&ppv->attribs, "device")) != 0 )
	  if ( PrinterGetDefaults(ppv, name, 1) > 0 ) {  /* Set default DEVMODE too.  */
	    current_printer_values = ppv;  /* This is now the default printer.  */
	  }
      }
    else
      {
	/* Failed or cancelled. Leave everything else the same.  */
	Tcl_Free( (char *) ppv);
	/* Per Steve Bold--restore the default printer values
	   In any case the current_printer_values shouldn't be left hanging
	   .  */
	*(struct printer_values  * )data = &default_printer_values;
      }
  }
    
  /* The status does not need to be supplied. either hDC is OK or it's NULL.  */
  if ( ppv->hdcname[0] )
    Tcl_SetResult(interp, ppv->hdcname, TCL_VOLATILE);
  else
    {
      sprintf(msgbuf, "0x%lx", ppv->hDC);
      Tcl_SetResult(interp, msgbuf, TCL_VOLATILE);
    }
    
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * PrintOpen  --
 *
 *  Open any named printer (or the default printer if no name
 *  is provided).
 *
 * Results:
 *	Printer opened.
 *
 *----------------------------------------------------------------------
 */

static int PrintOpen(ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  /* The ClientData is the default printer--this may be overridden by the proc arguments.  */
  struct printer_values *ppv = *(struct printer_values  * )data;
  const char *printer_name;
  int        use_printer_name = 0;
  int        use_default = 0;
  int        use_attrs   = 0;
  const char  *     attrs = 0;
  int        j;
  int        retval = TCL_OK;
  static const char usage_message[] = "::tk::print::_print open [-name printername|-default]";
    
  /* Command line should specify everything needed. Don't bring up dialog.  */
  /* This should also SET the default to any overridden printer name.  */
  for (j=0; j<argc; j++)
    {
      if ( strcmp (argv[j], "-name") == 0 )
        {
	  use_printer_name = 1;
	  printer_name = argv[++j];
        }
      else if ( strcmp (argv[j], "-default") == 0 )
	use_default = 1;
      /* Need a case here for attributes, so one can specify EVERYTHING on the command.  */
      else if ( strncmp (argv[j], "-attr", 5) == 0 )
        {
	  use_attrs = 1;
	  attrs = argv[++j];
        }
    }
    
  switch ( use_printer_name + use_default )
    {
    case 0:
      use_default = 1;
      break;
    case 2:
      Tcl_AppendResult(interp, "::tk::print::_print open: Can't specify both printer name and default\n", usage_message, 0);
      return TCL_ERROR;
    }
    
  if ( use_printer_name )
    {
      if (ppv && ppv->hDC)
        {
	  char tmpbuf[11+1+1];
	  char *args[3];
	  sprintf(tmpbuf, "0x%lx", ppv->hDC);
	  args[0] = "-hDC";
	  args[1] = tmpbuf;
	  args[2] = 0;
	  PrintClose(data, interp, 2, args);
        }
        
      ppv = make_printer_values(0);  /* Get a default printer_values context.  */
      *(struct printer_values  * )data = ppv;
      /*
       *  Since this is a print open, a new HDC will be created--at this point, starting
       *  with the default attributes.
       */
      if (ppv) {
	int retval = 0;
            
	if ( (retval = PrinterGetDefaults(ppv, printer_name, 1)) > 0 )     /* Set devmode if available.  */
	  {
	    const char *cp;
	    if ( (cp = make_printer_dc_name(interp, ppv->hDC, ppv) ) != 0 )
	      {
		strncpy(ppv->hdcname, cp, sizeof (current_printer_values->hdcname));
		set_attribute(&ppv->attribs, "hdcname", cp);
	      }
	    current_printer_values = ppv;  /* This is now the default printer.  */
	  } else {
	  /* an error occurred - printer is not usable for some reason, so report that.  */
	  switch ( retval ) {
	  case GETDEFAULTS_UNSUPPORTED:  /* Not supported.  */
	    Tcl_AppendResult(interp, "PrinterGetDefaults: Not supported for this OS\n", 0);
	    break;
	  case GETDEFAULTS_NOSUCHPRINTER:  /* Can't find printer.  */
	    Tcl_AppendResult(interp, "PrinterGetDefaults: Can't find printer ", printer_name, "\n", 0);
	    break;
	  case GETDEFAULTS_CANTCREATEDC:  /* Can't create DC.  */
	    Tcl_AppendResult(interp, "PrinterGetDefaults: Can't create DC: Insufficient printer information\n", 0);
	    break;
	  case GETDEFAULTS_CANTOPENPRINTER:  /* Can't open printer.  */
	    Tcl_AppendResult(interp, "PrinterGetDefaults: Can't open printer ", printer_name, "\n", 0);
	    break;
	  case GETDEFAULTS_WINDOWSERROR:  /* Windows error.  */
	    Tcl_AppendResult(interp, "PrinterGetDefaults: Windows error\n", 0);
	    break;
	  default:  /* ???.  */
	    Tcl_AppendResult(interp, "PrinterGetDefaults: Unknown error\n", 0);
	    break;
	  }
                
	  if (ppv->errorCode != 0 )
	    ReportWindowsError(interp,ppv->errorCode);
                
	  /* release the ppv.  */
	  delete_printer_values(ppv);
                
	  return TCL_ERROR;
	}
      }
    }
  else  /* It's a default.  */
    {
      retval = PrintOpenDefault(data, interp, argc, argv);    /* argc, argv unused.  */
      ppv = *(struct printer_values  * )data;
    }
    
  /* Get device names information.  */
  {
    char *dev, *dvr, *port;
    /* 
     * retval test added by Jon Hilbert, <jhilbert@hilbertsoft.com> 8/8/02. 
     * The printer name in this function should not be matched with wildcards.  
     */
    if ( retval == TCL_OK && ppv && ppv->pdevmode && ppv->pdevmode->dmDeviceName &&
	 GetPrinterWithName((char *)(ppv->pdevmode->dmDeviceName), &dev, &dvr, &port, 0) != 0 )
      {
	strcpy(ppv->devnames_filename, dvr );
	strcpy(ppv->devnames_port,    port );
      }
  }
    
  /* Check for attribute modifications.  */
  if ( use_attrs != 0 && retval == TCL_OK )
    {
      char hdcbuffer[20];
      const char *args[5];
#if TCL_MAJOR_VERSION > 8 || (TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION >= 1)
      Tcl_SavedResult state;
      Tcl_SaveResult(interp, &state);
#endif
      args[0] = "-hDC";
      sprintf(hdcbuffer, "0x%lx", ppv->hDC);
      args[1] = hdcbuffer;
      args[2] = "-set";
      args[3] = attrs;
      args[4] = 0;
      PrintAttr(data, interp, 4, args);
#if TCL_MAJOR_VERSION > 8 || (TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION >= 1)
      Tcl_RestoreResult(interp,&state);
#endif
    }
    
  /* The status does not need to be supplied. either hDC is OK or it's NULL.  */
  if ( ppv->hdcname[0] )
    Tcl_SetResult(interp, ppv->hdcname, TCL_VOLATILE);
  else
    {
      sprintf(msgbuf, "0x%lx", ppv->hDC);
      Tcl_SetResult(interp, msgbuf, TCL_VOLATILE);
    }
    
  return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintClose  --
 *
 *  Frees the printer DC and releases it.
 *
 * Results:
 *	Printer closed.
 *
 *----------------------------------------------------------------------
 */

static int PrintClose(ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  int j;
  const char *hdcString = 0;
    
  /* Start with the default printer.  */
  struct printer_values *ppv = *(struct printer_values  * )data;
    
  /* See if there are any command line arguments.  */
  for (j=0; j<argc; j++)
    {
      if ( strcmp (argv[j], "-hDC") == 0 || strcmp (argv[j], "-hdc") == 0 )
        {
	  hdcString = argv[++j];
        }
    }
    
  if ( hdcString)
    {
      HDC hdc = get_printer_dc(interp, hdcString);
      ppv = find_dc_by_hdc(hdc);
      *(struct printer_values  * )data = ppv;
        
      if ( ppv == current_printer_values )
        {
	  current_printer_values = &default_printer_values;  /* This is the easiest....  */
        }
    }
    
  if ( ppv == 0 )  /* Already closed?.  */
    return TCL_OK;
    
  /* Check the status of the job and page.  */
    
  PrintFinish(ppv->hDC, interp);
  ppv->in_page = 0;
  ppv->in_job  = 0;
    
  /* Free the printer DC.  */
  if (ppv->hDC)
    {
      delete_dc(ppv->hDC);
      DeleteDC(ppv->hDC);
      ppv->hDC = NULL;
    }
    
  if ( ppv->hdcname[0] != '\0' && hdc_delete != 0 )
    hdc_delete(interp, ppv->hdcname);
  ppv->hdcname[0] = '\0';
    
  /* We should also clean up the devmode and devname structures.  */
  if ( ppv && ppv != current_printer_values )
    delete_printer_values(ppv);
    
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintDialog--
 *
 *  Main dialog for selecting printer and page setup.
 *
 * Results:
 *	Printer or page setup selected.
 *
 *----------------------------------------------------------------------
 */


static int PrintDialog(ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  /* Which dialog is requested: one of select, page_setup.  */
  static char usage_message[] = "::tk::print::_print dialog [-hDC hdc ] [select|page_setup] [-flags flagsnum]";
  struct printer_values *ppv = *(struct printer_values  * )data;
  int flags;
  int oldMode;
  int print_retcode;
  HDC hdc = 0;
  const char *hdcString = 0;
    
  int is_new_ppv = 0;
  struct printer_values *old_ppv = ppv;
    
  static const int PRINT_ALLOWED_SET = PD_ALLPAGES|PD_SELECTION|PD_PAGENUMS|
    PD_NOSELECTION|PD_NOPAGENUMS|PD_COLLATE|
    PD_PRINTTOFILE|PD_PRINTSETUP|PD_NOWARNING|
    PD_RETURNDC|PD_RETURNDEFAULT|
    PD_DISABLEPRINTTOFILE|PD_HIDEPRINTTOFILE|
    PD_NONETWORKBUTTON;
  static const int PRINT_REQUIRED_SET = PD_NOWARNING|PD_RETURNDC;
    
  static const int PAGE_ALLOWED_SET =
    PSD_MINMARGINS|PSD_MARGINS|PSD_NOWARNING|
    PSD_DEFAULTMINMARGINS|PSD_DISABLEMARGINS|
    PSD_DISABLEORIENTATION|PSD_DISABLEPAGEPAINTING|
    PSD_DISABLEPAPER|PSD_DISABLEPRINTER|
    PSD_INHUNDREDTHSOFMILLIMETERS|PSD_INTHOUSANDTHSOFINCHES|
    PSD_RETURNDEFAULT;
  static const int PAGE_REQUIRED_SET =
    PSD_NOWARNING | PSD_DISABLEPRINTER;
    
  /* Create matching devmode and devnames to match the defaults.  */
  HANDLE  hDevMode = 0;
  HANDLE  hDevNames = 0;
  DEVMODE *pdm = 0;
  DEVNAMES *pdn = 0;
  int     dmsize = 0;
    
  int errors = 0;
  const int alloc_devmode = 1;
  const int lock_devmode  = 2;
  const int alloc_devname = 4;
  const int lock_devname  = 8;
  const int change_devmode = 16;
  int k;
  int do_select= 0;
  int do_page  = 0;
  int do_flags = 0;
  int do_sync  = 0;
    
  if (argc < 1)
    {
      Tcl_SetResult(interp, usage_message, TCL_STATIC);
      return TCL_ERROR;
    }
    
  for (k = 0; k < argc; k++ )
    {
      if ( strcmp(argv[k], "select") == 0 )
	do_select = 1;
      else if ( strcmp(argv[k], "page_setup") == 0 )
	do_page   = 1;
      else if ( strcmp(argv[k], "-hdc") == 0  || strcmp (argv[k], "-hDC") == 0 )
        {
	  k++;
	  hdcString = argv[k];
        }
      else if ( strcmp(argv[k], "-flags") == 0 )
        {
	  char *endstr;
	  if (argv[k+1])
            {
	      flags = strtol(argv[++k], &endstr, 0);  /* Take any valid base.  */
	      if (endstr != argv[k])  /* if this was a valid numeric string.  */
		do_flags = 1;
            }
        }
    }
    
  if ( (do_page + do_select) != 1 )
    {
      Tcl_SetResult(interp, usage_message, TCL_STATIC);
      return TCL_ERROR;
    }
    
  if ( ppv == 0 || ppv == &default_printer_values || ppv->hDC == 0 )
    {
      is_new_ppv = 1;
      old_ppv = 0;
    }
    
  if ( hdcString )
    {
      hdc = get_printer_dc(interp,hdcString);
      ppv = find_dc_by_hdc(hdc);
      *(struct printer_values  * )data = ppv;
      if (hdc == 0 )
        {
	  is_new_ppv = 1;
        }
      if (ppv == 0 )
        {
	  is_new_ppv = 1;
        }
    }
    
  if ( is_new_ppv == 1 )
    {
      /* Open a brand new printer values structure.  */
      old_ppv = ppv;
      ppv = make_printer_values(0);
      *(struct printer_values  * )data = ppv;
    }
    
  /* Copy the devmode and devnames into usable components.  */
  if (ppv && ppv->pdevmode)
    dmsize = ppv->pdevmode->dmSize+ppv->pdevmode->dmDriverExtra;
    
  if ( dmsize <= 0 )
    ;  /* Don't allocate a devmode structure.  */
  else if ( (hDevMode = GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT, dmsize) ) == NULL )
    {
      /* Failure!.  */
      errors |= alloc_devmode;
      pdm = 0;   /* Use the default devmode.  */
    }
  else if ( (pdm = (DEVMODE *)GlobalLock(hDevMode)) == NULL )
    {
      /* Failure!.  */
      errors |= lock_devmode;
    }
    
  /* If this is the first time we've got a ppv, just leave the names null.  */
  if ( ppv->devnames_filename[0] == 0 ||
       ppv->devnames_port[0] == 0 ||
       ppv->pdevmode == 0 )
    ;  /* Don't allocate the devnames structure.  */
  else if ( (hDevNames = GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,
				     sizeof(DEVNAMES)+
				     sizeof(ppv->devnames_filename)   + 
				     CCHDEVICENAME +
				     sizeof(ppv->devnames_port)       + 2 )
	     ) == NULL)
    {
      /* Failure!.  */
      errors |= alloc_devname;
      pdn = 0;
    }
  else if ( (pdn = (DEVNAMES *)GlobalLock(hDevNames)) == NULL)
    {
      /* Failure!.  */
      errors |= lock_devname;
    }
    
  if (pdm)
    memcpy (pdm, ppv->pdevmode, dmsize);
    
  if (pdn)
    {
      pdn->wDefault = 0;
      pdn->wDriverOffset = 4*sizeof (WORD);
      strcpy( (char *)pdn + pdn->wDriverOffset, ppv->devnames_filename);
      pdn->wDeviceOffset = pdn->wDriverOffset + strlen(ppv->devnames_filename) + 2;
      strcpy ( (char *)pdn + pdn->wDeviceOffset, ppv->pdevmode->dmDeviceName);
      pdn->wOutputOffset = pdn->wDeviceOffset + strlen(ppv->pdevmode->dmDeviceName) + 2;
      strcpy ( (char *)pdn + pdn->wOutputOffset, ppv->devnames_port);
    }
    
  if (hDevMode) 
    GlobalUnlock(hDevMode);
  if (hDevNames)
    GlobalUnlock(hDevNames);
    
  if ( do_select )
    {
      /*
       *  Looking at the return value of PrintDlg, we want to
       *  save the values in the PAGEDIALOG for the next time.
       *  The tricky part is that PrintDlg and PageSetupDlg
       *  have the ability to move their hDevMode and hDevNames memory. 
       *  This never seems to happen under NT, 
       *  seems not to happen under Windows 3.1,
       *  but can be demonstrated under Windows 95 (and presumably Windows 98).
       * 
       *  As the handles are shared among the Print and Page dialogs, we must
       *  consistently establish and free the handles.
       *  Current thinking is to preserve them in the PageSetup structure ONLY,
       *  thus avoiding the problem here.
       .  */
        
      HWND    tophwnd;
        
      /* Assign the copied, moveable handles to the dialog structure.  */
      ppv->pdlg.hDevMode = hDevMode;
      ppv->pdlg.hDevNames = hDevNames;
        
      /* 
       *  This loop make the dialog box modal to the toplevel it's working with.
       *  It also avoids any reliance on Tk code (for Tcl users).
       .  */
      if ( (ppv->pdlg.hwndOwner = GetActiveWindow()) != 0 )
	while ( (tophwnd = GetParent(ppv->pdlg.hwndOwner) ) != 0 )
	  ppv->pdlg.hwndOwner = tophwnd;
        
      /* Leaving the memory alone will preserve selections.  */
      /* memset (&(ppv->pdlg), 0, sizeof(PRINTDLG) );.  */
      ppv->pdlg.lStructSize = sizeof(PRINTDLG);
      ppv->pdlg.Flags |= PRINT_REQUIRED_SET; 
        
      /* Vista (Win95) Fix Start.  */
      /* Seems to be needed to print multiple copies.  */
      ppv->pdlg.Flags |= PD_USEDEVMODECOPIES; 
      ppv->pdlg.nCopies = (WORD)PD_USEDEVMODECOPIES;  /* Value shouldn't matter.  */
      /* Vista Fix End.  */
        
      if ( do_flags )
        {
	  /* Enable requested flags, but disable the flags we don't want to support.  */
	  ppv->pdlg.Flags |= flags;
	  ppv->pdlg.Flags &= PRINT_ALLOWED_SET;
        }
        
      /* One may not specify return default when devmode or devnames are present.  */
      /* Since the copied flags in the ppv's pdevmode may have been created by
       *  the "PrintOpen" call, this flag _might_ be set
       .  */
      if (ppv->pdlg.hDevMode || ppv->pdlg.hDevNames)
	ppv->pdlg.Flags &= (~PD_RETURNDEFAULT);
        
#if TCL_MAJOR_VERSION > 7
      /* In Tcl versions 8 and later, a service call to the notifier is provided.  */
      oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
#endif
        
      print_retcode = PrintDlg(&(ppv->pdlg));
        
#if TCL_MAJOR_VERSION > 7
      /* Return the service mode to its original state.  */
      Tcl_SetServiceMode(oldMode);
#endif
        
      if ( print_retcode == 1 )  /* Not canceled.  */
        {
	  const char *name;
	  StorePrintVals (ppv, &ppv->pdlg, 0);
            
	  if  ( (name = get_attribute (&ppv->attribs, "device")) != 0 )
	    PrinterGetDefaults(ppv, name, 0);  /* Don't set default DEVMODE: 
						  user may have already set it in properties.  */
            
	  add_dc(ppv->hDC, ppv);
	  current_printer_values = ppv;
            
	  hDevNames = NULL;
	  hDevMode = NULL;
        }
      else  /* Canceled.  */
        {
	  DWORD extError = CommDlgExtendedError();
	  if (ppv->pdlg.hDevMode)
	    GlobalFree(ppv->pdlg.hDevMode);
	  else
	    GlobalFree(hDevMode);
	  hDevMode = ppv->pdlg.hDevMode = NULL;
            
	  if ( ppv->pdlg.hDevNames )
	    GlobalFree (ppv->pdlg.hDevNames);
	  else
	    GlobalFree (hDevNames);
	  hDevNames = ppv->pdlg.hDevNames = NULL;
            
	  if (is_new_ppv)
            {
	      Tcl_Free((char *)ppv);
	      ppv = old_ppv;
	      if ( ppv == 0 )
		ppv = &default_printer_values;
	      *(struct printer_values  * )data = ppv;
            }
        }
        
      /* Results are available through printer attr; HDC now returned.  */
      /* This would be a good place for Tcl_SetObject, but for now, support
       *  older implementations by returning a Hex-encoded value.
       *  Note: Added a 2nd parameter to allow caller to note cancellation.
       */
      {
	const char *cp = ppv->hdcname;
	if (cp && cp[0])
	  sprintf(msgbuf, "%s %d", cp, print_retcode );
	else
	  sprintf(msgbuf, "0x%lx %d", ppv->hDC, print_retcode);
	Tcl_SetResult(interp, msgbuf, TCL_VOLATILE);
      }
    }
  else if (do_page)
    {
      if ( do_flags == 0 )
	flags = PSD_MARGINS|PSD_NOWARNING|PSD_DISABLEPRINTER|PSD_INTHOUSANDTHSOFINCHES;
        
      ppv->pgdlg.Flags = flags;
      /* Restrict flags to those we wish to support.  */
      ppv->pgdlg.Flags |= PAGE_REQUIRED_SET;
      ppv->pgdlg.Flags &= PAGE_ALLOWED_SET;
        
      /* Set the devmode and devnames to match our structures.  */
      ppv->pgdlg.hDevMode = hDevMode;
      ppv->pgdlg.hDevNames = hDevNames;
        
      ppv->pgdlg.lStructSize = sizeof(PAGESETUPDLG);
#if TCL_MAJOR_VERSION > 7
      /* In Tcl versions 8 and later, a service call to the notifier is provided.  */
      oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
#endif
        
      print_retcode = PageSetupDlg(&(ppv->pgdlg));
        
#if TCL_MAJOR_VERSION > 7
      /* Return the service mode to its original state.  */
      Tcl_SetServiceMode(oldMode);
#endif
        
      if ( print_retcode == 1 )  /* Not cancelled.  */
        {      
	  StorePrintVals(ppv, 0, &ppv->pgdlg);
	  /* Modify the HDC using ResetDC.  */
	  ResetDC(ppv->hDC, ppv->pdevmode);      
	  hDevNames = NULL;
	  hDevMode  = NULL;
        }
      else  /* Canceled.  */
        {
	  if (ppv->pgdlg.hDevMode)
	    GlobalFree(ppv->pgdlg.hDevMode);
	  else
	    GlobalFree(hDevMode);
	  hDevMode = ppv->pgdlg.hDevMode = NULL;
            
	  if ( ppv->pgdlg.hDevNames )
	    GlobalFree (ppv->pgdlg.hDevNames);
	  else
	    GlobalFree (hDevNames);
	  hDevNames = ppv->pgdlg.hDevNames = NULL;
	  if ( is_new_ppv )
            {
	      Tcl_Free ((char *)ppv);
	      ppv = old_ppv;
	      if (ppv == 0 )
		ppv = &default_printer_values;
	      *(struct printer_values  * )data = ppv;
            }
        }
        
      {
	const char *cp = ppv->hdcname;
	if (cp && cp[0])
	  sprintf(msgbuf, "%s %d", cp, print_retcode );
	else
	  sprintf(msgbuf, "0x%lx %d", ppv->hDC, print_retcode);
	Tcl_SetResult(interp, msgbuf, TCL_VOLATILE);
      }
      Tcl_SetResult(interp, msgbuf, TCL_VOLATILE);
    }
  else
    {
      Tcl_SetResult(interp, usage_message, TCL_STATIC);
      return TCL_ERROR;
    }
    
  if (errors)
    {
      if (errors & alloc_devmode)
	Tcl_AppendResult(interp, "\nError allocating global DEVMODE structure", 0);
      if (errors & lock_devmode)
	Tcl_AppendResult(interp, "\nError locking global DEVMODE structure", 0);
      if (errors & alloc_devname)
	Tcl_AppendResult(interp, "\nError allocating global DEVNAMES structure", 0);
      if (errors & lock_devname)
	Tcl_AppendResult(interp, "\nError locking global DEVNAMES structure", 0);
    }
    
  return TCL_OK;
}

static int JobInfo(int state, const char *name, const char  * outname)
{
  static int inJob = 0;
  static char jobname[63+1];
    
  switch (state)
    {
    case 0:
      inJob = 0;
      jobname[0] = '\0';
      break;
    case 1:
      inJob = 1;
      if ( name )
	strncpy (jobname, name, sizeof(jobname) - 1 );
      break;
    default:
      break;
    }
  if ( outname )
    *outname = jobname;
  return inJob;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintJob--
 *
 *  Manage print jobs.
 *
 * Results:
 *	Print job executed.
 *
 *----------------------------------------------------------------------
 */


static int PrintJob(ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  DOCINFO di;
  struct printer_values  * ppv = *(struct printer_values  * ) data;
    
  static char usage_message[] = "::tk::print::_print job [ -hDC hdc ] [ [start [-name docname] ] | end ]";
  HDC hdc = 0;
  const char *hdcString = 0;
    
  /* Parameters for document name and output file (if any) should be supported.  */
  if ( argc > 0 && (strcmp(argv[0], "-hdc") == 0  || strcmp (argv[0], "-hDC") == 0) )
    {
      argc--;
      argv++;
      hdcString = argv[0];
      argc--;
      argv++;
    }
    
  if ( hdcString )
    {
      hdc = get_printer_dc(interp,hdcString);
      ppv = find_dc_by_hdc(hdc);
      *(struct printer_values  * )data = ppv;
        
      if (hdc == 0 )
        {
	  Tcl_AppendResult(interp, "printer job got unrecognized hdc ", hdcString, 0);
	  return TCL_ERROR;
        }
      if (ppv == 0 )
        {
        }
    }
    
  if (ppv && hdc == 0 )
    hdc = ppv->hDC;
    
  /* Should this command keep track of start/end state so two starts in a row
   *  automatically have an end inserted?
   .  */
  if ( argc == 0 )   /* printer job by itself.  */
    {
      const char *jobname;
      int status;
        
      status = JobInfo (-1, 0, &jobname);
      if ( status )
	Tcl_SetResult(interp, (char *)jobname, TCL_VOLATILE);
      return TCL_OK;
    }
  else if ( argc >= 1 )
    {
      if ( strcmp (*argv, "start") == 0 )
        {
	  const char *docname = "Tcl Printer Document";
	  int oldMode;
            
	  argc--;
	  argv++;
	  /* handle -name argument if present.  */
	  if ( argc >= 1 && strcmp( *argv, "-name" ) == 0 )
            {
	      argv++;
	      if ( --argc > 0 )
                {
		  docname = *argv;
                }
            }
            
	  /* Ensure the hDC is valid before continuing.  */
	  if ( hdc == NULL )
            {
	      Tcl_SetResult (interp, "Error starting print job: no printer context", TCL_STATIC);
	      return TCL_ERROR;
            }
            
	  /* Close off any other job if already in progress.  */
	  if ( JobInfo(-1, 0, 0) )
            {
	      EndDoc(ppv->hDC);
	      JobInfo(0, 0, 0);
            }
            
	  memset ( &di, 0, sizeof(DOCINFO) );
	  di.cbSize = sizeof(DOCINFO);
	  di.lpszDocName = docname;
            
	  /* *
	   *  If print to file is selected, this causes a popup dialog.
	   *  Therefore, in Tcl 8 and above, enable event handling
	   *  */
#if TCL_MAJOR_VERSION > 7
	  /* In Tcl versions 8 and later, a service call to the notifier is provided.  */
	  oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
#endif
	  StartDoc(hdc, &di);
	  JobInfo (1, docname, 0);
#if TCL_MAJOR_VERSION > 7
	  /* Return the service mode to its original state.  */
	  Tcl_SetServiceMode(oldMode);
#endif
	  if (ppv)
	    ppv->in_job = 1;
            
	  return TCL_OK;
        }
      else if ( strcmp (*argv, "end") == 0 )
        {
	  EndDoc(hdc);
	  JobInfo (0, 0, 0);
	  if (ppv)
	    ppv->in_job = 0;
            
	  return TCL_OK;
        }
    }
    
  Tcl_SetResult(interp, usage_message, TCL_STATIC);
  return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintPage--
 *
 *  Manage page by page printing.
 *
 * Results:
 *	Page printing executed. 
 *
 *----------------------------------------------------------------------
 */


static int PrintPage(ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  struct printer_values  * ppv = *(struct printer_values  * ) data;
  static char usage_message[] = "::tk::print::_print [-hDC hdc] [start|end]";
  HDC hdc = 0;
  const char *hdcString = 0;
    
  if ( argv[0] && ( strcmp(argv[0], "-hdc") == 0  || strcmp (argv[0], "-hDC") == 0 ) )
    {
      argc--;
      argv++;
      hdcString = argv[0];
      argc--;
      argv++;
    }
    
  if ( hdcString )
    {
      hdc = get_printer_dc(interp,hdcString);
      ppv = find_dc_by_hdc(hdc);
      *(struct printer_values  * )data = ppv;
        
      if (hdc == 0 )
        {
	  Tcl_AppendResult(interp, "printer page got unrecognized hdc ", hdcString, 0);
	  return TCL_ERROR;
        }
      if (ppv == 0 )
        {
	  Tcl_AppendResult(interp, "printer page got unrecognized hdc ", hdcString, 0);
	  return TCL_ERROR;
        }
    }
  /*
   *  Should this command keep track of start/end state so two starts in a row
   *  automatically have an end inserted?
   *  Also, if no job has started, should it start a printer job?
   .  */
  if ( argc >= 1 )
    {
      if ( strcmp (*argv, "start") == 0 )
        {
	  StartPage(ppv->hDC);
	  ppv->in_page = 1;
	  return TCL_OK;
        }
      else if ( strcmp (*argv, "end") == 0 )
        {
	  EndPage(ppv->hDC);
	  ppv->in_page = 0;
	  return TCL_OK;
        }
    }
    
  Tcl_SetResult(interp, usage_message, TCL_STATIC);
  return TCL_ERROR;
}

/* 
 *  This function gets physical page size in case the user hasn't
 *  performed any action to set it
 */
static int PrintPageAttr (HDC hdc, int *hsize,   int *vsize,
                          int *hscale,  int *vscale,
                          int *hoffset, int *voffset,
                          int *hppi,    int *vppi)
{
  int status = 0;
  if ( hdc == 0 )
    {
      return -1;  /* A value indicating failure.  */
    }
    
  *hsize   = GetDeviceCaps(hdc, PHYSICALWIDTH);
  *vsize   = GetDeviceCaps(hdc, PHYSICALHEIGHT);
  *hscale  = GetDeviceCaps(hdc, SCALINGFACTORX);
  *vscale  = GetDeviceCaps(hdc, SCALINGFACTORY);
  *hoffset = GetDeviceCaps (hdc, PHYSICALOFFSETX);
  *voffset = GetDeviceCaps (hdc, PHYSICALOFFSETY);
  *hppi    = GetDeviceCaps (hdc, LOGPIXELSX);
  *vppi    = GetDeviceCaps (hdc, LOGPIXELSY);
    
  return status;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintAttr--
 *
 *  Report printer attributes. In some cases, this function should probably get the information
 *  if not already available from user action.
 *
 * Results:
 *	Returns printer attributes.
 *
 *----------------------------------------------------------------------
 */

static int PrintAttr(ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  HDC hdc = 0;
  const char *hdcString = 0;
  /*
   *  Note: Currently, attributes are maintained ONCE per Tcl session.
   *  Later design may allow a set of attributes per hDC.
   *  In that case, the hDC is a component of this command.
   *  Meanwhile, the hDC is consulted as a means of ensuring initialization of
   *  the printer attributes only.
   */
  static char usage_message[] = "::tk::print::_print attr "
    "[-hDC hdc] "
    "[ [-get keylist] | [-set key-value-pair list] | [-delete key-list] | [-prompt] ]";
    
  struct printer_values  * ppv = *(struct printer_values  * ) data;
    
  Tcl_HashEntry *ent;
  Tcl_HashSearch srch;
    
  /* 
   * Get and set options? Depends on further arguments? Pattern matching?. 
   * Returns a collection of key/value pairs. Should it use a user-specified array name?.  
   * The attributes of interest are the ones buried in the dialog structures. 
   */
    
  /* For the first implementation, more than 100 keys/pairs will be ignored.  */
  char  * keys=0;
  int key_count = 0;
    
  int do_get = 0;
  int do_set = 0;
  int do_delete = 0;
  int do_prompt = 0;
  int i;
    
  /*
   *  This command should take an HDC as an optional parameter, otherwise using
   *  the one in the ppv structure?
   .  */
  for (i=0; i<argc; i++)
    {
      if ( strcmp(argv[i], "-get") == 0 )
        {
	  if ( argv[++i] == 0 )
            {
	      Tcl_AppendResult(interp, "\nMust supply list with -get\n", usage_message, 0 );
	      return TCL_ERROR;
            }
	  do_get = 1;
	  /* Now extract the list of keys.  */
	  if ( Tcl_SplitList(interp, argv[i], &key_count, &keys) == TCL_ERROR )
            {
	      Tcl_AppendResult(interp, "\nCan't parse list with -get\n", 
			       argv[i], "\n", usage_message, 0 );
	      return TCL_ERROR;
            }
        }
      else if (strcmp(argv[i], "-set") == 0 )
        {
	  /* With the change in philosophy to doing a per-hdc attribute setting,
	   *  the attributes are automatically synched, and use ResetDC
	   *  to update the HDC
           .  */
	  if ( argv[++i] == 0 )
            {
	      Tcl_AppendResult(interp, "\nMust supply list with -set\n", usage_message, 0 );
	      return TCL_ERROR;
            }
	  do_set = 1;
	  /* Extract the list of key/value pairs.  */
	  if ( Tcl_SplitList(interp, argv[i], &key_count, &keys) == TCL_ERROR )
            {
	      Tcl_AppendResult(interp, "\nCan't parse list with -set\n", 
			       argv[i], "\n", usage_message, 0 );
	      return TCL_ERROR;
            }
        }
      else if ( strcmp(argv[i], "-delete") == 0 )
        {
	  if ( argv[++i] == 0 )
            {
	      Tcl_AppendResult(interp, "\nMust supply list with -delete\n", usage_message, 0);
	      return TCL_ERROR;
            }
	  do_delete = 1;
	  /* Now extract the list of keys.  */
	  if ( Tcl_SplitList(interp, argv[i], &key_count, &keys) == TCL_ERROR )
            {
	      Tcl_AppendResult(interp, "\nCan't parse list with -delete\n", 
			       argv[i], "\n", usage_message, 0 );
	      return TCL_ERROR;
            }      
        }
      else if ( strcmp(argv[0], "-prompt") == 0 )
        {
	  do_prompt = 1;
        }
      else if ( strcmp(argv[0], "-hdc") == 0  || strcmp (argv[0], "-hDC") == 0 )
        {
	  i++;
	  hdcString = argv[i];
        }
      /* Ignore others or generate error?.  */
    }
    
  /* Check for any illegal implementations.  */
  if ( do_set + do_get + do_delete + do_prompt > 1 )
    {
      Tcl_AppendResult(interp, "\nCannot use two options from "
		       "-get, -set, -delete, and -prompt in same request.\n", 
		       usage_message, 
		       0);
      if (keys)
	Tcl_Free((char *)keys);
      return TCL_ERROR;
    } 
    
  if ( hdcString )
    {
      hdc = get_printer_dc(interp,hdcString);
      ppv = find_dc_by_hdc(hdc);
      *(struct printer_values  * )data = ppv;
        
      if (hdc == 0 )
        {
	  Tcl_AppendResult(interp, "::tk::print::_print attr got unrecognized hdc ", hdcString, 0);
	  return TCL_ERROR;
        }
      if (ppv == 0 )
        {
	  Tcl_AppendResult(interp, "::tk::print::_print attr got unrecognized hdc ", hdcString, 0);
	  return TCL_ERROR;
        }
    }
    
  /* 
   *  Handle the case where we are asking for attributes on a non-opened printer
   *  The two choices are (a) to consider this a fatal error for the printer attr
   *  command; and (b) to open the default printer. For now, we use choice (b)
   */
  if ( ppv == 0 || ppv == &default_printer_values || ppv->hDC == NULL )
    {
      /* In these cases, open the default printer, if any. If none, return an error.  */
      if ( PrintOpen(data, interp, 0, 0) != TCL_OK )
        {
	  Tcl_AppendResult(interp, "\nThere appears to be no default printer."
			   "\nUse '::tk::print::_print dialog select' before '::tk::print::_print attr'\n", 
			   0);
	  if (keys)
	    Tcl_Free((char *)keys);
	  return TCL_ERROR;
        }
      else
	Tcl_ResetResult(interp);   /* Remove the hDC from the result.  */
        
      /* This changes the ppv (via changing data in PrintOpen!.  */
      ppv = *(struct printer_values  * )data;
        
    }
    
  /* 
   *  This command must support two switches:
   *  -get: the list following this switch represents a set of
   *  "wildcard-matchable" values to retrieve from the attribute list.
   *  When found, they are reported ONCE in alphabetical order.
   *  -set: the LIST OF PAIRS following this switch represents a set
   *  of LITERAL keys and values to be added or replaced into the
   *  attribute list. Values CAN be set in this list that are not
   *  recognized by the printer dialogs or structures.
   */
  /* This is the "delete" part, used only by the -delete case.  */
  if ( do_delete )
    {
      int count_del = 0;
      char count_str[12+1];
        
      /* The only trick here is to ensure that only permitted
       *  items are deleted
       .  */
      static const char *illegal[] = {
	"device",
	"driver",
	"hDC", 
	"hdcname",
	"pixels per inch",
	"port",
	"resolution",
      };
      for ( ent = Tcl_FirstHashEntry(&ppv->attribs, &srch);
	    ent != 0;
	    ent = Tcl_NextHashEntry(&srch) )
        {
	  const char *key;
	  if ( (key   = (const char *)Tcl_GetHashKey(&ppv->attribs, ent))   != 0   )
            {
	      /* Test here to see if a list is available, and if this element is on it.  */
	      int found=0;
	      int i;
	      for (i=0; i<key_count; i++)
                {
		  if ( Tcl_StringMatch(key, keys[i]) == 1 )
                    {
		      int q;
		      for (q=0; q < sizeof illegal / sizeof (char *); q++)
			if ( strcmp(key, illegal[q]) == 0 )
			  break;
		      if ( q == sizeof illegal / sizeof (char *) )
			found = 1;
		      break;
                    }
                }
	      if (found == 0)
		continue;
            }
	  del_attribute(&ppv->attribs, key);
	  count_del++;
        }
        
      /* If the delete option is chosen, we're done.  */
      if (keys)
	Tcl_Free((char *)keys);
      sprintf(count_str, "%d", count_del);
      Tcl_SetResult(interp, count_str, TCL_VOLATILE);
      return TCL_OK;
    }
  /* This is the "set" part, used only by the -set case.  */
  else if ( do_set )
    {
      int k;
      /* Split each key, do the set, and then free the result.
       *  Also, replace keys[k] with just the key part.
       .  */
      for (k=0; k<key_count; k++)
        {
	  int scount;
	  char  * slist;
	  if ( Tcl_SplitList(interp, keys[k], &scount, &slist) == TCL_ERROR )
            {
	      Tcl_AppendResult(interp, "\nCan't parse list with -set\n", 
			       argv[i], "\n", usage_message, 0 );
            }
	  else
            {
	      if ( scount > 1 )
                {
		  set_attribute (&ppv->attribs, slist[0], slist[1]);
		  strcpy(keys[k], slist[0]);  /* Always shorter, so this should be OK.  */
                }
	      if ( slist )
		Tcl_Free((char *)slist);
            }
        }
        
      /* Here we should "synchronize" the pairs with the devmode.  */
      GetDevModeAttribs (&ppv->attribs, ppv->pdevmode);
      RestorePrintVals  (ppv, &ppv->pdlg, &ppv->pgdlg);
      /* -------------- added 8/1/02 by Jon Hilbert.  */
      /* tell the printer about the devmode changes 
	 This is necessary to support paper size setting changes
	 .  */
      DocumentProperties(GetActiveWindow(),ppv->hDC,ppv->pdevmode->dmDeviceName,
			 ppv->pdevmode,ppv->pdevmode,DM_IN_BUFFER|DM_OUT_BUFFER);
        
      /* Here we should modify the DEVMODE by calling ResetDC.  */
      ResetDC(ppv->hDC, ppv->pdevmode);
    } 
  else if ( do_prompt ) 
    {
      DWORD dwRet;
      HANDLE hPrinter;
      PRINTER_DEFAULTS pd = {0, 0, 0};
        
      pd.DesiredAccess = PRINTER_ALL_ACCESS;
      pd.pDevMode = ppv->pdevmode;
        
      OpenPrinter (ppv->pdevmode->dmDeviceName, &hPrinter, &pd);
      dwRet = DocumentProperties (
				  GetActiveWindow(), hPrinter, ppv->pdevmode->dmDeviceName,
				  ppv->pdevmode, ppv->pdevmode, DM_PROMPT | DM_IN_BUFFER | DM_OUT_BUFFER);
      if ( dwRet == IDCANCEL ) 
        {
	  /* The dialog was canceled. Don't do anything.  */
        } 
      else 
        {
	  if (dwRet != IDOK) {
	    ppv->errorCode = GetLastError();
	    sprintf(msgbuf, "::tk::print::_print attr -prompt: Cannot retrieve printer attributes: %ld (%ld)", (long) ppv->errorCode, dwRet);
	    Tcl_SetResult (interp, msgbuf, TCL_VOLATILE);
	    ClosePrinter(hPrinter);
	    return TCL_ERROR;
	  }
            
	  ppv->pdevmode->dmFields |= DM_PAPERSIZE;
	  if (ppv->pdevmode->dmPaperLength && ppv->pdevmode->dmPaperWidth) {
	    ppv->pdevmode->dmFields |= DM_PAPERWIDTH | DM_PAPERLENGTH;
	  }
	  SetDevModeAttribs (&ppv->attribs, ppv->pdevmode);
            
	  dwRet = DocumentProperties(GetActiveWindow(),hPrinter, ppv->pdevmode->dmDeviceName,
				     ppv->pdevmode,ppv->pdevmode,DM_IN_BUFFER | DM_OUT_BUFFER);
	  if (dwRet != IDOK) {
	    ppv->errorCode = GetLastError();
	    sprintf(msgbuf, "::tk::print::_print attr -prompt: Cannot set printer attributes: %ld", (long) ppv->errorCode);
	    Tcl_SetResult (interp, msgbuf, TCL_VOLATILE);
	    ClosePrinter(hPrinter);
	    return TCL_ERROR;
	  }
	  ResetDC(hPrinter, ppv->pdevmode);
        }
      ClosePrinter(hPrinter);
    }
    
  /* This is the "get" part, used for all cases of the command.  */
  for ( ent = Tcl_FirstHashEntry(&ppv->attribs, &srch);
	ent != 0;
	ent = Tcl_NextHashEntry(&srch) )
    {
      const char *key, *value;
      if ( (value = (const char *)Tcl_GetHashValue(ent)) != 0 &&
	   (key   = (const char *)Tcl_GetHashKey(&ppv->attribs, ent))   != 0   )
        {
	  /* Test here to see if a list is available, and if this element is on it.  */
	  if (do_set || do_get )
            {
	      int found=0;
	      int i;
	      for (i=0; i<key_count; i++) {
		  if ( Tcl_StringMatch(key, keys[i]) == 1 )
                    {
		      found = 1;
		      break;
                    }
                }
	      if (found == 0)
		continue;
            }
	  Tcl_AppendResult(interp, "{", 0);
	  Tcl_AppendElement(interp, key);
	  Tcl_AppendElement(interp, value);
	  Tcl_AppendResult(interp, "} ", 0);
        }
    }
    
  /*
   *  Sort the results.
   * Note: For the current set of values, the code below should work fine
   *  (it is specifically written for 8.0 and 8.1 compatibility, with strong
   *  belief it can be retrofit with little change to 7.5 and 7.6).
   *  However, if "arbitrary" strings including nulls are added to the list,
   *  it will fail to work any longer, and must be changed to fully "Obj"
   */
  {
    const char *cp;
#if TCL_MAJOR_VERSION == 8
    /* In earlier versions of Tcl, don't sort the list--too expensive.  */
    cp = Tcl_GetStringResult(interp);  /* JUST the attribute pairs: Tcl 8 and higher.  */
    Tcl_VarEval(interp, "lsort -dictionary -index 0 {", cp, "}", 0);  /* Tcl 8 and up */
    /* Tcl_Free(cp);  /* Not documented, but assume this has to be freed....  */
#endif
  }
           
  if (keys)
    Tcl_Free((char *)keys);
  return TCL_OK;
}
                  
/*
 *----------------------------------------------------------------------
 *
 * PrintOption--
 *
 *  Printer-specific options. 
 *
 * Results:
 *	Returns printer options.
 *
 *----------------------------------------------------------------------
 */
   
static int PrintOption(ClientData data, Tcl_Interp *interp, int argc, const char  * argv)
{
  /* Currently, there is only one option (autoclose)--so the logic is simple.  */
  int i;
  const char *cp;
  int errors = 0;
        
  static const char *usage = "::tk::print::_print option [ list of option/value ] ...\n"
    "  where options are\n"
    "    autoclose true/false -- default true";
        
  for (i=0; i<argc; i++)
    {
      /* Input is a list with 2 elements.  */
      char  * keys = 0;
      int key_count = 0;
      if ( Tcl_SplitList(interp, argv[i], &key_count, &keys) == TCL_ERROR 
	   || key_count != 2 )  /* count test added by Jon Hilbert.  */
	{
	  Tcl_AppendResult(interp, "Can't parse argument ", argv[i], "\n", 0);
	  errors++;
	  continue;
	}
      if  ( strcmp(keys[0], "autoclose") == 0 )
	{
	  autoclose = 0;  /* Set a default value.  */
	  Tcl_GetBoolean(interp, keys[1],&autoclose);  /* Replaced strcmp with Tcl routine -- Jon Hilbert.  */
	}
      else if ( strcmp(keys[0], "abortproc_var") == 0 )
	{
	  if ( keys[1] && keys[1][0] )
	    setAbortProcVarName(keys[1]);
	}
      /* Other cases go here in an "else if".  */
      if ( keys ) 
	Tcl_Free((char *)keys);
    }
        
  if ( autoclose != 0 )
    cp = "true";
  else
    cp = "false";
        
  Tcl_AppendResult (interp, "{ autoclose ", cp, " }",
		    "{ abortproc_var ", setAbortProcVarName(0), " }", 
		    0 );
  if (errors > 0 )
    Tcl_AppendResult(interp, "\n", usage, "\n", 0);
        
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * add_dc--
 *
 *  Adds device context. 
 *
 * Results:
 *	Device context added.
 *
 *----------------------------------------------------------------------
 */
   
static void add_dc(HDC hdc, struct printer_values *pv)
{
  Tcl_HashEntry *data;
  int status;
  data = Tcl_CreateHashEntry(&printer_hdcs, (const char *)hdc, &status);
  Tcl_SetHashValue(data,(const char *)pv);
}
    
/*
 *----------------------------------------------------------------------
 *
 * delete_dc--
 *
 *  Deletes device context. 
 *
 * Results:
 *	Device context deleted.
 *
 *----------------------------------------------------------------------
 */
   
    
static struct printer_values *delete_dc (HDC hdc)
{
  Tcl_HashEntry *data;
  struct printer_values *pv = 0;
  if ( (data = Tcl_FindHashEntry(&printer_hdcs, (const char *)hdc)) != 0 )
    {
      pv = (struct printer_values *)Tcl_GetHashValue(data);
      Tcl_DeleteHashEntry(data);
    }
  return pv;
}

/*
 *----------------------------------------------------------------------
 *
 * find_dc_by_hdc --
 *
 *  Finds device context. 
 *
 * Results:
 *	Device context found.
 *
 *----------------------------------------------------------------------
 */
   
    
static struct printer_values *find_dc_by_hdc(HDC hdc)
{
  Tcl_HashEntry *data;
  if ( (data = Tcl_FindHashEntry(&printer_hdcs, (const char *)hdc)) != 0 )
    return (struct printer_values *)Tcl_GetHashValue(data);
  return 0;
}
    
#define PRINTER_dc_type 32

/*
 *----------------------------------------------------------------------
 *
 * init_printer_dc_contexts --
 *
 *  Initializes DC contexts. 
 *
 * Results:
 *	Device contexts initialized.
 *
 *----------------------------------------------------------------------
 */
   
    
static void init_printer_dc_contexts(Tcl_Interp *interp)
{
  if (hdc_prefixof)
    hdc_prefixof(interp, PRINTER_dc_type, "printerDc");
}

/*
 *----------------------------------------------------------------------
 *
 * delete_printer_dc_contexts --
 *
 *  Deletes DC contexts. 
 *
 * Results:
 *	Device contexts deleted.
 *
 *----------------------------------------------------------------------
 */
   
    
static void delete_printer_dc_contexts(Tcl_Interp *interp)
{
  const char *contexts[1000];
  int   outlen = sizeof(contexts) / sizeof(const char *);
  int i;
  HDC hdc;
        
        
  /* Note: hdc_List, hdc_get, and hdc_delete do not use the interp argument.  */ 
  hdc_list(interp, PRINTER_dc_type, contexts, &outlen);
  for (i=0; i<outlen; i++)
    {
      if ( (hdc = (HDC)hdc_get(interp, contexts[i])) != 0 )
	{
	  delete_dc(hdc);
	  DeleteDC(hdc);
	}
      hdc_delete(interp, contexts[i]);
    }
}
    
/*
 *----------------------------------------------------------------------
 *
 * make_printer_dc_name --
 *
 *  Makes printer name. 
 *
 * Results:
 *	Printer name created.
 *
 *----------------------------------------------------------------------
 */
   
static const char *make_printer_dc_name(Tcl_Interp *interp, HDC hdc, struct printer_values *pv)
{
  add_dc(hdc, pv);
        
  if (hdc_create)
    return hdc_create(interp, hdc, PRINTER_dc_type);
  else
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * printer_name_valid --
 *
 *  Tests validity of printer name. 
 *
 * Results:
 *	Printer name tested.
 *
 *----------------------------------------------------------------------
 */
    
static int printer_name_valid(Tcl_Interp *interp, const char *name)
{
  if (hdc_loaded == 0 || hdc_valid == 0)
    return 0;
  return hdc_valid(interp, name, PRINTER_dc_type);
}


/*
 *----------------------------------------------------------------------
 *
 * is_valid_dc --
 *
 *  Tests validity of DC.
 *
 * Results:
 *	DC tested.
 *
 *----------------------------------------------------------------------
 */
    
    
static int is_valid_hdc (HDC hdc)
{
  int retval = 0;
  DWORD objtype = GetObjectType((HGDIOBJ)hdc);
  switch (objtype)
    {
      /* Any of the DC types are OK.  */
    case OBJ_DC: case OBJ_MEMDC: case OBJ_METADC: case OBJ_ENHMETADC:
      retval = 1;
      break;
      /* Anything else is invalid.  */
    case 0:  /* Function failed.  */
    default:
      break;
    }
  return retval;
}
    
    
/*
 *----------------------------------------------------------------------
 *
 * get_printer_dc --
 *
 *  Gets printer dc.
 *
 * Results:
 *	DC returned.
 *
 *----------------------------------------------------------------------
 */
        
static HDC get_printer_dc(Tcl_Interp *interp, const char *name)
{
  if ( printer_name_valid(interp, name) == 0 )
    {
      char *strend;
      unsigned long tmp;
            
      /* Perhaps it is a numeric DC.  */
      tmp = strtoul(name, &strend, 0);
      if ( strend != 0 && strend > name )
	{
	  if ( is_valid_hdc((HDC)tmp) == 0 )
	    {
	      tmp = 0;
	      Tcl_AppendResult(interp, "Error: Wrong type of handle for this operation: ",
			       "need a printer drawing context, got non-context address: ", name, "\n", 0);
	    }
	  return (HDC)tmp;
	}
      else
	{
	  Tcl_AppendResult(interp, "Error: Wrong type of handle for this operation: ",
			   "need a printer drawing context, got: ", name, "\n", 0);
	  return 0;
	}
    }
  return (HDC)hdc_get(interp, name);
        
}
   
 
/*
 * Local variables:
 * mode: c
 * indent-tabs-mode: nil
 * End:
 */