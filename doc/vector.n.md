# vector(n) -- Vector data type for Tcl

*   [NAME]((#NAME)
*   [SYNOPSIS](#SYNOPSIS)
*   [DESCRIPTION](#DESCRIPTION)
*   [SYNTAX](#SYNTAX)
  * [graph::vector configure](#graph::vector-configure) *?option value?...*
  * [graph::vector create](#graph::vector-create) *vectorName?(...)? ?option value?...*
  * [graph::vector destroy](#graph::vector-destroy) *vectorName ?vectorName?...*
  * [graph::vector expression](#graph::vector-expression) *expression*
  * [graph::vector names](#graph::vector-names) *?pattern...?*
  * [graph::vector op](#graph::vector-op) *operation vectorName ?arg?...*
*   [VECTOR INDICES](#VECTOR-INDICES)
*   [VECTOR OPERATIONS](#VECTOR-OPERATIONS)
  * [vectorName +](#vectorName-plus") *item*
  * [vectorName -](#vectorName-minus") *item*
  * [vectorName `*`](#vectorName-mult") *item*
  * [vectorName /](#vectorName-div") *item*
  * [vectorName append](#vectorName-append) *item ?item?...*
  * [vectorName binread](#vectorName-binread) *channel ?length? ?switches?*
  * [vectorName binwrite](#vectorName-binwrite) *channel ?length? ?-at index?*
  * [vectorName clear](#vectorName-clear)
  * [vectorName delete](#vectorName-delete) *index ?index?...*
  * [vectorName dup](#vectorName-dup) *destName*
  * [vectorName expr](#vectorName-expr) *expression*
  * [vectorName index](#vectorName-index) *index ?value?...*
  * [vectorName insert](#vectorName-insert) *index item ?item?...*
  * [vectorName length](#vectorName-length) *?newSize?*
  * [vectorName matrix](#vectorName-matrix) *...*
    * [vectorName matrix copy](#vectorName-matrix-copy) *dstcolumn srccolumn ?srcVec?*
    * [vectorName matrix delete](#vectorName-matrix-delete) *column*
    * [vectorName matrix get](#vectorName-matrix-get) *column*
    * [vectorName matrix insert](#vectorName-matrix-insert) *column ?initvalue?*
    * [vectorName matrix multiply](#vectorName-matrix-multiply) *srcVec ?dstVec?*
    * [vectorName matrix numcols](#vectorName-matrix-numcols) *?size?*
    * [vectorName matrix numrows](#vectorName-matrix-numrows) *?size?*
    * [vectorName matrix set](#vectorName-matrix-set) *column ?valuelist?*
    * [vectorName matrix shift](#vectorName-matrix-shift) *column amount ?startoffset?*
    * [vectorName matrix sort](#vectorName-matrix-sort) *column ?-reverse?*
    * [vectorName matrix transpose](#vectorName-matrix-transpose)
  * [vectorName merge](#vectorName-merge) *srcName ?srcName?...*
  * [vectorName notify](#vectorName-notify) *?keyword? ?script?*
  * [vectorName populate](#vectorName-populate) *destName ?density?*
  * [vectorName range](#vectorName-range) *firstIndex ?lastIndex?...*
  * [vectorName search](#vectorName-search) *value ?value?*
  * [vectorName set](#vectorName-set) *item*
  * [vectorName seq](#vectorName-seq) *start ?finish? ?step?*
  * [vectorName sort](#vectorName-sort) *?-reverse? ?argName?...*
  * [vectorName split](#vectorName-split) *dstName ?dstName?...*
  * [vectorName variable](#vectorName-variable) *varName*
* [C LANGUAGE API](#C-LANGUAGE-API)
* [LIBRARY ROUTINES](#LIBRARY-ROUTINES)
* [C API EXAMPLE](#C-API-EXAMPLE)
* [INCOMPATIBILITIEA](#INCOMPATIBILITIES)
* [EXAMPLE](#EXAMPLE)
* [KEYWORDS](#KEYWORDS)
* [COPYRIGHT](#COPYRIGHT)

<a name="NAME"></a>
## NAME 

graph::vector - Vector data type for graph widgets

<a name="SYNOPSIS"></a>
## SYNOPSIS 

**[graph::vector configure](#graph::vector-configure)** *?option value? ...*

**[graph::vector create](#graph::vector-create)** *vectorName?(...)? ?option value?...*

**[graph::vector destroy](#graph::vector-destroy)** *vectorName ?vectorName...?*

**[graph::vector expr](#graph::vector-expr)** *expression*

**[graph::vector names](#graph::vector-names)** *?pattern...?*

**[graph::vector op](#graph::vector-op)** *operation vectorName ?arg?...*

<a name="DESCRIPTION"></a>
## DESCRIPTION 

The vector command creates a vector of floating point values. The vector's components can be manipulated in three ways: through a Tcl array variable, a Tcl command, or the C API.

A vector is simply an ordered set of numbers. The components of a vector are real numbers, indexed by counting numbers.

Vectors are common data structures for many applications. For example, a graph may use two vectors to represent the X-Y coordinates of the data plotted. The graph will automatically be redrawn when the vectors are updated or changed. By using vectors, you can separate data analysis from the graph widget. This makes it easier, for example, to add data transformations, such as splines. It's possible to plot the same data to in multiple graphs, where each graph presents a different view or scale of the data.

You could try to use Tcl's associative arrays as vectors. Tcl arrays are easy to use. You can access individual elements randomly by specifying the index, or the set the entire array by providing a list of index and value pairs for each element. The disadvantages of associative arrays as vectors lie in the fact they are implemented as hash tables.

- There's no implied ordering to the associative arrays. If you used vectors for plotting, you would want to insure the second component comes after the first, an so on. This isn't possible since arrays are actually hash tables. For example, you can't get a range of values between two indices. Nor can you sort an array.

- Arrays consume lots of memory when the number of elements becomes large (tens of thousands). This is because each element's index and value are stored as strings in the hash table.

- The C programming interface is unwieldy. Normally with vectors, you would like to view the Tcl array as you do a C array, as an array of floats or doubles. But with hash tables, you must convert both the index and value to and from decimal strings, just to access an element in the array. This makes it cumbersome to perform operations on the array as a whole.

The vector command tries to overcome these disadvantages while still retaining the ease of use of Tcl arrays. The vector command creates both a new Tcl command and associate array which are linked to the vector components. You can randomly access vector components though the elements of array. Not all indices are generated for the array, so printing the array (using the parray procedure) does not print out all the component values. You can use the Tcl command to access the array as a whole. You can copy, append, or sort vector using its command. If you need greater performance, or customized behavior, you can write your own C code to manage vectors.

<a name="SYNTAX"></a>
## SYNTAX

<a name="graph::vector-configure"></a>
**graph::vector configure** *?-flush bool -watchunset bool -oldcreate bool -maxsize int -novariable bool -nocommand bool?*

> The configure operation sets the default options used in creating vectors: these options are global to the interpreter. The -maxsize option, when non-zero, limits creation size. The -oldcreate enable the creation shortcut: vector vec1 vec2 .... See the create command for details on the others. By default, these are all disabled or zero.

<a name="graph::vector-create"></a>
**graph::vector create** *vectorName?(...)? ?option value?*

> The create operation creates a new vector vectorName. This creates both a Tcl command and array variable called vectorName. The name vectorName must be unique, so another Tcl command or array variable can not already exist in the current scope. You may access the components of the vector using the variable. If you change a value in the array, or unset an array element, the vector is updated to reflect the changes. When the variable vectorName is unset, the vector and its Tcl command are also destroyed.

> *vectorName*

> > This creates a new vector vectorName which initially has no components.

> *vectorName(size)*

> > This second form creates a new vector which will contain size number of components. The components will be indexed starting from zero (0). The default value for the components is 0.0.

> *vectorName(rows,columns)*

> > This form allows creation of a matrix with the specified columns and `rows*columns` elements. See the matrix section for more details.

> *vectorName(first:last)*

> > The last form creates a new vector of indexed first through last. First and last can be any integer value so long as first is less than last.

> The vector has optional switches that affect how the vector is created. They are as follows:

> **-variable** *varName*

> > Specifies the name of a Tcl variable to be mapped to the vector. If the variable already exists, it is first deleted, then recreated. If varName is the empty string, then no variable will be mapped. You can always map a variable back to the vector using the vector's variable operation.

> **-command** *cmdName*

> > Maps a Tcl command to the vector. The vector can be accessed using cmdName and one of the vector instance operations. A Tcl command by that name cannot already exist. If cmdName is the empty string, no command mapping will be made.

> **-watchunset** *boolean*

> > Indicates that the vector should automatically delete itself if the variable associated with the vector is unset. By default, the vector will not be deleted. This is different from previous releases. Set boolean to "true" to get the old behavior.

> **-flush** *boolean*

> > Indicates that the vector should automatically flush the cached variable elements which unsets all the elements of the Tcl array variable associated with the vector, freeing memory associated with the variable. This includes both the hash table and the hash keys. The down side is that this effectively flushes the caching of vector elements in the array. This means that the subsequent reads of the array will require a decimal to string conversion. By default, flushing is disabled.

> Vector names must start with a letter and consist of letters, digits, or underscores.

> > <code>
	# Error: must start with letter  
	graph::vector create 1abc  
</code>

> You can automatically generate vector names using the "#auto" vector name. The create operation will generate a unique vector name.

> > <code>
	set vec [graph::vector create #auto]  
	puts "$vec has [$vec length] components"  
</code>

> If successful, vector returns the name of the vector. It also creates a new Tcl command by the same name. You can use this command to invoke various operations that query or modify the vector. The general form is:

> **vectorName** *operation ?arg?...*

> Both, operation and its arguments determine the exact behavior of the command. The operations available for the graph are described in section [VECTOR OPERATIONS](#VECTOR-OPERATIONS).

<a name="graph::vector-destroy"></a>
**graph::vector destroy** *vectorName ?vectorName...?*

> Destroy given vectors.

<a name="graph::vector-expr"></a>
**graph::vector expr** *expression*

> All binary operators take vectors as operands (remember that numbers are treated as one-component vectors).The exact action of binary operators depends upon the length of the second operand. If the second operand has only one component, then each element of the first vector operand is computed by that value. For example, the expression "x * 2" multiples all elements of the vector x by 2. If the second operand has more than one component, both operands must be the same length. Each pair of corresponding elements are computed. So "x + y" adds the the first components of x and y together, the second, and so on.

> The valid operators are listed below, grouped in decreasing order of precedence:

> **- !**

> > Unary minus and logical NOT. The unary minus flips the sign of each component in the vector. The logical not operator returns a vector of whose values are 0.0 or 1.0. For each non-zero component 1.0 is returned, 0.0 otherwise.

> **^**

> > Exponentiation.

> **\* / %**

> > Multiply, divide, remainder.

> **+ -**

> > Add and subtract.

> **<< >>**

> > Left and right shift. Circularly shifts the values of the vector

> **< > <= >=**

> > Boolean less, greater, less than or equal, and greater than or equal. Each operator returns a vector of ones and zeros. If the condition is true, 1.0 is the component value, 0.0 otherwise.

> **== !=**

> > Boolean equal and not equal. Each operator returns a vector of ones and zeros. If the condition is true, 1.0 is the component value, 0.0 otherwise.

> **&&**

> > Logical AND. Produces a 1 result if both operands are non-zero, 0 otherwise.

> **||**

> > Logical OR. Produces a 0 result if both operands are zero, 1 otherwise.

> **x?y:z**

> > If-then-else, as in C.

> See the C manual for more details on the results produced by each operator. All of the binary operators group left-to-right within the same precedence level.

> Several mathematical functions are supported for vectors. Each of the following functions invokes the math library function of the same name; see the manual entries for the library functions for details on what they do. The operation is applied to all elements of the vector returning the results. All functions take a vector operand. If no vector operand is used in the call, the current vector is assumed. eg.

> <code>
        vector create aVec
        aVec seq 0 100
        aVec expr {2*abs(aVec)-1}
        aVec length 100
        aVec expr {2*row()}
        vector expr {2*row()} ; # ERROR!
</code>

> Standard mathematical functions:

> > **acos	cos	hypot	sinh**  
> > **asin	cosh	log	sqrt**  
> > **atan	exp	log10	tan**    
> > **ceil	floor	sin	tanh**  

> Additional functions are:

> > **abs**

> > > Returns the absolute value of each component.

> > **random**

> > > Returns a vector of non-negative values uniformly distributed between [0.0, 1.0) using drand48. The seed comes from the internal clock of the machine or may be set manual with the srandom function.

> > **round**

> > > Rounds each component of the vector.

> > **srandom**

> > > Initializes the random number generator using srand48. The high order 32-bits are set using the integral portion of the first vector component. All other components are ignored. The low order 16-bits are set to an arbitrary value.

> The following functions return a single value.

> > **adev**

> > > Returns the average deviation (defined as the sum of the absolute values of the differences between component and the mean, divided by the length of the vector).

> > **kurtosis**

> > > Returns the degree of peakedness (fourth moment) of the vector.

> > **length**

> > > Returns the number of components in the vector.

> > **max**

> > > Returns the vector's maximum value.

> > **mean**

> > > Returns the mean value of the vector.

> > **median**

> > > Returns the median of the vector.

> > **min**

> > > Returns the vector's minimum value.

> > **q1**

> > > Returns the first quartile of the vector.

> > **q3**

> > > Returns the third quartile of the vector.

> > **prod**

> > > Returns the product of the components.

> > **sdev**

> > > Returns the standard deviation (defined as the square root of the variance) of the vector.

> > **skew**

> > > Returns the skewness (or third moment) of the vector. This characterizes the degree of asymmetry of the vector about the mean.

> > **sum**

> > > Returns the sum of the components.

> > **var**

> > > Returns the variance of the vector. The sum of the squared differences between each component and the mean is computed. The variance is the sum divided by the length of the vector minus 1.

> This last set of functions returns a vector of the same length as the argument.

> > **invert**

> > > Returns vector with elements in reversed order.

> > **norm**

> > > Scales the values of the vector to lie in the range [0.0..1.0].

> > **row**

> > > Psuedo function to get the current row.

> > **sort**

> > > Returns the vector components sorted in ascending order.

> > **shift(nVec,N)**

> > > This is the only function taking a second arg. It provides a version of nvec shifted by N places. When N is a scalar or vector with only one element, shift fills vacant area with 0. Otherwise the second element of nVec is used for the fill value. One use for this is providing running totals.

<a name="graph::vector-names"></a>
**graph::vector names** *?pattern?*

> Return names of all defined vectors.

<a name="graph::vector-op"></a>
**graph::vector op** *operation vectorName ?arg?...*

> Invoke instance operation. Supported operations are defined in the next section. Op is the only way to invoke instance operation sub-commands when -command is defined as empty in a vector. It also allows writing vector code that is checkable by a syntax checkers. eg.

> <code>
    graph::vector create v1  
    v1 op append {1 2 3}  
    v1 op modify 1 2.1  
</code>

<a name="VECTOR-INDICES"></a>
## VECTOR INDICES

Vectors are indexed by integers. You can access the individual vector components via its array variable or Tcl command. The string representing the index can be an integer, a numeric expression, a range, or a special keyword.

The index must lie within the current range of the vector, otherwise an an error message is returned. Normally the indices of a vector are start from 0. But you can use the offset operation to change a vector's indices on-the-fly.

> <code>
puts $vectorName(0)  
vectorName offset -5  
puts $vectorName(-5)  
</code>

When matrix numcols is > 1, 2D indexes are supported using ROW,COL form.

> <code>
vectorName matrix numcols 3  
puts vectorName(0,2)  
</code>

You can also use numeric expressions as indices. The result of the expression must be an integer value.

> <code>
set n 21  
set vectorName($n+3) 50.2  
</code>

The following special non-numeric indices are available: min, max, end, and ++end.

> <code>
puts "min = $vectorName($min)"  
set vectorName(end) -1.2  
</code>

The indices min and max will return the minimum and maximum values of the vector. Also available are: prod, sum, and mean. The index end returns the value of the last component in the vector. he index end,0 returns the value of the last row in column 0 of the vector. The index ++end is used to append new value onto the vector. It automatically extends the vector by numcols and sets its value.

> <code>
 # Append an new component to the end  
set vectorName(++end) 3.2  
</code>

A range of indices can be indicated by a colon (:).

> <code>
 # Set the first six components to 1.0  
set vectorName(0:5) 1.0  
</code>

If no index is supplied the first or last component is assumed.

> <code>
 # Print the values of all the components  
puts $vectorName(:)  
</code>

<a name="VECTOR-OPERATIONS"></a>
## VECTOR OPERATIONS

You can also use the vector's Tcl command to query or modify it. The general form is

> **vectorName** *operation arg...*

Note this is equivalent to the form:

> **graph::vector op** *operation vectorName ?arg?...*

Both operation and its arguments determine the exact behavior of the command. The operations available for vectors are listed below.

<a name="vectorName-plus"></a>
**vectorName +** *item*

<a name="vectorName-minus"></a>
**vectorName -** *item*

<a name="vectorName-mult"></a>
**vectorName** `*` *item*

<a name="vectorName-div"></a>
**vectorName /** *item*

> Perform binary op and return result as a list.

<a name="vectorName-append"></a>
**vectorName append** *item ?item?...*

> Appends the component values from item to vectorName. Item can be either the name of a vector or a list of numeric values.

<a name="vectorName-binread"></a>
**vectorName binread** *channel ?length? ?switches?*

> Reads binary values from a Tcl channel. Values are either appended to the end of the vector or placed at a given index (using the -at option), overwriting existing values. Data is read until EOF is found on the channel or a specified number of values length are read (note that this is not necessarily the same as the number of bytes). The following switches are supported:

> **-swap**

> > Swap bytes and words. The default endian is the host machine.

> **-at** *index*

> > New values will start at vector index index. This will overwrite any current values.

> **-format** *format*

> > Specifies the format of the data. Format can be one of the following: "i1", "i2", "i4", "i8", "u1, "u2", "u4", "u8", "r4", "r8", or "r16". The number indicates the number of bytes required for each value. The letter indicates the type: "i" for signed, "u" for unsigned, "r" or real. The default format is "r16".

<a name="vectorName-binwrite"></a>
**vectorName binwrite** *channel ?length? ?-at index?*

> Like binread, but writes data.

<a name="vectorName-clear"></a>
**vectorName clear**

> Clears the element indices from the array variable associated with vectorName. This doesn't affect the components of the vector. By default, the number of entries in the Tcl array doesn't match the number of components in the vector. This is because its too expensive to maintain decimal strings for both the index and value for each component. Instead, the index and value are saved only when you read or write an element with a new index. This command removes the index and value strings from the array. This is useful when the vector is large.

<a name="vectorName-delete"></a>
**vectorName delete** *index ?index?...*

> Deletes the indexth component from the vector vectorName. Index is the index of the element to be deleted. This is the same as unsetting the array variable element index. The vector is compacted after all the indices have been deleted.

<a name="vectorName-dup"></a>
**vectorName dup** *destName*

> Copies vectorName to destName. DestName is the name of a destination vector. If a vector destName already exists, it is overwritten with the components of vectorName. Otherwise a new vector is created.

<a name="vectorName-expr"></a>
**vectorName expr** *expression*

> Computes the expression and resets the values of the vector accordingly. Both scalar and vector math operations are allowed. All values in expressions are either real numbers or names of vectors. All numbers are treated as one component vectors.

<a name="vectorName-index"></a>
**vectorName index** *index ?value?...*

> Get/set individual vector values. This provides element updating when -variable is set to empty.

<a name="vectorName-insert"></a>
**vectorName insert** *index item ?item?...*

> Inserts the component values from item to vectorName at index Item can be either the name of a vector or a list of numeric values.

<a name="vectorName-length"></a>
**vectorName length** *?newSize?*

> Queries or resets the number of components in vectorName. NewSize is a number specifying the new size of the vector. If newSize is smaller than the current size of vectorName, vectorName is truncated. If newSize is greater, the vector is extended and the new components are initialized to 0.0. If no newSize argument is present, the current length of the vector is returned.

<a name="vectorName-matrix"></a>
**vectorName matrix** *...*

> Matrix provides a 2D array view into 1D data. It provides indexing operations in ROW,COL form making it suitable for use with TkTable. Data storage remains unchanged: vectors are still just a single long array. For example, here are two ways to create a 3 column by 10 row matrix:

> <code>
    graph::vector create aVec(10,3)  
    graph::vector create bVec(30)  
    bVec matrix numcols 3  
    set aVec(0,0) 99  
    set bVec(29,2) -99  
    aVec append {5 6 7}; # aVec now has 11 rows.  
    aVec append 1 2;     # Now aVec has 13 rows!  
</code>

> Note that data is appended only in increments of numcols. Elements 0-2 make up the first row, 3-5 the second, etc. Elements will appear only in increments of the column size.

<a name="vectorName-matrix-copy"></a>
**vectorName matrix copy** *dstcolumn srccolumn ?srcVec?*

> Copy a column of element values to column dstcolumn from srccolumn. If vector srcVec is given, and not the same as vectorName, the columns numbers must be different. If the srcVec column is longer, vectorName will be extended. If shorter, remaining destination values are not overwritten.

<a name="vectorName-matrix-delete"></a>
**vectorName matrix delete** *column*

> Delete elements in a column. Note that numcols, which must be greater than 1, will be decremented.

<a name="vectorName-matrix-get"></a>
**vectorName matrix get** *column*

> Get the element in a column: this number must be less than numcols. Note that numcols must be non-zero.

<a name="vectorName-matrix-insert"></a>
**vectorName matrix insert** *column ?initvalue?*

> Insert a new column of elements at column (default 0). The new column is initialized with initvalue, or 0.0 if not specified. Note that numcols will be incremented.

<a name="vectorName-matrix-multiply"></a>
**vectorName matrix multiply** *srcVec ?dstVec?*

> Perform matrix multiplication using srcVec, placing results either in dstVec, or returned as a list. The numrows of srcVec must equal numcols in vectorName. One application for multiply is coordinate transformation.

<a name="vectorName-matrix-numcols"></a>
**vectorName matrix numcols** *?size?*

> Get or set the number of columns for a vectors data. Values >1 enable array variables to accept 2d matrix indexes. For example with a numcols of 10, $vec1(1,2) refers to the 13th element in the vector. A vectors size is also constrained to multiples of numcols, as is it's offset. By default, numcols is 1.

<a name="vectorName-matrix-numrows"></a>
**vectorName matrix numrows** *?size?*

> Get or set the length of rows in a columns for a vector. By default, this is just the vector length/numcols. Setting this value simply provides a convenient way to increase or decrease the vector size by multiples of numcols.

<a name="vectorName-matrix-set"></a>
**vectorName matrix set** *column ?valuelist?*

> Set value elements in a column: this number must be less than numcols. The valuelist is a list values. If this list is shorter than the column, it's last value is used for all remaining columns. The column gets set to the values of item, or 0.0 by default.

<a name="vectorName-matrix-shift"></a>
**vectorName matrix shift** *column amount ?startoffset?*

> Shifts the values of a column by integer inamount. A negative value shifts upward. The startoffset indicates where to start shifting from.

<a name="vectorName-matrix-sort"></a>
**vectorName matrix sort** *column ?-reverse?*

> Sort the vector by the given column.

<a name="vectorName-matrix-transpose"></a>
**vectorName matrix transpose**

> Transpose all columns with rows in matrix. Note that this is a no-op if numcols is 1. Otherwise, numcols will change to vectorLength/numcols.

<a name="vectorName-merge"></a>
**vectorName merge** *srcName ?srcName?...*

> Merges the named vectors into a single vector. The resulting vector is formed by merging the components of each source vector one index at a time.

<a name="vectorName-notify"></a>
**vectorName notify** *?keyword? ?script?*

> Queries or controls how vector clients are notified of changes to the vector. Also allows setting a notifier callback. The exact behavior is determined by keyword.

> **always**

> > Indicates that clients are to be notified immediately whenever the vector is updated.

> **never**

> > Indicates that no clients are to be notified.

> **whenidle**

> > Indicates that clients are to be notified at the next idle point whenever the vector is updated.

> **now**

> > If any client notifications is currently pending, they are notified immediately.

> **cancel**

> > Cancels pending notifications of clients using the vector.

> **pending**

> > Returns 1 if a client notification is pending, and 0 otherwise.

> **callback** *?script?*

> > Query or set a Tcl callback script that is evaluated when a vector is updated.

<a name="vectorName-populate"></a>
**vectorName populate** *destName ?density?*

> Creates a vector destName which is a superset of vectorName. DestName will include all the components of vectorName, in addition the interval between each of the original components will contain a density number of new components, whose values are evenly distributed between the original components values. This is useful for generating abscissas to be interpolated along a spline.

<a name="vectorName-range"></a>
**vectorName range** *firstIndex ?lastIndex?...*

> Returns a list of numeric values representing the vector components between two indices. Both firstIndex and lastIndex are indices representing the range of components to be returned. If lastIndex is less than firstIndex, the components are listed in reverse order.

<a name="vectorName-search"></a>
**vectorName search** *value ?value?*

> Searches for a value or range of values among the components of vectorName. If one value argument is given, a list of indices of the components which equal value is returned. If a second value is also provided, then the indices of all components which lie within the range of the two values are returned. If no components are found, then "" is returned.

<a name="vectorName-set"></a>
**vectorName set** *item*

> Resets the components of the vector to item. Item can be either a list of numeric expressions or another vector.

<a name="vectorName-seq"></a>
**vectorName seq** *start ?finish? ?step?*

> Generates a sequence of values starting with the value start. Finish indicates the terminating value of the sequence. The vector is automatically resized to contain just the sequence. If three arguments are present, step designates the interval.

> With only two arguments (no finish argument), the sequence will continue until the vector is filled. With one argument, the interval defaults to 1.0.

<a name="vectorName-sort"></a>
**vectorName sort** *?-reverse? ?argName?...*

> Sorts the vector vectorName in increasing order. If the -reverse flag is present, the vector is sorted in decreasing order. If other arguments argName are present, they are the names of vectors which will be rearranged in the same manner as vectorName. Each vector must be the same length as vectorName. You could use this to sort the x vector of a graph, while still retaining the same x,y coordinate pairs in a y vector.

<a name="vectorName-split"></a>
**vectorName split** *dstName ?dstName?...*

> Split the vector into a multiple vectors. The resulting N vectors each contain the mod-Nth element from source.

<a name="vectorName-variable"></a>
**vectorName variable** *varName*

> Maps a Tcl variable to the vector, creating another means for accessing the vector. The variable varName can't already exist. This overrides any current variable mapping the vector may have.

<a name="C-LANGUAGE-API"></a>
## C LANGUAGE API

You can create, modify, and destroy vectors from C code, using library routines. You need to include the header file blt.h. It contains the definition of the structure `Rbc_Vector`, which represents the vector. It appears below.

> <code>
typedef struct {  
double valueArr;  
int numValues;  
int arraySize;    
double min, max;  
} **Rbc_Vector**;  
</code>

The field valueArr points to memory holding the vector components. The components are stored in a double precision array, whose size size is represented by arraySize. NumValues is the length of vector. The size of the array is always equal to or larger than the length of the vector. Min and max are minimum and maximum component values.

<a name="LIBRARY-ROUTINES"></a>
## LIBRARY ROUTINES

The following routines are available from C to manage vectors. Vectors are identified by the vector name.

**Rbc_CreateVector**

> Synopsis:

> > `int Rbc_CreateVector(Tcl_Interp *interp; char *vectorName; int length; Rbc_Vector **vecPtrPtr);`

> Description:

> > Creates a new vector vectorName with a length of length. Rbc_CreateVector creates both a new Tcl command and array variable vectorName. Neither a command nor variable named vectorName can already exist. A pointer to the vector is placed into vecPtrPtr.

> Results:

> > Returns TCL_OK if the vector is successfully created. If length is negative, a Tcl variable or command vectorName already exists, or memory cannot be allocated for the vector, then TCL_ERROR is returned and interp->result will contain an error message.

**Rbc_VectorFree**

> Synopsis:

> > `int Rbc_VectorFree(Rbc_Vector *vecPtr);`

> Description:

> > Removes the vector pointed to by vecPtr. VecPtr is a pointer to a vector, typically set by Rbc_GetVector or Rbc_CreateVector. Both the Tcl command and array variable of the vector are destroyed. All clients of the vector will be notified immediately that the vector has been destroyed.

> Results:

> > Returns TCL_OK if the vector is successfully deleted. If vectorName is not the name a vector, then TCL_ERROR is returned.

**Rbc_GetVector**

> Synopsis:

> > `int Rbc_GetVector(Tcl_Interp *interp; char *vectorName; Rbc_Vector **vecPtrPtr);`

> Description:

> > Retrieves the vector vectorName. VecName is the name of a vector which must already exist. VecPtrPtr will point be set to the address of the vector.

> Results:

> > Returns TCL_OK if the vector is successfully retrieved. If vectorName is not the name of a vector, then TCL_ERROR is returned and interp->result will contain an error message.

**Rbc_ResetVector**

> Synopsis:

> > `int Rbc_ResetVector(Rbc_Vector *vecPtr; double *dataArr; int *numValues; int *arraySize; Tcl_FreeProc *freeProc);` 

> Description:

> > Resets the components of the vector pointed to by vecPtr. Calling Rbc_ResetVector will trigger the vector to dispatch notifications to its clients. DataArr is the array of doubles which represents the vector data. NumValues is the number of elements in the array. ArraySize is the actual size of the array (the array may be bigger than the number of values stored in it). FreeProc indicates how the storage for the vector component array (dataArr) was allocated. It is used to determine how to reallocate memory when the vector is resized or destroyed. It must be TCL_DYNAMIC, TCL_STATIC, TCL_VOLATILE, or a pointer to a function to free the memory allocated for the vector array. If freeProc is TCL_VOLATILE, it indicates that dataArr must be copied and saved. If freeProc is TCL_DYNAMIC, it indicates that dataArr was dynamically allocated and that Tcl should free dataArr if necessary. Static indicates that nothing should be done to release storage for dataArr.

> Results:

> > Returns TCL_OK if the vector is successfully resized. If newSize is negative, a vector vectorName does not exist, or memory cannot be allocated for the vector, then TCL_ERROR is returned and interp->result will contain an error message.

**Rbc_ResizeVector**

> Synopsis:

> > `int Rbc_ResizeVector(Rbc_Vector *vecPtr; int newSize);`

> Description:

> > Resets the length of the vector pointed to by vecPtr to newSize. If newSize is smaller than the current size of the vector, it is truncated. If newSize is greater, the vector is extended and the new components are initialized to 0.0. Calling Rbc_ResetVector will trigger the vector to dispatch notifications.

> Results:

> > Returns TCL_OK if the vector is successfully resized. If newSize is negative or memory can not be allocated for the vector, then TCL_ERROR is returned and interp->result will contain an error message.

**Rbc_VectorExists**

> Synopsis:

> > `int Rbc_VectorExists(Tcl_Interp *interp; char *vectorName);` 

> Description:

> > Indicates if a vector named vectorName exists in interp.

> Results:

> > Returns 1 if a vector vectorName exists and 0 otherwise.

If your application needs to be notified when a vector changes, it can allocate a unique client identifier for itself. Using this identifier, you can then register a call-back to be made whenever the vector is updated or destroyed. By default, the call-backs are made at the next idle point. This can be changed to occur at the time the vector is modified. An application can allocate more than one identifier for any vector. When the client application is done with the vector, it should free the identifier.

The call-back routine must of the following type.

> `typedef void (Rbc_VectorChangedProc) (Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify);`

ClientData is passed to this routine whenever it is called. You can use this to pass information to the call-back. The notify argument indicates whether the vector has been updated of destroyed. It is an enumerated type.

> `typedef enum { RBC_VECTOR_NOTIFY_UPDATE=1, RBC_VECTOR_NOTIFY_DESTROY=2 } Rbc_VectorNotify;`

**Rbc_AllocVectorId**

> Synopsis:

> > `Rbc_VectorId Rbc_AllocVectorId(Tcl_Interp *interp; char *vectorName);` 

> Description:

> > Allocates an client identifier for with the vector vectorName. This identifier can be used to specify a call-back which is triggered when the vector is updated or destroyed.

> Results:

> > Returns a client identifier if successful. If vectorName is not the name of a vector, then NULL is returned and interp->result will contain an error message.

**Rbc_GetVectorById**

> Synopsis:

> > `int Rbc_GetVectorById(Tcl_Interp *interp; Rbc_VectorId clientId; Rbc_Vector **vecPtrPtr);` 

> Description:

> > Retrieves the vector used by clientId. ClientId is a valid vector client identifier allocated by Rbc_AllocVectorId. VecPtrPtr will point be set to the address of the vector.

> Results:

> > Returns TCL_OK if the vector is successfully retrieved.

**Rbc_SetVectorChangedProc**

> Synopsis:

> > `void Rbc_SetVectorChangedProc(Rbc_VectorId clientId; Rbc_VectorChangedProc *proc; ClientData *clientData);` 

> Description:

> > Specifies a call-back routine to be called whenever the vector associated with clientId is updated or deleted. Proc is a pointer to call-back routine and must be of the type Rbc_VectorChangedProc. ClientData is a one-word value to be passed to the routine when it is invoked. If proc is NULL, then the client is not notified.

> Results:

> > The designated call-back procedure will be invoked when the vector is updated or destroyed.

**Rbc_FreeVectorId**

> Synopsis:

> > `void Rbc_FreeVectorId(Rbc_VectorId clientId);` 

> Description:

> > Frees the client identifier. Memory allocated for the identifier is released. The client will no longer be notified when the vector is modified.

> Results:

> > The designated call-back procedure will be no longer be invoked when the vector is updated or destroyed.

**Rbc_NameOfVectorId**

> Synopsis:

> > `char *Rbc_NameOfVectorId(Rbc_VectorId clientId);` 

> Description:

> > Retrieves the name of the vector associated with the client identifier clientId.

> Results:

> > Returns the name of the vector associated with clientId. If clientId is not an identifier or the vector has been destroyed, NULL is returned.

<a name="C-API-EXAMPLE"></a>
## C API EXAMPLE

The following example opens a file of binary data and stores it in an array of doubles. The array size is computed from the size of the file. If the vector "data" exists, calling Rbc_VectorExists, Rbc_GetVector is called to get the pointer to the vector. Otherwise the routine Rbc_CreateVector is called to create a new vector and returns a pointer to it. Just like the Tcl interface, both a new Tcl command and array variable are created when a new vector is created. It doesn't make any difference what the initial size of the vector is since it will be reset shortly. The vector is updated when lt_ResetVector is called. Rbc_ResetVector makes the changes visible to the Tcl interface and other vector clients (such as a graph widget).

> <code>
  #include "tcl.h"  
  #include "blt.h"  
  Rbc_Vector *vecPtr;  
  double *newArr;  
  FILE *f;  
  struct stat statBuf;  
  int numBytes, numValues;  
  f = fopen("binary.dat", "r");  
  fstat(fileno(f), &statBuf);  
  numBytes = (int)statBuf.st_size; /* Allocate an array big enough to hold all the data */  
  newArr = (double *)malloc(numBytes);  
  numValues = numBytes / sizeof(double);  
  fread((void *)newArr, numValues, sizeof(double), f);  
  fclose(f);  
  if (Rbc_VectorExists(interp, "data")) {  
    if (Rbc_GetVector(interp, "data", &vecPtr) != TCL_OK) {  
      return TCL_ERROR;  
    }  
  } else {  
    if (Rbc_CreateVector(interp, "data", 0, &vecPtr) != TCL_OK) {  
      return TCL_ERROR;  
    }  
  }  
  /* * Reset the vector. Clients will be notified when Tk is idle.  
   * TCL_DYNAMIC tells the vector to free the memory allocated  
   * if it needs to reallocate or destroy the vector.  
   */  
  if (Rbc_ResetVector(vecPtr, newArr, numValues, numValues, TCL_DYNAMIC) != TCL_OK) {  
    return TCL_ERROR;  
  }  
</code>

<a name="INCOMPATIBILITIES"></a>
## INCOMPATIBILITIES

In previous versions, if the array variable isn't global (i.e. local to a Tcl procedure), the vector is automatically destroyed when the procedure returns.

> <code>
proc doit {} {  
    # Temporary vector x  
    vector x(10)  
    set x(9) 2.0  
      ...  
}  
</code>

This has changed. Variables are not automatically destroyed when their variable is unset. You can restore the old behavior by setting the "-watchunset" switch.

<a name="EXAMPLE"></a>
## EXAMPLE

You create vectors using the vector command and its create operation.

> <code>
 # Create a new vector.  
graph::vector create y(50)  
</code>

This creates a new vector named y. It has fifty components, by default, initialized to 0.0. In addition, both a Tcl command and array variable, both named y, are created. You can use either the command or variable to query or modify components of the vector.

> <code>
 # Set the first value.  
set y(0) 9.25  
puts "y has [y length] components"  
</code>

The array y can be used to read or set individual components of the vector. Vector components are indexed from zero. The array index must be a number less than the number of components. For example, it's an error if you try to set the 51st element of y.

> <code>
 # This is an error. The vector only has 50 components.  
set y(50) 0.02  
</code>

You can also specify a range of indices using a colon (:) to separate the first and last indices of the range.

> <code>
 # Set the first six components of y  
set y(0:5) 25.2  
</code>

If you don't include an index, then it will default to the first and/or last component of the vector.

> <code>
 # Print out all the components of y  
puts "y = $y(:)"  
</code>

There are special non-numeric indices. The index end, specifies the last component of the vector. It's an error to use this index if the vector is empty (length is zero). The index ++end can be used to extend the vector by one component and initialize it to a specific value. You can't read from the array using this index, though.

> <code>
 # Extend the vector by one component.  
set y(++end) 0.02  
</code>

The other special indices are min and max. They return the current smallest and largest components of the vector.

> <code>
 # Print the bounds of the vector  
puts "min=$y(min) max=$y(max)"  
</code>

To delete components from a vector, simply unset the corresponding array element. In the following example, the first component of y is deleted. All the remaining components of y will be moved down by one index as the length of the vector is reduced by one.

> <code>
 # Delete the first component  
unset y(0)  
puts "new first element is $y(0)"  
</code>

The vector's Tcl command can also be used to query or set the vector.

> <code>
 # Create and set the components of a new vector  
graph::vector create x  
x set { 0.02 0.04 0.06 0.08 0.10 0.12 0.14 0.16 0.18 0.20 }  
</code>

Here we've created a vector x without a initial length specification. In this case, the length is zero. The set operation resets the vector, extending it and setting values for each new component.

There are several operations for vectors. The range operation lists the components of a vector between two indices.

> <code>
 # List the components  
puts "x = [x range 0 end]"  
</code>

You can search for a particular value using the search operation. It returns a list of indices of the components with the same value. If no component has the same value, it returns "".

> <code>
 # Find the index of the biggest component  
set indices [x search $x(max)]  
</code>

Other operations copy, append, or sort vectors. You can append vectors or new values onto an existing vector with the append operation.

> <code>
 # Append assorted vectors and values to x  
x append x2 x3 { 2.3 4.5 } x4  
</code>

The sort operation sorts the vector. If any additional vectors are specified, they are rearranged in the same order as the vector. For example, you could use it to sort data points represented by x and y vectors.

> <code>
 # Sort the data points  
x sort y  
</code>

The vector x is sorted while the components of y are rearranged so that the original x,y coordinate pairs are retained.

The expr operation lets you perform arithmetic on vectors. The result is stored in the vector.

> <code>
 # Add the two vectors and a scalar  
x expr { x + y }  
x expr { x * 2 }  
</code>

When a vector is modified, resized, or deleted, it may trigger call-backs to notify the clients of the vector. For example, when a vector used in the graph widget is updated, the vector automatically notifies the widget that it has changed. The graph can then redrawn itself at the next idle point. By default, the notification occurs when Tk is next idle. This way you can modify the vector many times without incurring the penalty of the graph redrawing itself for each change. You can change this behavior using the notify operation.

> <code>
 # Make vector x notify after every change  
x notify always  
	...  
 # Never notify  
x notify never  
	...  
 # Force notification now  
x notify now  
 # Set Tcl callback for update of Tktable widget .t.  
x notify callback {.t conf -padx [.t cget -padx]; .t reread}  
</code>

To delete a vector, use the vector delete command. Both the vector and its corresponding Tcl command are destroyed.

> <code>
 # Remove vector x  
graph::vector destroy x  
</code>

The pseudo vector last can be used at the end of an expression to implement running totals. During execution it resolves to the result from the previous vector element evaluation.

> <code>
graph::vector create A(10)  
graph::vector create B(10)  
graph::vector create S(10)  
graph::vector create T(10)  
graph::S expr A+B  
graph::T expr S+last; # Running total  
</code>

<a name="KEYWORDS"></a>
## KEYWORDS

vector, graph, widget

<a name="COPYRIGHT"></a>
## COPYRIGHT

&copy; 1995-1997 Roger E. Critchlow Jr.

&copy; 2001 George A. Howlett.

&copy; 2018 René Zaumseil <r.zaumseil@freenet.de>


