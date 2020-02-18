# tko(n) -- oo class like widgets

*   [SYNOPSIS](#SYNOPSIS)
*   [TKO STANDARD OPTIONS](#TKO-STANDARD-OPTIONS)
  [-class, class, Class](#-class)
  [-screen, screen, Screen](#-screen)
*   [DESCRIPTION](#DESCRIPTION)
*   [PUBLIC METHODS](#PUBLIC-METHODS)
*   [PRIVATE METHODS](#PRIVATE-METHODS)
*   [OPTIONS](#OPTIONS)
*   [EXAMPLES](#EXAMPLES)
*   [SEE ALSO](#SEE-ALSO)
*   [KEYWORDS](#KEYWORDS)
*   [COPYRIGHT](#COPYRIGHT)

<a name="SYNOPSIS"></a>
## SYNOPSIS

### Tko widgets

**::tko::toplevel** *pathName ?option value? ..*

**::tko::frame** *pathName ?option value? ..*

**::tko::labelframe** *pathName ?option value? ..*

### Class functions

**::tko initclass**

**::tko initfrom** *tkoclass*

**::tko initwrap** *widget readonlyoptionlist methodlist*

**::tko eventoption**

**::tko optiondef** *classname ?-option definitionlist? .. ?body?*

**::tko optiondel** *classname ?-option? ..*

**::tko optionget** *classname ?-option? ..*

**::tko optionhide** *classname ?-option? ..*

**::tko optionshow** *classname ?-option? ..*

### Widget methods

**my \_tko optionadd** *-option definitionlist ?body?*

**my \_tko optiondel** *-option* ..

**my \_tko optionhide** *-option* ..

**my \_tko optionshow** *-option* ..

<a name="TKO-STANDARD-OPTIONS"></a>
## TKO STANDARD OPTIONS

<a name="-class"></a>
Command-Line Name: **-class**
Database Name: **class**
Database Class: **Class**

> Define class for use in getting values from option database. Can only be set on widget creation time. The option should be the first option in the option definition list because it is needed to get other option values from the option database. Only the **-screen** option can precede it.

<a name="-screen"></a>
Command-Line Name: **-screen**
Database Name: **screen**
Database Class: **Screen**

> Affect creation of underlying widget structure. If given the created widget will be a toplevel widget. The option should be the very first option of an widget to be recognised.

<a name="DESCRIPTION"></a>
## DESCRIPTION

### Option definitionlist

In the *definitionlist* description below an entry with name *flags* can contain a combination of the following letters:

  - "r" the option is readonly and can only be set on creation
  - "h" The option is hidden from use in **cget** and **configure** methods.

The *definitionlist* can have one of the following forms:

  - *{-synonym flags}*

> Description of an synonym option. When *-option* is set then instead the provided *-synonym* option will be set.

  - *{dbname dbclass default flags}*

> Description of an option.
If *dbname* or *dbclass* is not empty then the values will be used to search for an default option value in the option database.
The search will need a **Tk\_Window** and therefore this definition can only be used in an widget class.
If both value are empty then the option definition can be used in an normal class create with **tko initclass**.
*default* is the default value if no value can be found in the option database.

### Function **::tko**

**::tko initclass**

> The function will create public **constructor**, **destructor**, **cget** and **configure** methods and private **_tko** and **_tko_configure** methods in the current class.

> The function can be used to add **cget** and **configure** functionality to an normal oo class.
No additional functionality is added.

> The function should be called only once inside the "oo::class create" script.
When called the list of used options will be cleared.

**::tko initfrom** *tkoclass*

> This function will provide the necessary initialization of an oo class as tko widget.
The argument *tkoclass* should be an **tko** widget class.
The class *tkoclass* will be the superclass of the current widget and all options of *tkoclass* will be added to our new class.

> If you create your own **constructor**, **destructor**, **cget** or **configure** methods you need to call **next** inside these function to call the function of the *tkoclass* superclass.

> The function should be called only once inside the "oo::class create" script.
When called the list of used options will be cleared.

**::tko initwrap** *widget readonlyoptionlist methodlist*

> This function will wrap an existing normal tk widget as an tko widget class.
The argumen *widget* is the name of the normal tk widget.
The argument *readonlyoptionlist* is a list of all readonly options of the given *widget*.
The argument *methodlist* is a list of methods to link to the wrapped *widget*.

> The function will create public **constructor**, **destructor**, **cget** and **configure** methods and private **_tko** and **_tko_configure** methods in the current class.

> The function should be called only once inside the "oo::class create" script.
When called the list of used options will be cleared.

**::tko eventoption**

> This option will send an <<TkoEventChanged>> virtual event to all widgets.
If a option value was set using the option database then the value of this option will updated with the current value of the option database.
The option database can so be used as a style source.

**::tko optiondef** *classname ?-option definitionlist? .. ?body?*

> The function will add or replace all given *-option definitionlist* pairs to the given *classname*. If an additional ?body? argument is given it will be used to create the *-option* method of the last given *-option* in *classname*

**::tko optiondel** *::classname ?-option? ..*

> The function will remove the given options from the defined class options of the given *::classname*. If no option is given then all existing options will be removed.

**::tko optionget** *::classname ?-option? ..*

> This function will return a list of *-option definitionlist* pairs ready for use in the **::tko optiondef** command.
The list consist of the specified options or all options if there are no options given.
THe option will be read from the fully qualified ?::classname? definitions.

**::tko optionhide** *::classname ?-option ..*

> Hide the given options from the use in **cget** and **configure** methods. If no options are given then return the list of all hidden options.

**::tko optionshow** *::classname ?-option ..*

> Unhide the given options from the use in **cget** and **configure** methods. If no options are given then return the list of all useable options.

<a name="tko-toplevel"></a>
### Widget **::tko::toplevel**

These class contain the functionality of the [toplevel][] widget command.

<a name="tko-frame"></a>
### Widget **::tko::frame**

These class contain the functionality of the [frame][] widget command.

<a name="tko-labelframe"></a>
### Widget **::tko::labelframe**

These class contain the functionality of the [labelframe][] widget command.

<a name="PUBLIC-METHODS"></a>
## PUBLIC METHODS

Widget methods can be dynamically added and removed at class or object level.

**NOTE** Do not change *tkoClass*'s behaviour. Instead create your own class and modify it to your need! Or change created widget objects behaviour.

<a name="method-cget"></a>
**cget** *-option*

> Return the current value of the given *option*.

<a name="method-configure"></a>
**configure**

> The method will return a sorted list of all configuration options.

**configure** *-option*

> Return value of given option.

**configure** *-option value* ..

> Use given *-option vlaue* pairs to set options.

<a name="PRIVATE-METHODS"></a>
## PRIVATE METHODS

<a name="method-_tko"></a>
**my \_tko optionadd** *-option definitionlist ?body?*

> Add a new option in the current object. The meaning of the *definitionlist* argument is the same as in the **::tko optionset** command.
The function will only add new options.
It is not possible to change object options.

**my \_tko optiondel** *-option* ..

> Delete the given option and unset the entry in the tko array variable. The created *-option* method's are not deleted. This is the task of the caller.

**my \_tko optionhide** *-option* ..

> If no *-option* is given return a list of all not configure'able options. Otherwise hide all of the given options.

**my \_tko optionshow** *-option* ..

> If no *-option* is given return a list of all configure'able options. Otherwise make all of the given options configure'able.

<a name="method-tko-configure"></a>
**my \_tko\_configure**

> This is an virtual method of the *tkoClass* widgets. This method will be called at the end of each **configure** *-option value ..* call. It can be implemented in each class to make necessary changes. If it is implemented it should also call **next** to notify underlying classes.

<a name="OPTIONS"></a>
## OPTIONS

Widget option values are saved in an option array **tko**. The option name is the field name in the array. Additionally is an field "**.**" containing the tk widget path name of the widget.

Widget options can be dynamically added and removed at class or object level.
It is possible to hide and unhide options.

<a name="EXAMPLES"></a>
## EXAMPLES

    #
    # Configurable object
    #
    oo::class create A {
      ::tko initclass
    }
    A create a1
    a1 configure ==>

    # Add class option
    ::tko optiondef A -o1 {o1 O1 v1 {}} {set tko(-o1) x}
    A create a2
    a2 configure
    ==> {-o1 o1 O1 v1 v1}

    # Add object option
    oo::define A method mycmd {args} {my {*}$args}
    a2 mycmd _tko optionadd -o2 {o2 O2 v2 {}} {variable tko; set tko(-o2) x}
    a2 configure
    ==> {-o1 o1 O1 v1 v1} {-o2 o2 O2 v2 x}

    #
    # Wrap an existing widget
    #
    oo::class create B {
      ::tko initwrap frame {-class -container -colormap -visual} {}
    }
    B .b
    lindex [.b configure] 0
    ==> {-background background Background SystemButtonFace SystemButtonFace}

    #
    # Create a new widget class.
    #
    oo::class create C {
      ::tko initfrom ::tko::frame
      constructor {args} {next {*}$args}
      destructor {next}
      method mycmd {args} {my {*}$args}
    }

    # Hide all inherited frame options
    ::tko optionhide C {*}[::tko optionhide C]
    ::tko optionshow C
    ==> -class -visual -colormap -container -borderwidth ...
    C .c
    .c configure
    ==>

    # Add a new option
    oo::define C method -o1 {} {puts $tko(-o1)}
    ::tko optiondef C -o1 {o1 O1 v1 {}}
    ::tko optionhide C
    ==> -o1

    # Add another option
    ::tko optiondef C -o2 {o2 O2 v2 {}} {puts $tko(-o2)}
    ::tko optionhide C
    ==> -o1 -o2

    # Add options at object level:
    C .c1
    .c1 mycmd _tko optionadd -o3 {o3 O3 v3 {}} {my variable tko; puts $tko(-o3)}
    .c1 configure
    ==> {-o1 o1 O1 v1 v1} {-o2 o2 O2 v2 v2} {-o3 o3 O3 v3 v3}

    # Show all frame options again
    .c1 mycmd _tko optionshow {*}[.c1 mycmd _tko optionshow]
    llength [.c1 configure]
    ==> 24

    # Intercept options
    oo::define C method -width {} {
        puts "[my cget -width]->$tko(-width)->[set tko(-width) 100]"
    }
    .c1 configure -width 1
    ==> 0->1->100

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

