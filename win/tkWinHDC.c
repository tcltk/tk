/*
 * tkWinHDC.c --
 *
 *      This module implements utility functions for accessing hardware device contexts
 *      for graphics rendering in Windows.
 *
 * Copyright © 2009 Michael I. Schwartz.
 * Copyright © 2021 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkWinHDC.h"


/*
 *----------------------------------------------------------------------
 *
 * hdc_create --
 *
 *  Creates device context.
 *
 * Results:
 *	 HDC created.
 *
 *----------------------------------------------------------------------
 */

const char * hdc_create (Tcl_Interp *interp, void *ptr, int type)
{
  struct hdc_value *pval;
  const char *name;
  Tcl_HashEntry *entry;
  int status;
  
  pval = (struct hdc_value *)Tcl_Alloc(sizeof(struct hdc_value));
  if (pval == 0)
  {
    return 0;
  }
  pval->addr = ptr;
  pval->type = type;

  name = Hdc_build_name(type);
  if ( ( entry = Tcl_CreateHashEntry(&hdcs, name, &status)) != 0 )
    Tcl_SetHashValue(entry, (ClientData)pval);
  return name;
}


/*
 *----------------------------------------------------------------------
 *
 * hdc_valid --
 *
 *  Tests validity of HDC.
 *
 * Results:
 *	 HDC tested.
 *
 *----------------------------------------------------------------------
 */

int hdc_valid (Tcl_Interp *interp, const char *hdcname, int type)
{
  struct hdc_value *val;
  Tcl_HashEntry *data;

  if ( (data = Tcl_FindHashEntry(&hdcs, hdcname)) != 0 )
  {
    val = (struct hdc_value *)Tcl_GetHashValue(data);

    if ( type <= 0 || val->type == type )
      return 1;
  }
  return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * hdc_delete --
 *
 *  Dletes device context.
 *
 * Results:
 *	 HDC created.
 *
 *----------------------------------------------------------------------
 */

int hdc_delete (Tcl_Interp *interp, const char *hdcname)
{
  struct hdc_value *val;
  Tcl_HashEntry *data;

  if ( (data = Tcl_FindHashEntry(&hdcs, hdcname)) != 0 )
  {
    val = (struct hdc_value *)Tcl_GetHashValue(data);

    Tcl_DeleteHashEntry(data);
    Tcl_Free((void *)val);
    return 1;
  }
  return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * hdc_get --
 *
 *  Gets device context.
 *
 * Results:
 *	 HDC returned.
 *
 *----------------------------------------------------------------------
 */

void * hdc_get (Tcl_Interp *interp, const char *hdcname)
{
  struct hdc_value *val;
  Tcl_HashEntry *data;

  if ( (data = Tcl_FindHashEntry(&hdcs, hdcname)) != 0 )
    val = (struct hdc_value *)Tcl_GetHashValue(data);
  else
    return 0;

  return val->addr;
}

/*
 *----------------------------------------------------------------------
 *
 * hdc_typeof --
 *
 *  Gets HDC type.
 *
 * Results:
 *	 Type returned.
 *
 *----------------------------------------------------------------------
 */


int hdc_typeof (Tcl_Interp *interp, const char *hdcname)
{
  struct hdc_value *val;
  Tcl_HashEntry *data;

  if ( (data = Tcl_FindHashEntry(&hdcs, hdcname)) != 0 )
    val = (struct hdc_value *)Tcl_GetHashValue(data);

  return val->type;
}

/*
 *----------------------------------------------------------------------
 *
 * hdc_prefixof --
 *
 *  Gets HDC prefix.
 *
 * Results:
 *	 Prefix returned.
 *
 *----------------------------------------------------------------------
 */

const char * hdc_prefixof (Tcl_Interp *interp, int type, const char *newprefix)
{
  const char *prefix;
  Tcl_HashEntry *data;

  if ( (data = Tcl_FindHashEntry(&hdcprefixes, (char *)type)) != 0 )
    prefix = (const char *)Tcl_GetHashValue(data);
    
  if ( newprefix )
  {
    char *cp;
    int siz, len;
    
    siz = strlen(newprefix);
    len = siz > 32 ? 32 : siz;
    
    if ( (cp = (char *)Tcl_Alloc(len+1)) != 0 )
    {
      int newptr = 0;
      
      strncpy (cp, newprefix, len);
      cp[len] = '\0';
      if ( data == 0 )
        data = Tcl_CreateHashEntry(&hdcprefixes,(char *)type,&newptr);
      Tcl_SetHashValue(data, (ClientData)cp);
      prefix = cp;
    }
  }

  return prefix;
}

/*
 *----------------------------------------------------------------------
 *
 * hdc_list --
 *
 *  Lists all device contexts.
 *
 * Results:
 *	List of device contexts returned.
 *
 *----------------------------------------------------------------------
 */
 
int hdc_list (Tcl_Interp *interp, int type, const char *out[], int *poutlen)
{
  Tcl_HashEntry *ent;
  Tcl_HashSearch srch;
  int i=0;
  const char *cp;
  int retval = 0;
  struct hdc_value *val;
  
  for ( ent = Tcl_FirstHashEntry(&hdcs, &srch); ent !=0; ent=Tcl_NextHashEntry(&srch))
  {
    if ( (cp = Tcl_GetHashKey(&hdcs, ent)) != 0 )
    {
      if ( i < *poutlen )
      {
        if ( (val = (struct hdc_value *)Tcl_GetHashValue(ent) ) != 0 )
        {
          if ( type <= 0 || type == val->type )
          {
            out[i++] = cp;
            retval++;
          }
        }
      }
    }
  }
  *poutlen = i;
  return retval;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */


