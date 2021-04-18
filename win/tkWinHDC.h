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


int hdc_create(ClientData data, Tcl_Interp *interp, int argc, char **argv);
int hdc_delete(ClientData data, Tcl_Interp *interp, int argc, char **argv);
int hdc_list(ClientData data, Tcl_Interp *interp, int argc, char **argv);
int hdc_prefixof(ClientData data, Tcl_Interp *interp, int argc, char **argv);
int hdc_typeof(ClientData data, Tcl_Interp *interp, int argc, char **argv);
void * hdc_get (Tcl_Interp *interp, const char *hdcname);