/*
 * rbcWinUtil.c --
 *
 *      This module contains WIN32 routines not included in the Tcl/Tk
 *      libraries.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

/*
 *--------------------------------------------------------------
 *
 * RbcGetPlatformId --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
RbcGetPlatformId(
    )
{
    static int      platformId = 0;

    if (platformId == 0) {
        OSVERSIONINFO   opsysInfo;

        opsysInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        if (GetVersionEx(&opsysInfo)) {
            platformId = opsysInfo.dwPlatformId;
        }
    }
    return platformId;
}

/*
 *--------------------------------------------------------------
 *
 * RbcLastError --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char           *
RbcLastError(
    )
{
    static char     buffer[1024];
    int             length;

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* Default language */
        buffer, 1024, NULL);
    length = strlen(buffer);
    if (buffer[length - 2] == '\r') {
        buffer[length - 2] = '\0';
    }
    return buffer;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
