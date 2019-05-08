# tko::widget(n) -- oo class like widgets

*   [SYNOPSIS](#SYNOPSIS)
*   [TKO STANDARD OPTIONS](#TKO-STANDARD-OPTIONS)  
  [-class, class, Class](#-class)  
  [-screen, screen, Screen](#-screen)  
*   [DESCRIPTION](#DESCRIPTION)  
*   [WIDGET METHODS](#WIDGET-METHODS)  
*   [WIDGET OPTIONS](#WIDGET-OPTIONS)  
*   [EXAMPLES](#EXAMPLES)  
*   [SEE ALSO](#SEE-ALSO)  
*   [KEYWORDS](#KEYWORDS)  
*   [COPYRIGHT](#COPYRIGHT)  

<a name="SYNOPSIS"></a>
## SYNOPSIS

    oo::class create myWidget {
      {*}$::tko::unknown    ;# define unknown method to support common tk widget style
      superclass "tkoClass" ;# one of the provided tko widget class's
      variable tko          ;# array with options *$tko(-option)* and widget path *$tko(.)*
      method -myoption {...};# deal with option when set
      ...
      constructor {optionlist arglist} {
        next [concat {
          {-myoption myOption MyOption value}
          ...
        } $optionlist] $arglist
        ...
      }
      destructor {next}
    }

The command creates a new Tcl class whose name is *widgetClass*. This command may be used to create new widgets. Each new widget class has as a *tkoClass* as superclass. The common functionality is in the **tko::widget** class. Currently the following *tkoClass* superclasses are provided:

**::tko::toplevel** *pathName ?option value? ..*

**::tko::frame** *pathName ?option value? ..*

**::tko::labelframe** *pathName ?option value? ..*

**::graph** *pathName ?option value? ..*

**::path** *pathName ?option value? ..*

<a name="TKO-STANDARD-OPTIONS"></a>
## TKO STANDARD OPTIONS

<a name="-class"></a>
Command-Line Name: **-class**  
Database Name: **class**  
Database Class: **Class**

> > Define class for use in getting values from option database. Can only be set on widget creation time.

<a name="-screen"></a>
Command-Line Name: **-screen**  
Database Name: **screen**  
Database Class: **Screen**

> > Affect creation of underlying widget structure. If given the created widget will be a toplevel widget.

<a name="DESCRIPTION"></a>
## DESCRIPTION

The **tko::widget** class contain the common widget functionality. To get these functionality you have to create a subclass of the **tko::widget** class. This can only be done in C. To use the functionality on tcl script level the following classes are provided.

<a name="tko-toplevel"></a>
## tko::toplevel

These class contain the functionality of the [toplevel][] widget command.

<a name="tko-frame"></a>
## tko::frame

These class contain the functionality of the [frame][] widget command.

<a name="tko-labelframe"></a>
## tko::labelframe

These class contain the functionality of the [labelframe][] widget command.

<a name="tko-graph"></a>
## graph

These class contain the functionality of the [Rbc][] graph widget command. It is described in detail in the [graph][] manpage.

<a name="tko-path"></a>
## path

These class contain the functionality of the [Tkpath][] widget command. It is described in detail in the [path][] manpage.

<a name="WIDGET-METHODS"></a>
### WIDGET METHODS

Widget methods can be dynamically added and removed at class or object level.

**NOTE** Do not change *tkoClass*'s behaviour. Instead create your own class and modify it to your need! Or change created widget objects behaviour.

The **tko::widget** class provides the following methods.

<a name="method-constructor"></a>
**constructor {optionlist arglist}**

> The *optionlist* contain a sorted list off option descriptions as described in **configure optonadd**. It will be processed in the **tko::widget** constructor in the given order. It should always start with the "-class" option definition. The necessary *-option* method's should already be exist.

> The *arglist* is the normal *-option value ..* list of all tk widgets.

> Each widget class constructor should call **next** *$optionlist $arglist*!


<a name="method-destructor"></a>
**destructor {}**

> Here you can free your own ressources. Don't forget to call **next**! After **next** the **tko::widget** data are gone (widget path, tko array variable).

<a name="method-cget"></a>
**cget** *option*

> Return the current vlaue of the given *option*.

<a name="method-configure"></a>
**configure** *args*

> **configure**

> > If *args* is empty the method will return a sorted list of all configuration options.

> **configure** *-option*

> > If we have one element in *args* starting with a minus sign ("-") then the method return the configuration list including the current value of the given *-option*.

> **configure** *-option value ..*

> > If we have an even number list in *args* and the first element starts with a minus sign ("-") then the method does configure all the given option-value pairs. If an error occurs the the corresponding element is not set and the method gives an error. Alrready successfull set options remain.

> **configure init**

> > This is an internal function used in constructing new widgets. It is used in the *unknown* method to initialize all options.

> **configure optionadd** *-synonym -option*

> > Add a *-synonym* for a given *-option*. The *-option* needs not to be defined at this time.

> **configure optionadd** *-synonym dbnam dbclass ?default? ?flags?*

> > Add a new option. If ?flags? is equal "1" then the option is readonly and can only be set in this call. Before adding a new option a *-option* method must created. The method will be called without any arguments. The method can access the new value using the *tko(-option)* array variable. If the method throws a n error the array variable will be reset to the old value.

> **configure optiondel** *-option*

> > Delete the given option and unset the entry in the tko array variable. The created *-option* method's are not deleted. This is the task of the caller.

> **configure optionhide** *?-option? ..*

> > If no *-option* is given return a list of all not configure'able options. Otherwise hide all of the given options.

> **configure optionshow** *?-option? ..*

> > If no *-option* is given return a list of all configure'able options. Otherwise make all of the given options configure'able.

> **configure optionvar**

> > The method return the global varname of the tko array variable holding all option values.

<a name="method-cget"></a>
**\_tko\_configure**

> This is an virtuel method of the **tko::widget** class. This method will be called at the end of each **configure** *-option value ..* call. It can be implemented in each class to amke necessary changes. If it is implemented it should also call **next** to notify underlying classes.

<a name="WIDGET-OPTIONS"></a>
### WIDGET OPTIONS

Widget option values are saved in an option array. The option name is the field name in the array. Additionally is an field "**.**" containing the tk widget path name of the widget. The name of the option array variable can be retrieved using the following code:
    set myVar [.w configure optionvar]
    parray $myVar

Widget options can be dynamically added and removed at class or object level.
It is possible to hide and unhide options.

<a name="EXAMPLES"></a>
### EXAMPLES

    # Add options at class creation:
    oo::class create ::myWidget {
      {*}$::tko::unknown
      superclass ::tko::frame
      variable tko
      method -myoption {} {puts $tko(-myoption)}
      method -myreadonly {} {puts $tko(-myreadonly)}
      constructor {optionlist arglist} {
        next [concat {
          {-myoption myOption MyOption value}
          {-myreadonly myReadonly MyReadonly value 1}
        } $optionlist] $arglist
      }
    }
    proc output {} {
      puts "config: [.w configure]"
      puts "normal: [.w configure optionhide]"
      puts "hidden: [.w configure optionshow]"
    }
    # Add options at object level:
    ::myWidget .w
    oo::objdefine .w method -o1 {} {my variable tko; puts $tko(-o1)}
    oo::objdefine .w method -o2 {} {my variable tko; puts $tko(-o2)}
    .w configure optionadd -o1 o1 O1 v1 1 ;#->
    .w configure optionadd -o2 o2 O2 v2
    output
    # Remove one and hide rest
    .w configure optiondel -o2
    .w configure optionhide {*}[.w configure optionshow]
    output
    # Reverse state
    .w configure optionshow {*}[.w configure optionhide]
    output


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


