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

Tko\_WidgetClassDefine,
Tko\_WidgetCreate,
Tko\_WidgetDestroy,
Tko\_WidgetClientData,
Tko\_WidgetOptionGet,
Tko\_WidgetOptionSet,

<a name="SYNOPSIS"></a>
## SYNOPSIS

**#include "tkoWidget.h"**

int
**Tko\_WidgetClassDefine**(*interp,classname,methods,options*)
int
**Tko\_WidgetCreate**(*clientdata,interp,object,createmode,arglist*)
void
**Tko\_WidgetDestroy**(*context*)
ClientData
**Tko\_WidgetClientData**(*context*)
Tcl\_Obj \*
**Tko\_WidgetOptionGet**(*widget,option*)
int
**Tko\_WidgetOptionSet**(*widget,option,value*)

<a name="ARGUMENTS"></a>
## ARGUMENTS

| Tcl\_Interp **\*interp** | Used interpreter.
| Tcl\_Obj **\*classname** |Oo class name of widget.
| const Tcl\_MethodType **\*methods** | This array defines class methods to create. For creation methods see [Tcl\_NewMethod] manpage. If the method name of the first array entry is not NULL it will be used as **constructor**, if the second method name is not NULL it used as **destructor**. Then follow public methods until an entry with an method name equal NULL comes. Then follow private methods until an entry with an method name equal NULL comes.
| const Tko\_WidgetOptionDefine **\*options** | This array contain option definitions.
| Tcl\_Object **object** | This is the current object reference.
| Tko_WidgetCreateMode **createmode** | When =1 then create a toplevel otherwise a frame window.
| Tcl\_Obj **arglist** | Argument list of constructor call.
| ClientData **cientdata** | Pointer to widget structure. First part in this struct is Tko\_Widget. It
| Tcl\_ObjectContext **context** | Context of method calls.
| Tcl\_Obj **\*option** | The name of the used option.
| Tcl\_Obj **\*value** | New value of the given option.

<a name="DESCRIPTION"></a>
## DESCRIPTION

The **Tko\_WidgetClassDefine** function create a new tko widget class of *classname*. The function create the class add common methods (cget, configure, \_tko\_configure) and then add given methods and options.

The **Tko\_WidgetCreate** function create a new window. The *clientdata* should be *ckalloc*ed in the widget constructor. The function add the given *clientdata* to the object metadata. The function should be called in a C widget constructor.

The **Tko\_WidgetDestroy** function clears all internal widget data. The function also arrange the *ckfree* of the *clientdata*.

The **Tko\_WidgetClientData** should be used from inside widget methods to get the widget structure data given in the **Tko\_WidgetCreate** function.

The **Tko\_WidgetOptionGet** function returns the current value of the given option.

The **Tko\_WidgetOptionSet** function set the given *option* to the new given *value*.

### Enum: `Tko_WidgetOptionType`

Suported enum type in the **Tko\_WidgetOptionDefine** definition. As comment is the type of the address provided in the **Tko\_WidgetOptionDefine** definition.

    typedef enum Tko\_WidgetOptionType {
        TKO_SET_CLASS = 1,     /* (Tcl_Obj **)address */
        TKO_SET_VISUAL, /* (Tcl_Obj **)address */
        TKO_SET_COLORMAP,       /* (Tcl_Obj **)address */
        TKO_SET_USE,        /* (Tcl_Obj **)address */
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
        TKO_SET_SCROLLREGION,   /* (int *[4])address */
        TKO_SET_JUSTIFY /* (Tk_Justify *)address */
    } Tko\_WidgetOptionType;

### Enum: `Tko_WidgetCreateMode`

Supported values in **Tko\_WdigetCreate()** function call.

    typedef enum Tko_WidgetCreateMode {
      TKO_CREATE_WIDGET, /* Create new widget */
      TKO_CREATE_TOPLEVEL, /* Create new toplevel widget */
      TKO_CREATE_CLASS, /* See "tko initclass" */
      TKO_CREATE_WRAP /* See "tko initwrap" */
    } Tko_WidgetCreateMode;

### Struct: `Tko_WidgetOptionDefine`

Widget definition data used in class.
An option set method "-option" is created in the following order:
  - "option"=NULL indicate the end of a list of option definitions.
  - If "method" is given it will be used as option set method.
  - If "type" is greater 0 a common option set method will be used.
    In this case "offset" are used as offset in the widget structure.

    typedef struct Tko_WidgetOptionDefine {
        const char *option;    /* Name of option. Starts with "-" minus sign */
        const char *dbname;    /* Option DB name or synonym option if dbclass is NULL */
        const char *dbclass;   /* Option DB class name or NULL for synonym options. */
        const char *defvalue;  /* Default value. */
        int flags;             /* bit array of TKO_OPTION_* values to configure option behaviour */
        Tcl_MethodCallProc *method;    /* If not NULL it is the function name of the -option method */
        Tko_WidgetOptionType type;  /* if greater 0 then option type used in common option set method */
        int offset;            /* offset in meta data struct */
    } Tko_WidgetOptionDefine;
    #define TKO_OPTION_READONLY 0x1 /* option is only setable at creation time */

### Struct: `Tko_Widget`

Widget structure data used in objects.
These structure will be filled in the **Tko\_WidgetCreate** call and cleared in
the **Tko\_WidgetDestroy** call. Widget methods should check the value of *tkWin* on NULL before using it.

    typedef struct Tko_Widget {
        Tk_Window tkWin;       /* Window that embodies the widget. NULL means
                                * that the window has been destroyed but the
                                * data structures haven't yet been cleaned
                                * up.*/
        Display *display;      /* Display containing widget. Used, among
                                * other things, so that resources can be
                                * freed even after tkwin has gone away. */
        Tcl_Interp *interp;    /* Interpreter associated with widget. */
        Tcl_Command widgetCmd; /* Token for command. */
        Tcl_Object object;     /* our own object */
        Tcl_Obj *myCmd;        /* Objects "my" command. Needed to call internal methods. */
        Tcl_Obj *optionsArray; /* Name of option array variable */
        Tcl_HashTable *optionsTable; /* Hash table containing all used options */
    } Tko_Widget;


<a name="EXAMPLES"></a>
### EXAMPLES

    static Tko_WidgetOptionDefine myOptions[] = {
        /*
         * Readonly option, only setable on creation time.
         * Use of internal standard option setting function.
         */
        {"-class","class","Class","TkoFrame",TKO_OPTION_READONLY,
            NULL,NULL,TKO_SET_CLASS,NULL,0},
        /*
         * Option value in structure have NULL value when option is empty.
         * Use of internal standard option setting function.
         */
        {"-background","background","Background",DEF_FRAME_BG_COLOR,TKO_OPTION_NULL,
            NULL,NULL,TKO_SET_3DBORDER,&frameMeta,offsetof(tkoFrame, border)},
        /*
         * Use own provided oo method to set option value.
         */
        {"-backgroundimage","backgroundImage","BackgroundImage",DEF_FRAME_BG_IMAGE,0,
            NULL,FrameMethod_backgroundimage,0,NULL,0},
        /*
         * Synonym option definition.
         */
        {"-bg","-background",NULL,NULL,0,NULL,NULL,0,NULL,0},
        /*
         * Indicate end of options in array.
         */
        {NULL,NULL,NULL,NULL,0,NULL,NULL,0,NULL,0}
    };

For detailed examples see also the implementation of **tko::toplevel**, **tko::frame** and **tko::labelframe** widgets in file generic/tko/tkoFrame.c.

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


