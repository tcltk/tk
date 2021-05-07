#include <tcl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


/* 
 * Static data and function prototypes.
 */

struct hdc_value
{
  void *addr;
  int  type;
};

static unsigned long hdc_count = 0L;
static Tcl_HashTable hdcs;
static Tcl_HashTable hdcprefixes;
static char hdc_name [32+12+1];


const char * hdc_create (Tcl_Interp *interp, void *ptr, int type);
int hdc_valid (Tcl_Interp *interp, const char *hdcname, int type);
int hdc_delete (Tcl_Interp *interp, const char *hdcname);
const char * hdc_prefixof (Tcl_Interp *interp, int type, const char *newprefix);
int hdc_typeof (Tcl_Interp *interp, const char *hdcname);
void * hdc_get (Tcl_Interp *interp, const char *hdcname);
static const char *hdc_build_name(int type);
