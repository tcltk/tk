---
CommandName: photo
title: photo
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - image(n)
Keywords:
 - photo
 - image
 - color
Copyright:
 - Copyright (c) 1994 The Australian National University
 - Copyright (c) 1994-1997 Sun Microsystems, Inc.
---

# Name

photo - Full-color images

# Synopsis

::: {.synopsis} :::
[image]{.cmd} [create]{.sub} [photo]{.lit} [name]{.optarg} [options]{.optarg}

[imageName]{.ins} [blank]{.sub}
[imageName]{.ins} [cget]{.sub} [option]{.arg}
[imageName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
[imageName]{.ins} [copy]{.sub} [sourceImage]{.arg} [option value(s)]{.optdot}
[imageName]{.ins} [data]{.sub} [option value(s)]{.optdot}
[imageName]{.ins} [get]{.sub} [x]{.arg} [y]{.arg} [option]{.optarg}
[imageName]{.ins} [put]{.sub} [data]{.arg} [option value(s)]{.optdot}
[imageName]{.ins} [read]{.sub} [filename]{.arg} [option value(s)]{.optdot}
[imageName]{.ins} [redither]{.sub}
[imageName]{.ins} [transparency]{.sub} [subcommand]{.arg} [arg]{.optdot}
[imageName]{.ins} [write]{.sub} [filename]{.arg} [option value(s)]{.optdot}
:::

# Description

A photo is an image whose pixels can display any color with a varying degree of transparency (the alpha channel). A photo image is stored internally in full color (32 bits per pixel), and is displayed using dithering if necessary.  Image data for a photo image can be obtained from a file or a string, or it can be supplied from C code through a procedural interface.  At present, only [PNG,]{version="8.6"} GIF, PPM/PGM, [and (read-only) SVG]{version="9.0"} formats are supported, but an interface exists to allow additional image file formats to be added easily.  A photo image is (semi)transparent if the image data it was obtained from had transparency information. In regions where no image data has been supplied, it is fully transparent. Transparency may also be modified with the **transparency set** subcommand.

# Creating photos

Like all images, photos are created using the [image create][image] command. Photos support the following *options*:

[-data]{.lit} [string]{.arg}
: Specifies the contents of the image as a string. [The string should contain data in the default list-of-lists form,]{version="9.0"} binary data or, for some formats, base64-encoded data (this is currently guaranteed to be supported for PNG and GIF images). The format of the string must be one of those for which there is an image file format handler that will accept string data.  If both the **-data** and **-file** options are specified, the **-file** option takes precedence.

[-format]{.lit} [formatSpec]{.arg}
: Specifies a format handler for the data given with the **-data** or **-file** option. In its simplest form, *formatSpec* is just the name of the format handler for the data. Optionally, additional arguments can be passed to the format handler. In that case, *formatSpec* is a list starting with the format handler name as the first list element and followed by amy number of *option value* pairs, using the options supported by the format handler (see section [Format suboptions] for details).

[-file]{.lit} [name]{.arg}
: *name* gives the name of a file that is to be read to supply data for the photo image.  The file format must be one of those for which there is an image file format handler that can read data.

[-gamma]{.lit} [value]{.arg}
: Specifies that the colors allocated for displaying this image in a window should be corrected for a non-linear display with the specified gamma exponent value.  (The intensity produced by most CRT displays is a power function of the input value, to a good approximation; gamma is the exponent and is typically around 2). The value specified must be greater than zero.  The default value is one (no correction).  In general, values greater than one will make the image lighter, and values less than one will make it darker.

[-height]{.lit} [number]{.arg}
: Specifies the height of the image, in pixels.  This option is useful primarily in situations where the user wishes to build up the contents of the image piece by piece.  A value of zero (the default) allows the image to expand or shrink vertically to fit the data stored in it. 

[-metadata]{.lit} [metadata]{.arg}
: [Set the metadata dictionary of the image. Additional keys may be set within the metadata dictionary of the image, if image data is processed due to a **-file** or **-data** options and the driver outputs any metadata keys. See section [Metadata dictionary] below.]{version="9.0"}

[-palette]{.lit} [palette-spec]{.arg}
: Specifies the resolution of the color cube to be allocated for displaying this image, and thus the number of colors used from the colormaps of the windows where it is displayed.  The *palette-spec* string may be either a single decimal number, specifying the number of shades of gray to use, or three decimal numbers separated by slashes (/), specifying the number of shades of red, green and blue to use, respectively.  If the first form (a single number) is used, the image will be displayed in monochrome (i.e., grayscale).

[-width]{.lit} [number]{.arg}
: Specifies the width of the image, in pixels.    This option is useful primarily in situations where the user wishes to build up the contents of the image piece by piece.  A value of zero (the default) allows the image to expand or shrink horizontally to fit the data stored in it.


# Image command

When a photo image is created, Tk also creates a new command whose name is the same as the image. This command may be used to invoke various operations on the image. It has the following general form:

```
imageName option ?arg ...?
```

*Option* and the *arg*s determine the exact behavior of the command.

Those options that write data to the image generally expand the size of the image, if necessary, to accommodate the data written to the image, unless the user has specified non-zero values for the **-width** and/or **-height** configuration options, in which case the width and/or height, respectively, of the image will not be changed.

The following commands are possible for photo images:

[imageName]{.ins} [blank]{.sub}
: Blank the image; that is, set the entire image to have no data, so it will be displayed as transparent, and the background of whatever window it is displayed in will show through. The metadata dict of the image is not changed.

[imageName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the [image create][image] **photo** command.

[imageName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options for the image. If no *option* is specified, returns a list describing all of the available options for *imageName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the [image create][image] **photo** command. [Note that setting the **-metadata** option without any other option will not invoke the image format driver to recreate the bitmap.]{version="9.0"}

[imageName]{.ins} [copy]{.sub} [sourceImage]{.arg} [option value(s)]{.optdot}
: Copies a region from the image called *sourceImage* (which must be a photo image) to the image called *imageName*, possibly with pixel zooming and/or subsampling.  If no options are specified, this command copies the whole of *sourceImage* into *imageName*, starting at coordinates (0,0) in *imageName*.  The following options may be specified:

    [-from]{.lit} [x1]{.arg} [y1]{.arg} [x2]{.arg} [y2]{.arg}
    : Specifies a rectangular sub-region of the source image to be copied. (*x1,y1*) and (*x2,y2*) specify diagonally opposite corners of the rectangle.  If *x2* and *y2* are not specified, the default value is the bottom-right corner of the source image.  The pixels copied will include the left and top edges of the specified rectangle but not the bottom or right edges.  If the **-from** option is not given, the default is the whole source image.

    **-to** *x1 y1 x2 y2*
    : Specifies a rectangular sub-region of the destination image to be affected.  (*x1,y1*) and (*x2,y2*) specify diagonally opposite corners of the rectangle.  If *x2* and *y2* are not specified, the default value is (*x1,y1*) plus the size of the source region (after subsampling and zooming, if specified).  If *x2* and *y2* are specified, the source region will be replicated if necessary to fill the destination region in a tiled fashion.

    **-shrink**
    : Specifies that the size of the destination image should be reduced, if necessary, so that the region being copied into is at the bottom-right corner of the image.  This option will not affect the width or height of the image if the user has specified a non-zero value for the **-width** or **-height** configuration option, respectively.

    **-zoom** *x y*
    : Specifies that the source region should be magnified by a factor of *x* in the X direction and *y* in the Y direction.  If *y* is not given, the default value is the same as *x*.  With this option, each pixel in the source image will be expanded into a block of *x* x *y* pixels in the destination image, all the same color.  *x* and *y* must be greater than 0.

    **-subsample** *x y*
    : Specifies that the source image should be reduced in size by using only every *x*th pixel in the X direction and *y*th pixel in the Y direction.  Negative values will cause the image to be flipped about the Y or X axes, respectively.  If *y* is not given, the default value is the same as *x*.

    **-compositingrule** *rule*
    : Specifies how transparent pixels in the source image are combined with the destination image.  When a compositing rule of *overlay* is set, the old contents of the destination image are visible, as if the source image were printed on a piece of transparent film and placed over the top of the destination.  When a compositing rule of *set* is set, the old contents of the destination image are discarded and the source image is used as-is.  The default compositing rule is *overlay*.


*imageName* **data** ?*option value(s) ...*?
: Returns image data in the form of a string. [The format of the string depends on the format handler. By default, a human readable format as a list of lists of pixel data is used, other formats can be chosen with the **-format** option. See [Image formats] below for details.]{version="9.0"} The following options may be specified:

    [-background]{.lit} [color]{.arg}
    : If the color is specified, the data will not contain any transparency information. In all transparent pixels the color will be replaced by the specified color.

    **-format** *formatSpec*
    : Specifies the image file format handler to use. In its simplest form, *formatSpec* is just the name of the format handler. Optionally, additional arguments can be passed to the format handler. In that case, *formatSpec* is a list starting with the format handler name as the first list element and followed by any number of *option value* pairs, using the options supported by the format handler (see section [Format suboptions] for details). Specifically, this subcommand searches for the first handler whose name matches an initial substring of the format handler name and which has the capability to write a string containing this image data. [If this option is not given, this subcommand uses the default format that consists of a list (one element per row) of lists (one element per pixel/column) of colors in "**#***rrggbb*" format (see [Image formats] below).]{version="9.0"}

    **-from** *x1 y1 x2 y2*
    : Specifies a rectangular region of *imageName* to be returned. If only *x1* and *y1* are specified, the region extends from *(x1,y1)* to the bottom-right corner of *imageName*.  If all four coordinates are given, they specify diagonally opposite corners of the rectangular region, including x1,y1 and excluding x2,y2.  The default, if this option is not given, is the whole image.

    **-grayscale**
    : If this options is specified, the data will not contain color information. All pixel data will be transformed into grayscale. 

    [**-metadata** *metadata*]{version="9.0"}
    : [Image format handler may use metadata to be included in the returned data string. The specified *metadata* is passed to the driver for inclusion in the data. If no **-metadata** option is given, the current metadata of the image is used.]{version="9.0"}


*imageName* **get** *x y* ?**-withalpha**?
: Returns the color of the pixel at coordinates (*x*,*y*) in the image as a list of three integers between 0 and 255, representing the red, green and blue components respectively. If the **-withalpha** option is specified, the returned list will have a fourth element representing the alpha value of the pixel as an integer between 0 and 255.

[imageName]{.ins} [put]{.sub} [data]{.arg} [option value(s)]{.optdot}
: Sets pixels in * imageName* to the data specified in *data*. [This command searches the list of image file format handlers for a handler that can interpret the data in *data*, and then reads the image encoded within into *imageName* (the destination image). See [Image formats] below for details on formats for image data.]{version="9.0"} The following options may be specified:

    [-format]{.lit} [formatSpec]{.arg}
    : Specifies the format of the image data in *data*. In its simplest form, *formatSpec* is just the name of the format handler of the image data. Optionally, additional arguments can be passed to the format handler. In that case, *formatSpec* is a list starting with the format handler name as the first list element and followed by any number of *option value* pairs, using the options supported by the format handler (see section [Format suboptions] for details). Specifically, only image file format handlers whose names begin with the specified format handler name will be used while searching for an image data format handler to read the data. 

    [**-metadata** *metadata*]{version="9.0"}
    : [A specified *metadata* is passed to the image format driver when interpreting the data. Note that the current metadata of the image is not passed to the format driver and is not changed by the command.]{version="9.0"}

    **-to** *x1 y1* ?*x2 y2*?
    : Specifies the coordinates of the top-left corner (*x1*,*y1*) of the region of *imageName* into which the image data will be copied.  The default position is (0,0).  If *x2*,*y2* is given and *data* is not large enough to cover the rectangle specified by this option, the image data extracted will be tiled so it covers the entire destination rectangle. If the region specified with this option is smaller than the supplied *data*, the exceeding data is silently discarded. Note that if *data* specifies a single color value, then a region extending to the bottom-right corner represented by (*x2*,*y2*) will be filled with that color.


*imageName* [read] *filename* ?*option value(s) ...*?
: Reads image data from the file named *filename* into the image. This command first searches the list of image file format handlers for a handler that can interpret the data in *filename*, and then reads the image in *filename* into *imageName* (the destination image).  The following options may be specified:

    [-format]{.lit} [formatSpec]{.arg}
    : Specifies the format of the image data in *filename*. In its simplest form, *formatSpec* is just the name of the format handler for the image data. Optionally, additional arguments can be passed to the format handler. In that case, *formatSpec* is a list starting with the format handler name as the first list element and followed by any number of *option value* pairs, using the options supported by the format handler (see section [Format suboptions] for details). Specifically, only image file format handlers that begin with the  specified handler name will be used while searching for an image data format handler to read the data.

    **-from** *x1 y1 x2 y2*
    : Specifies a rectangular sub-region of the image file data to be copied to the destination image.  If only *x1* and *y1* are specified, the region extends from (*x1,y1*) to the bottom-right corner of the image in the image file.  If all four coordinates are specified, they specify diagonally opposite corners or the region. The default, if this option is not specified, is the whole of the image in the image file. 

    [**-metadata** *metadata*]{version="9.0"}
    : [A specified *metadata* is passed to the image format driver when interpreting the data. Note that the current metadata of the image is not passed to the format driver and is not changed by the command.]{version="9.0"}

    **-shrink**
    : If this option, the size of *imageName* will be reduced, if necessary, so that the region into which the image file data are read is at the bottom-right corner of the *imageName*.  This option will not affect the width or height of the image if the user has specified a non-zero value for the **-width** or **-height** configuration option, respectively.

    **-to** *x y*
    : Specifies the coordinates of the top-left corner of the region of *imageName* into which data from *filename* are to be read. The default is (0,0).


*imageName* **redither**
: The dithering algorithm used in displaying photo images propagates quantization errors from one pixel to its neighbors. If the image data for *imageName* is supplied in pieces, the dithered image may not be exactly correct.  Normally the difference is not noticeable, but if it is a problem, this command can be used to recalculate the dithered image in each window where the image is displayed.

[imageName]{.ins} [transparency]{.sub} [subcommand]{.arg} [arg]{.optdot}
: Allows examination and manipulation of the transparency information in the photo image.  Several subcommands are available:

    [imageName]{.ins} [transparency]{.sub} [get]{.lit} [x]{.arg} [y]{.arg} [-alpha]{.optlit}
    : [Returns true if the pixel at (*x*,*y*) is fully transparent, false otherwise.  If the option **-alpha** is passed, returns the alpha value of the pixel instead, as an integer in the range 0 to 255.]{version="9.0"}

    *imageName* **transparency set** *x y newVal* ?**-alpha**?
    : [Change the transparency of the pixel at (*x*,*y*) to *newVal.* If no additional option is passed, *newVal* is interpreted as a boolean and the pixel is made fully transparent if that value is true, fully opaque otherwise.  If the **-alpha** option is passed, *newVal* is interpreted as an integral alpha value for the pixel, which must be in the range 0 to 255.]{version="9.0"}


*imageName* **write** *filename* ?*option value(s) ...*?
: Writes image data from *imageName* to a file named *filename*. The following options may be specified:

    [-background]{.lit} [color]{.arg}
    : If the color is specified, the data will not contain any transparency information. In all transparent pixels the color will be replaced by the specified color.

    **-format** *formatSpec*
    : Specifies the image file format handler to be used to write the data to the file. In its simplest form, *formatSpec* is just the name of the format handler for the data. Optionally, additional arguments can be passed to the format handler. In that case, *formatSpec* is a list starting with the format handler name as the first list element and followed by any number of *option value* pairs, using the options supported by the format handler (see section [Format suboptions] for details). Specifically, this subcommand searches for the first handler whose name matches an initial substring of the handler name and which has the capability to write an image file. If this option is not given, the format is guessed from the file extension. If that cannot be determined, this subcommand uses the first handler that has the capability to write an image file.

    **-from** *x1 y1 x2 y2*
    : Specifies a rectangular region of *imageName* to be written to the image file.  If only *x1* and *y1* are specified, the region extends from *(x1,y1)* to the bottom-right corner of *imageName*.  If all four coordinates are given, they specify diagonally opposite corners of the rectangular region.  The default, if this option is not given, is the whole image.

    **-grayscale**
    : If this options is specified, the data will not contain color information. All pixel data will be transformed into grayscale. 

    [**-metadata** *metadata*]{version="9.0"}
    : [Image format handler may use metadata to be included in the written file. The specified *metadata* is passed to the driver for inclusion in the file. If no **-metadata** option is given, the current metadata of the image is used.]{version="9.0"}



# Image formats

The photo image code is structured to allow handlers for additional image file formats to be added easily.  The photo image code maintains a list of these handlers.  Handlers are added to the list by registering them with a call to [Tk\_CreatePhotoImageFormat][CrtPhImgFmt].  The standard Tk distribution comes with handlers for PPM/PGM, PNG, GIF and (read-only) SVG formats, [as well as the **default** handler to encode/decode image data in a human readable form.]{version="9.0"} These handlers are automatically registered on initialization.

When reading an image file or processing string data specified with the **-data** configuration option, the photo image code invokes each handler in turn until one is found that claims to be able to read the data in the file or string.  Usually this will find the correct handler, but if it does not, the user may give a format name with the **-format** option to specify which handler to use.  In this case, the photo image code will try those handlers whose names begin with the string specified for the **-format** option (the comparison is case-insensitive).  For example, if the user specifies **-format gif**, then a handler named GIF87 or GIF89 may be invoked, but a handler named JPEG may not (assuming that such handlers had been registered).

When writing image data to a file, the processing of the **-format** option is slightly different: the value given for the **-format** option must begin with the complete name of the requested handler, and may be followed additional information following that, which the handler can use, for example, to specify which variant to use of the formats supported by the handler (the value is then a list). Note that not all image handlers may support writing transparency data to a file, even where the target image format does.

## The default image handler

The **default** image handler cannot be used to read or write data from/to a file. Its sole purpose is to encode and decode image data in string form in a clear text, human readable, form. The *imageName* **data** subcommand uses this handler when no other format is specified. When reading image data from a string with *imageName* **put** or the **-data** option, the default handler is treated as the other handlers.

Image data in the **default** string format is a (top-to-bottom) list of scan-lines, with each scan-line being a (left-to-right) list of pixel data. Every scan-line has the same length. The color and, optionally, alpha value of each pixel is specified in any of the forms described in the [Color formats] section below. 

## Format suboptions

[Image formats may support sub-options, which are specified using additional words in the value to the **-format** option. These suboptions can affect how image data is read or written to file or string. The nature and values of these options is up to the format handler. The built-in handlers support these suboptions:]{version="8.6"}

[default]{.cmd version="9.0"} [-colorformat]{.sub} [formatType]{.arg}
: [The option is allowed when writing image data to a string with *imageName* **data**. Specifies the format to use for the color string of each pixel. *formatType* may be one of: **rgb** to encode pixel data in the form **#***RRGGBB*, **rgba** to encode pixel data in the form **#***RRGGBBAA* or [list] to encode pixel data as a list with four elements. See [Color formats] below for details. The default is **rgb**.]{version="9.0"} 

[gif]{.cmd version="8.6"} [-index]{.sub} [indexValue]{.arg}
: [The option has effect when reading image data from a file. When parsing a multi-part GIF image, Tk normally only accesses the first image. By giving the **-index** sub-option, the *indexValue*'th value may be used instead. The *indexValue* must be an integer from 0 up to the number of image parts in the GIF data.]{version="8.6"}

[png]{.cmd version="8.6"} [-alpha]{.sub} [alphaValue]{.arg}
: [The option has effect when reading image data from a file. Specifies an additional alpha filtering for the overall image, which allows the background on which the image is displayed to show through.  This usually also has the effect of desaturating the image.  The *alphaValue* must be between 0.0 and 1.0.]{version="8.6"}

[svg]{.cmd version="8.6"} [-dpi]{.sub} [dpiValue]{.arg} [-scale]{.lit} [scaleValue]{.arg} [-scaletowidth]{.lit} [width]{.arg} [-scaletoheight]{.lit} [height]{.arg}
: [*dpiValue* is used in conversion between given coordinates and screen resolution. The value must be greater than 0 and the default value is 96.]{version="8.6"}


*scaleValue* is used to scale the resulting image. The value must be greater than 0 and the default value is 1. *width* and *height* are the width or height that the image will be adjusted to. Only one parameter among **-scale**, **-scaletowidth** and **-scaletoheight** can be given at a time and the aspect ratio of the original image is always preserved. The **svg** format supports a wide range of SVG features, but the full SVG standard is not available, for instance the 'text' feature is missing and silently ignored when reading the SVG data. The supported SVG features are:

**elements:**
: g, path, rect, circle, ellipse, line, polyline, polygon, linearGradient, radialGradient, stop, defs, svg, style

**attributes:**
: width, height, viewBox, preserveAspectRatio with none, xMin, xMid, xMax, yMin, yMid, yMax, slice

**gradient attributes:**
: gradientUnits with objectBoundingBox, gradientTransform, cx, cy, r fx, fy x1, y1, x2, y2 spreadMethod with pad, reflect or repeat, xlink:href

**poly attributes:**
: points

**line attributes:**
: x1, y1, x2, y2

**ellipse attributes:**
: cx, cy, rx, ry

**circle attributes:**
: cx, cy, r

**rectangle attributes:**
: x, y, width, height, rx, ry

**path attributes:**
: d with m, M, l, L, h, H, v, V, c, C, s, S, q, Q, t, T, a, A, z, Z

**style attributes:**
: display with none, visibility, hidden, visible, fill with nonzero and evenodd, opacity, fill-opacity, stroke, stroke-width, stroke-dasharray, stroke-dashoffset, stroke-opacity, stroke-linecap with butt, round and square, stroke-linejoin with miter, round and  bevel, stroke-miterlimit fill-rule, font-size, transform with matrix, translate, scale, rotate, skewX and  skewY, stop-color, stop-opacity, offset, id, class


Currently only SVG images reading and conversion into (pixel-based format) photos is supported: Tk does not (yet) support bundling photo images in SVG vector graphics.

# Color formats

The default image handler can represent/parse color and alpha values of a pixel in one of the formats listed below. If a color format does not contain transparency information, full opacity is assumed.  The available color formats are:

- The empty string - interpreted as full transparency, the color value is undefined.

- Any value accepted by [Tk\_GetColor][GetColor], optionally followed by an alpha suffix. The alpha suffix may be one of:

**@***A*
: The alpha value *A* must be a fractional value in the range  0.0 (fully transparent) to 1.0 (fully opaque).

**#***X*
: The alpha value *X* is a hexadecimal digit that specifies an integer alpha value in the range 0 (fully transparent) to 255 (fully opaque). This is expanded in range from 4 bits wide to 8 bits wide by multiplication by 0x11.

**#***XX*
: The alpha value *XX* is passed as two hexadecimal digits that specify an integer alpha value in the range 0 (fully transparent) to 255 (fully opaque).


- A Tcl list with three or four integers in the range 0 to 255, specifying the values for the red, green, blue and (optionally) alpha channels respectively.

- **#***RGBA* format: a **#** followed by four hexadecimal digits, where each digit is the value for the red, green, blue and alpha channels respectively. Each digit will be expanded internally to 8 bits by multiplication by 0x11.

- **#***RRGGBBAA* format: **#** followed by eight hexadecimal digits, where each pair of  subsequent digits represents the value for the red, green, blue and alpha channels respectively.


# Color allocation

When a photo image is displayed in a window, the photo image code allocates colors to use to display the image and dithers the image, if necessary, to display a reasonable approximation to the image using the colors that are available.  The colors are allocated as a color cube, that is, the number of colors allocated is the product of the number of shades of red, green and blue.

Normally, the number of colors allocated is chosen based on the depth of the window.  For example, in an 8-bit PseudoColor window, the photo image code will attempt to allocate seven shades of red, seven shades of green and four shades of blue, for a total of 198 colors.  In a 1-bit StaticGray (monochrome) window, it will allocate two colors, black and white.  In a 24-bit DirectColor or TrueColor window, it will allocate 256 shades each of red, green and blue.  Fortunately, because of the way that pixel values can be combined in DirectColor and TrueColor windows, this only requires 256 colors to be allocated.  If not all of the colors can be allocated, the photo image code reduces the number of shades of each primary color and tries again.

The user can exercise some control over the number of colors that a photo image uses with the **-palette** configuration option.  If this option is used, it specifies the maximum number of shades of each primary color to try to allocate.  It can also be used to force the image to be displayed in shades of gray, even on a color display, by giving a single number rather than three numbers separated by slashes.

# Metadata dictionary

Each image has a metadata dictionary property. This dictionary is not relevant to the bitmap representation of the image, but may contain additional information like resolution or comments. Image format drivers may output metadata when image data is parsed, or may use metadata to be included in image files or formats.

## Metadata keys (multiple formats)

Each image format driver supports an individual set of metadata dictionary keys. Predefined keys are:

**DPI**
: Horizontal image resolution in DPI as a double value. Supported by format **png**.

**aspect**
: Aspect ratio horizontal divided by vertical as double value. Supported by formats **gif** and **png**.

**comment**
: Image text comment. Supported by formats **gif** and **png**.


It is valid to set any key in the metadata dict. A format driver will ignore keys that it does not handle.

## Metadata keys for animated gif information

The following metadata keys are reported when reading a **gif** format file. They are typically used in conjunction with the **-index** option of an animated **gif** file to properly display the subimage sequence. The options are linked to each subimage selected by **-index**.

**delay time** *time*
: Update delay time in 10ms units. This key is only present if the delay time is not 0.

**disposal method** *method*
: Disposal method of the preceeding image, if given for the current image. Possible values are: **do not dispose**, **restore to background color**, **restore to previous**.

**user interaction** *bool*
: The key is present with a value of 1, if user interaction is specified. Otherwise, the key is not present.

[update region][update] *X0*, *Y0*, *width*, *height*
: Update region of the current subimage, if subimage has not the same size as the full image. The pixel outside of this box are all fully transparent.


# Credits

The photo image type was designed and implemented by Paul Mackerras, based on his earlier photo widget and some suggestions from John Ousterhout.

# Example

Load an image from a file and tile it to the size of a window, which is useful for producing a tiled background:

```
# These lines should be called once
image create photo untiled -file "theFile.ppm"
image create photo tiled

# These lines should be called whenever .someWidget changes
# size; a <Configure> binding is useful here
set width  [winfo width .someWidget]
set height [winfo height .someWidget]
tiled copy untiled -to 0 0 $width $height -shrink
```

[The PNG image loader allows the application of an additional alpha factor during loading, which is useful for generating images suitable for disabled buttons:]{version="8.6"}

```
image create photo icon -file "icon.png"
image create photo iconDisabled -file "icon.png" \
        -format "png -alpha 0.5"
button .b -image icon -disabledimage iconDisabled
```

[Create a green box with a simple shadow effect]{version="9.0"}

```
image create photo foo

# Make a simple graduated fill varying in alpha for the shadow
for {set i 14} {$i > 0} {incr i -1} {
   set i2 [expr {$i + 30}]
   foo put [format black#%x [expr {15-$i}]] -to $i $i $i2 $i2
}

# Put a solid green rectangle on top
foo put #F080 -to 0 0 30 30
```


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[CrtPhImgFmt]: ../TkCAPI/CrtPhImgFmt.md
[GetColor]: ../TkCAPI/GetColor.md
[image]: image.md
[list]: ../Tcl/list.md
[read]: ../Tcl/read.md
[update]: ../Tcl/update.md

