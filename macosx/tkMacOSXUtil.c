#include <Carbon/Carbon.h>

#include "tkMacOSXUtil.h"

#define DIR_SEP_CHAR ':'

/*****************************************************************************/
pascal  OSErr   FSMakeFSSpecCompat(short vRefNum,
                                                                   long dirID, 
                                                                   ConstStr255Param fileName,
                                                                   FSSpec *spec)
{
        OSErr   result;
  
        {
                /* Let the file system create the FSSpec if it can since it does the job */
                /* much more efficiently than I can. */
                result = FSMakeFSSpec(vRefNum, dirID, fileName, spec);
                /* Fix a bug in Macintosh PC Exchange's MakeFSSpec code where 0 is */
                /* returned in the parID field when making an FSSpec to the volume's */
                /* root directory by passing a full pathname in MakeFSSpec's */
                /* fileName parameter. Fixed in Mac OS 8.1 */
                if ( (result == noErr) && (spec->parID == 0) )
                        spec->parID = fsRtParID;
        }
        return ( result );
}


/*****************************************************************************/
pascal  OSErr GetCatInfoNoName(short vRefNum,
                                                           long dirID,
                                                           ConstStr255Param name,
                                                           CInfoPBPtr pb)
{                                                        
        Str31 tempName; 
        OSErr error;            
                                        
        /* Protection against File Sharing problem */           
        if ( (name == NULL) || (name[0] == 0) )
        {
                tempName[0] = 0;
                pb->dirInfo.ioNamePtr = tempName;
                pb->dirInfo.ioFDirIndex = -1;   /* use ioDirID */
        }
        else
        {
                pb->dirInfo.ioNamePtr = (StringPtr)name;
                pb->dirInfo.ioFDirIndex = 0;    /* use ioNamePtr and ioDirID */
        }
        pb->dirInfo.ioVRefNum = vRefNum;
        pb->dirInfo.ioDrDirID = dirID;
        error = PBGetCatInfoSync(pb);
        pb->dirInfo.ioNamePtr = NULL;
        return ( error );
}
/*****************************************************************************/
pascal  OSErr   GetDirectoryID(short vRefNum,
                                                           long dirID,
                                                           ConstStr255Param name,
                                                           long *theDirID,
                                                           Boolean *isDirectory)
{                       
        CInfoPBRec pb;  
        OSErr error;    
        error = GetCatInfoNoName(vRefNum, dirID, name, &pb);
        if ( error == noErr )   
        {           
                *isDirectory = (pb.hFileInfo.ioFlAttrib & kioFlAttribDirMask) != 0;
                if ( *isDirectory )
                {               
                        *theDirID = pb.dirInfo.ioDrDirID;
                }       
                else            
                {                       
                        *theDirID = pb.hFileInfo.ioFlParID;
                }
        }

        return ( error );
}

/*****************************************************************************/
pascal  OSErr   FSpGetDirectoryID(const FSSpec *spec,
                                                                  long *theDirID,
                                                                  Boolean *isDirectory)
{       
        return ( GetDirectoryID(spec->vRefNum, spec->parID, spec->name,
                         theDirID, isDirectory) );
}

/*
 *----------------------------------------------------------------------
 *
 * FSpPathFromLocation --
 *
 *	This function obtains a full path name for a given macintosh
 *	FSSpec.  Unlike the More Files function FSpGetFullPath, this
 *	function will return a C string in the Handle.  It also will
 *	create paths for FSSpec that do not yet exist.
 *
 * Results:
 *	OSErr code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

OSErr
FSpPathFromLocation(
    FSSpec *spec,		/* The location we want a path for. */
    int *length,		/* Length of the resulting path. */
    Handle *fullPath)		/* Handle to path. */
{
    OSErr err;
    FSSpec tempSpec;
    CInfoPBRec pb;
	
    *fullPath = NULL;
	
    /* 
     * Make a copy of the input FSSpec that can be modified.
     */
    BlockMoveData(spec, &tempSpec, sizeof(FSSpec));
	
    if (tempSpec.parID == fsRtParID) {
	/* 
	 * The object is a volume.  Add a colon to make it a full 
	 * pathname.  Allocate a handle for it and we are done.
	 */
	tempSpec.name[0] += 2;
	tempSpec.name[tempSpec.name[0] - 1] = DIR_SEP_CHAR;
	tempSpec.name[tempSpec.name[0]] = '\0';
		
	err = PtrToHand(&tempSpec.name[1], fullPath, tempSpec.name[0]);
    } else {
	/* 
	 * The object isn't a volume.  Is the object a file or a directory? 
	 */
	pb.dirInfo.ioNamePtr = tempSpec.name;
	pb.dirInfo.ioVRefNum = tempSpec.vRefNum;
	pb.dirInfo.ioDrDirID = tempSpec.parID;
	pb.dirInfo.ioFDirIndex = 0;
	err = PBGetCatInfoSync(&pb);

	if ((err == noErr) || (err == fnfErr)) {
	    /* 
	     * If the file doesn't currently exist we start over.  If the
	     * directory exists everything will work just fine.  Otherwise we
	     * will just fail later.  If the object is a directory, append a
	     * colon so full pathname ends with colon, but only if the name is
	     * not empty.  NavServices returns FSSpec's with the parent ID set,
	     * but the name empty...
	     */
	    if (err == fnfErr) {
		BlockMoveData(spec, &tempSpec, sizeof(FSSpec));
	    } else if ( (pb.hFileInfo.ioFlAttrib & ioDirMask) != 0 ) {
	        if (tempSpec.name[0] > 0) {
		    tempSpec.name[0] += 1;
		    tempSpec.name[tempSpec.name[0]] = DIR_SEP_CHAR;
		}
	    }
			
	    /* 
	     * Create a new Handle for the object - make it a C string.
	     */
	    tempSpec.name[0] += 1;
	    tempSpec.name[tempSpec.name[0]] = '\0';
	    err = PtrToHand(&tempSpec.name[1], fullPath, tempSpec.name[0]);
	    if (err == noErr) {
		/* 
		 * Get the ancestor directory names - loop until we have an 
		 * error or find the root directory.
		 */
		pb.dirInfo.ioNamePtr = tempSpec.name;
		pb.dirInfo.ioVRefNum = tempSpec.vRefNum;
		pb.dirInfo.ioDrParID = tempSpec.parID;
		do {
		    pb.dirInfo.ioFDirIndex = -1;
		    pb.dirInfo.ioDrDirID = pb.dirInfo.ioDrParID;
		    err = PBGetCatInfoSync(&pb);
		    if (err == noErr) {
			/* 
			 * Append colon to directory name and add 
			 * directory name to beginning of fullPath.
			 */
			++tempSpec.name[0];
			tempSpec.name[tempSpec.name[0]] = DIR_SEP_CHAR;
						
			(void) Munger(*fullPath, 0, NULL, 0, &tempSpec.name[1],
				tempSpec.name[0]);
                        fprintf(stderr,"mem\n");
			err = MemError();
		    }
		} while ( (err == noErr) &&
			(pb.dirInfo.ioDrDirID != fsRtDirID) );
	    }
	}
    }
    
    /*
     * On error Dispose the handle, set it to NULL & return the err.
     * Otherwise, set the length & return.
     */
    if (err == noErr) {
	*length = GetHandleSize(*fullPath) - 1;
    } else {
	if ( *fullPath != NULL ) {
	    DisposeHandle(*fullPath);
	}
	*fullPath = NULL;
	*length = 0;
    }

    return err;
}

/*
 *----------------------------------------------------------------------
 *
 * FSpLocationFromPath --
 *
 *	This function obtains an FSSpec for a given macintosh path.
 *	Unlike the More Files function FSpLocationFromFullPath, this
 *	function will also accept partial paths and resolve any aliases
 *	along the path.  
 *
 * Results:
 *	OSErr code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

OSErr
FSpLocationFromPath(
    int length,			/* Length of path. */
    const char *path,		/* The path to convert. */
    FSSpecPtr fileSpecPtr)	/* On return the spec for the path. */
{
    Str255 fileName;
    OSErr err;
    short vRefNum;
    long dirID;
    int pos, cur;
    Boolean isDirectory;
    Boolean wasAlias;

    /*
     * Check to see if this is a full path.  If partial
     * we assume that path starts with the current working
     * directory.  (Ie. volume & dir = 0)
     */
    vRefNum = 0;
    dirID = 0;
    cur = 0;
    if (length == 0) {
        return fnfErr;
    }
    if (path[cur] == DIR_SEP_CHAR) {
	cur++;
	if (cur >= length) {
	    /*
	     * If path = ":", just return current directory.
	     */
	    FSMakeFSSpecCompat(0, 0, NULL, fileSpecPtr);
	    return noErr;
	}
    } else {
	while (path[cur] != DIR_SEP_CHAR && cur < length) {
	    cur++;
	}
	if (cur > 255) {
	    return bdNamErr;
	}
	if (cur < length) {
	    /*
	     * This is a full path
	     */
	    cur++;
	    strncpy((char *) fileName + 1, path, cur);
	    fileName[0] = cur;
	    err = FSMakeFSSpecCompat(0, 0, fileName, fileSpecPtr);
	    if (err != noErr) return err;
	    FSpGetDirectoryID(fileSpecPtr, &dirID, &isDirectory);
	    vRefNum = fileSpecPtr->vRefNum;
	} else {
	    cur = 0;
	}
    }
    
    isDirectory = 1;
    while (cur < length) {
	if (!isDirectory) {
	    return dirNFErr;
	}
	pos = cur;
	while (path[pos] != DIR_SEP_CHAR && pos < length) {
	    pos++;
	}
	if (pos == cur) {
	    /* Move up one dir */
	    /* cur++; */
            fileName[1] = DIR_SEP_CHAR;
            fileName[2] = DIR_SEP_CHAR;
	    fileName[0] = 2;
	} else if (pos - cur > 255) {
	    return bdNamErr;
	} else {
	    strncpy((char *) fileName + 1, &path[cur], pos - cur);
	    fileName[0] = pos - cur;
	}
	err = FSMakeFSSpecCompat(vRefNum, dirID, fileName, fileSpecPtr);
	if (err != noErr) return err;
	err = ResolveAliasFile(fileSpecPtr, true, &isDirectory, &wasAlias);
	if (err != noErr) return err;
	FSpGetDirectoryID(fileSpecPtr, &dirID, &isDirectory);
	vRefNum = fileSpecPtr->vRefNum;
	cur = pos;
	if (path[cur] == DIR_SEP_CHAR) {
	    cur++;
	}
    }
    
    return noErr;
}
