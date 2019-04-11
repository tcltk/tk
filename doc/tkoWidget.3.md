# tkoWidget(3) -- oo class like widgets

*   [NAME](#NAME)
*   [SYNOPSIS](#SYNOPSIS)
*   [ARGUMENTS](#ARGUMENTS)  
*   [DESCRIPTION](#DESCRIPTION)  
*   [SEE ALSO](#SEE-ALSO)  
*   [KEYWORDS](#KEYWORDS)  
*   [COPYRIGHT](#COPYRIGHT)  

<a name="NAME"></a>
## NAME

TkoWidgetClassDefine,
TkoWidgetOptionVar,
TkoWidgetOptionGet,
TkoWidgetOptionSet,
TkoWidgetWindow -- tko widget class commands

<a name="SYNOPSIS"></a>
## SYNOPSIS

**#include "tkoWidget.h"**

int  
**TkoWidgetClassDefine**(*interp,clazz,classname,methods,options*)  
Tk\_Window \*  
**TkoWidgetWindow**(*object*)  
Tcl\_Obj \*  
**TkoWidgetOptionVar**(*object*)  
Tcl\_Obj \*  
**TkoWidgetOptionGet**(*interp,object,option*)  
int  
**TkoWidgetOptionSet**(*interp,context,option,type,meta,offset*)  

<a name="ARGUMENTS"></a>
## ARGUMENTS

| Tcl\_Interp **\*interp** | Used interpreter.  
| Tcl\_Class **clazz** | Oo class of widget.  
| Tcl\_Obj **\*classname** |Oo class name of widget.  
| const Tcl\_MethodType **\*methods** | This array defines class methods to create. For creation methods see [Tcl_NewMethod] manpage. If the method name of the first array entry is not NULL it will be used as **constructor**, if the second method name is not NULL it used as **destructor**. Then follow public methods until an entry with an method name equal NULL comes. Then follow private methods until an entry with an method name equal NULL comes.  
| const tkoWidgetOptionDefine **\*options** | This array contain option definitions.  
| Tcl\_Object **object** | This is the current object reference.  
| Tcl\_Obj **\*option** | The name of the used option.  
| Tcl\_ObjectContext **context** | The context of the current object. Used to get associated object data.  
| tkoWidgetOptionType **type** | A type used in the common option setting routine.  
| Tcl\_ObjectMetadataType **\*meta** | The type of metadata attaches to the current object.  
| size\_t **offset** | Offset of variable to set in the attaches meta data record.  

<a name="DESCRIPTION"></a>
## DESCRIPTION

The **TkoWidgetClassDefine** function can be used to define options and methods of an **tko::widget** subclass. The function is used in the widget class definition of a new tko widget class.

The **TkoWidgetWindow** function return the address of the internally created Tk\_Window. Subclasses should check the address on NULL after creation. If the Tk\_Window\* at these address is NULL the widget is destroyed and it should not be used.

The **TkoWidgetOptionVar** function return the globally accessible name of the array variable holding the option values. Additionall there is an field "**.**" containing the tk widget path name of the widget.

The **TkoWidgetOptionGet** function returns the current value of the given option.

The **TkoWidgetOptionSet** function can be used to check given *option* values and set C record structure fields at the given *offset*. The record will be retrieved using the given *meta* metadata. The *type* must be one of the **tkoWidgetOptionType** described below.

### Struct: `tkoWidgetOptionDefine`

    typedef struct tkoWidgetOptionDefine {
      const char *option;    /* Name of option. Starts with "-" minus sign */
      const char *dbname;    /* Option DB name or synonym option if dbclass is NULL */
      const char *dbclass;   /* Option DB class name or NULL for synonym options. */
      const char *defvalue;  /* Default value. */
      const char *proc;      /* If not NULL it is the body of the newly created -option method */
      Tcl_MethodCallProc *method;     /* If not NULL it is the function name of the -option method */
      int flags;             /* if TKO_WIDGETOPTIONREADONLY then option is only setable at creation time */
      tkoWidgetOptionType type;       /* if greater 0 then option type used in common option set method */
      Tcl_ObjectMetadataType *meta;   /* meta data address used in common option set method */
      int offset;            /* offset in meta data struct */
    } tkoWidgetOptionDefine;

### Enum: `tkoWidgetOptionType`

Suported enum type in the TkowidgetOptinSet() function. In comments is the type of the address provided in the **TkoWidgetOptionSet** funtion.

    typedef enum tkoWidgetOptionType {
        TKO_SET_CLASS = 1,     /* (Tcl_Obj **)address */
        TKO_SET_VISUAL, /* (Tcl_Obj **)address */
        TKO_SET_COLORMAP,       /* (Tcl_Obj **)address */
        TKO_SET_USENULL,        /* (Tcl_Obj **)address */
        TKO_SET_CONTAINER,      /* (int *)address */
        TKO_SET_TCLOBJ, /* (Tcl_Obj **)address */
        TKO_SET_XCOLOR, /* (Xcolor **)address */
        TKO_SET_3DBORDER,       /* (Tk_3DBorder *)address */
        TKO_SET_PIXEL,  /* (int *)address */
        TKO_SET_PIXELNONEGATIV, /* (int *)address */
        TKO_SET_PIXELPOSITIV,   /* (int *)address */
        TKO_SET_DOUBLE, /* (double *)address */
        TKO_SET_BOOLEAN,        /* (int *)address */
        TKO_SET_CURSOR, /* (Tk_Cursor *)address */
        TKO_SET_INT,    /* (int *)address */
        TKO_SET_RELIEF, /* (int *)address */
        TKO_SET_ANCHOR, /* (int *)address */
        TKO_SET_WINDOW, /* (Tk_Window *)address */
        TKO_SET_FONT,   /* (Tk_Font *)address */
        TKO_SET_STRING, /* (char **)address */
        TKO_SET_STRINGNULL,     /* (char **)address */
        TKO_SET_SCROLLREGION,   /* (int *[4])address */
        TKO_SET_JUSTIFY /* (Tk_Justify *)address */
    } tkoWidgetOptionType;

<a name="SEE-ALSO"></a>
## SEE ALSO

[frame][], [labelframe][], [toplevel][], [oo::class][]

<a name="KEYWORDS"></a>
## KEYWORDS

oo widget method option

<a name="COPYRIGHT"></a>
## COPYRIGHT

&copy; 2019- René Zaumseil <r.zaumseil@freenet.de>

BSD style license.

[options]: options.htm
[frame]: frame.htm
[labelframe]: labelframe.htm
[toplevel]: toplevel.htm
[oo::class]: class.htm
[graph]: graph.htm
[path]: path.htm
[Tkpath]: <https://sourceforge.net/projects/tclbitprint/>
[Rbc]: <https://sourceforge.net/projects/rbctoolkit/>


