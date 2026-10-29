---
CommandName: image
title: image
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - bitmap(n)
 - options(n)
 - photo(n)
 - nsimage(n)
Keywords:
 - height
 - image
 - types of images
 - width
Copyright:
 - Copyright (c) 1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

image - Create and manipulate images

# Synopsis

::: {.synopsis} :::
[image]{.cmd} [option]{.arg} [arg]{.optdot}
:::

# Description

The **image** command is used to create, delete, and query images. It can take several different forms, depending on the *option* argument.  The legal forms are:

[image]{.cmd} [create]{.sub} [type]{.arg} [name]{.optarg} [option value]{.optdot}
: Creates a new image and a command with the same name and returns its name. *type* specifies the type of the image, which must be one of the types currently defined (e.g., [bitmap]). *name* specifies the name for the image;  if it is omitted then Tk picks a name of the form **image***x*, where *x* is an integer. There may be any number of *option*-*value* pairs, which provide configuration options for the new image. The legal set of options is defined separately for each image type;  see below for details on the options for built-in image types. If an image already exists by the given name then it is replaced with the new image and any instances of that image will redisplay with the new contents. It is important to note that the image command will silently overwrite any procedure that may currently be defined by the given name, so choose the name wisely.  It is recommended to use a separate namespace for image names (e.g., **::img::logo**, **::img::large**).

[image]{.cmd} [delete]{.sub} [name name]{.optdot}
: Deletes each of the named images and returns an empty string. If there are instances of the images displayed in widgets, the images will not actually be deleted until all of the instances are released. However, the association between the instances and the image manager will be dropped. Existing instances will retain their sizes but redisplay as empty areas. If a deleted image is recreated with another call to **image create**, the existing instances will use the new image.

[image]{.cmd} [height]{.sub} [name]{.arg}
: Returns a decimal string giving the height of image *name* in pixels.

[image]{.cmd} [inuse]{.sub} [name]{.arg}
: Returns a boolean value indicating whether or not the image given by *name* is in use by any widgets.

[image]{.cmd} [names]{.sub}
: Returns a list containing the names of all existing images.

[image]{.cmd} [type]{.sub} [name]{.arg}
: Returns the type of image *name* (the value of the *type* argument to **image create** when the image was created).

[image]{.cmd} [types]{.sub}
: Returns a list whose elements are all of the valid image types (i.e., all of the values that may be supplied for the *type* argument to **image create**).

[image]{.cmd} [width]{.sub} [name]{.arg}
: Returns a decimal string giving the width of image *name* in pixels.


Additional operations (e.g. writing the image to a file) may be available as subcommands of the image instance command. See the manual page for the particular image type for details.

# Built-in image types

The following image types are defined by Tk so they will be available in any Tk application. Individual applications or extensions may define additional types.

[bitmap]
: Each pixel in the image displays a foreground color, a background color, or nothing. See the [bitmap] manual entry for more information.

[photo]
: Displays a variety of full-color images, using dithering to approximate colors on displays with limited color capabilities. See the [photo] manual entry for more information.

[nsimage]
: This type is only available in the Aqua platform.  It is a full-color image which may be created from a named system image.  It has options designed to facilitate the use of these images in buttons.  An [nsimage] can be based on an a *template image*, which is an anti-aliased 2-color image format that automatically interchanges black and white in response to changes between the light and dark modes on macOS.  An [nsimage] can also generate an alternate *pressed* variant which is lighter in dark mode and darker in light mode.  These are for use in image buttons. See the [nsimage] manual entry for more information.



[bitmap]: bitmap.md
[nsimage]: nsimage.md
[photo]: photo.md

