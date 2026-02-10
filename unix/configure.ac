! /bin/bash -norc
dnl	This file is an input file used by the GNU "autoconf" program to
dnl	generate the file "configure", which is run during Tk installation
dnl	to configure the system for the local environment.

AC_INIT([tk],[9.1])
AC_PREREQ([2.72])

dnl This is only used when included from macosx/configure.ac
m4_ifdef([SC_USE_CONFIG_HEADERS], [
    AC_CONFIG_HEADERS([tkConfig.h:../unix/tkConfig.h.in])
    AC_CONFIG_COMMANDS_PRE([DEFS="-DHAVE_TK_CONFIG_H  -imacros tkConfig.h"])
    AH_TOP([
    #ifndef _TKCONFIG
    #define _TKCONFIG])
    AH_BOTTOM([
    /* Undef unused package specific autoheader defines so that we can
     * include both tclConfig.h and tkConfig.h at the same time: */
    /* override */ #undef PACKAGE_NAME
    /* override */ #undef PACKAGE_TARNAME
    /* override */ #undef PACKAGE_VERSION
    /* override */ #undef PACKAGE_STRING
    #endif /* _TKCONFIG */])
])

TK_VERSION=9.1
TK_MAJOR_VERSION=9
TK_MINOR_VERSION=1
TK_PATCH_LEVEL="a1"
VERSION=${TK_VERSION}
LOCALES="cs da de el en en_gb eo es fr hu it nl pl pt ru sv"

#--------------------------------------------------------------------
# Find and load the tclConfig.sh file
#--------------------------------------------------------------------

SC_PATH_TCLCONFIG
SC_LOAD_TCLCONFIG

if test "${TCL_MAJOR_VERSION}" -lt 9 ; then
    AC_MSG_ERROR([${PACKAGE_NAME} ${PACKAGE_VERSION} requires Tcl 9.0+
Found config for Tcl ${TCL_VERSION}])
fi

SC_PROG_TCLSH
SC_BUILD_TCLSH

#------------------------------------------------------------------------
# Handle the --prefix=... option
#------------------------------------------------------------------------

if test "${prefix}" = "NONE"; then
    prefix="$TCL_PREFIX"
fi
if test "${exec_prefix}" = "NONE"; then
    exec_prefix=$prefix
fi
# Make sure srcdir is fully qualified!
srcdir="`cd "$srcdir" ; pwd`"
TK_SRC_DIR="`cd "$srcdir"/..; pwd`"

#------------------------------------------------------------------------
# Compress and/or soft link the manpages?
#------------------------------------------------------------------------

SC_CONFIG_MANPAGES

#------------------------------------------------------------------------
# Standard compiler checks
#------------------------------------------------------------------------

# If the user did not set CFLAGS, set it now to keep
# the AC_PROG_CC macro from adding "-g -O2".
if test "${CFLAGS+set}" != "set" ; then
    CFLAGS=""
fi

AC_PROG_CC
AC_C_INLINE

#------------------------------------------------------------------------
# If we're using GCC, see if the compiler understands -pipe.  If so, use it.
# It makes compiling go faster.  (This is only a performance feature.)
#------------------------------------------------------------------------

if test -z "$no_pipe" && test -n "$GCC"; then
    AC_CACHE_CHECK([if the compiler understands -pipe],
	tcl_cv_cc_pipe, [
	hold_cflags=$CFLAGS; CFLAGS="$CFLAGS -pipe"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[tcl_cv_cc_pipe=yes],[tcl_cv_cc_pipe=no])
	CFLAGS=$hold_cflags])
    if test $tcl_cv_cc_pipe = yes; then
	CFLAGS="$CFLAGS -pipe"
    fi
fi

#------------------------------------------------------------------------
# Embedded configuration information, encoding to use for the values, TIP #59
#------------------------------------------------------------------------

SC_TCL_CFG_ENCODING

SC_ENABLE_SHARED

#--------------------------------------------------------------------
# The statements below define a collection of compile flags.  This
# macro depends on the value of SHARED_BUILD, and should be called
# after SC_ENABLE_SHARED checks the configure switches.
#--------------------------------------------------------------------

SC_CONFIG_CFLAGS

SC_ENABLE_SYMBOLS

#--------------------------------------------------------------------
#	Detect what compiler flags to set for 64-bit support.
#--------------------------------------------------------------------

SC_TCL_EARLY_FLAGS

SC_TCL_64BIT_FLAGS

#--------------------------------------------------------------------
#	Check endianness because we can optimize some operations
#--------------------------------------------------------------------

AC_C_BIGENDIAN(,,,[#])

#------------------------------------------------------------------------
# If Tcl and Tk are installed in different places, adjust the library
# search path to reflect this.
#------------------------------------------------------------------------

LIB_RUNTIME_DIR='$(libdir)'

if test "$TCL_EXEC_PREFIX" != "$exec_prefix"; then
    LIB_RUNTIME_DIR="${LIB_RUNTIME_DIR}:${TCL_EXEC_PREFIX}/lib"
fi

if test "$TCL_PREFIX" != "$prefix"; then
    AC_MSG_WARN([
	Different --prefix selected for Tk and Tcl!
	[[package require tk]] may not work correctly in tclsh.])
fi

#--------------------------------------------------------------------
#	Include sys/select.h if it exists and if it supplies things
#	that appear to be useful and aren't already in sys/types.h.
#	This appears to be true only on the RS/6000 under AIX.  Some
#	systems like OSF/1 have a sys/select.h that's of no use, and
#	other systems like SCO UNIX have a sys/select.h that's
#	pernicious.  If "fd_set" isn't defined anywhere then set a
#	special flag.
#--------------------------------------------------------------------

AC_CACHE_CHECK([for fd_set in sys/types], tcl_cv_type_fd_set, [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>]], [[fd_set readMask, writeMask;]])],[tcl_cv_type_fd_set=yes],[tcl_cv_type_fd_set=no])])
tk_ok=$tcl_cv_type_fd_set
if test $tk_ok = no; then
    AC_CACHE_CHECK([for fd_mask in sys/select], tcl_cv_grep_fd_mask, [
	AC_EGREP_HEADER(fd_mask, sys/select.h,
	     tcl_cv_grep_fd_mask=present, tcl_cv_grep_fd_mask=missing)])
    if test $tcl_cv_grep_fd_mask = present; then
	AC_DEFINE(HAVE_SYS_SELECT_H, 1, [Should we include <sys/select.h>?])
	tk_ok=yes
    fi
fi
if test $tk_ok = no; then
    AC_DEFINE(NO_FD_SET, 1, [Do we have fd_set?])
fi

#------------------------------------------------------------------------------
#       Find out all about time handling differences.
#------------------------------------------------------------------------------

AC_CHECK_HEADERS_ONCE([sys/time.h])

#--------------------------------------------------------------------
#	Check for various typedefs and provide substitutes if
#	they don't exist.
#--------------------------------------------------------------------

AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T

AC_CHECK_TYPES([intptr_t, uintptr_t],,,[[
#include <stdint.h>
]])

#-------------------------------------------
#     In OS/390 struct pwd has no pw_gecos field
#-------------------------------------------

AC_CACHE_CHECK([pw_gecos in struct pwd], tcl_cv_pwd_pw_gecos, [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <pwd.h>]], [[struct passwd pwd; (void)pwd.pw_gecos;]])],[tcl_cv_pwd_pw_gecos=yes],[tcl_cv_pwd_pw_gecos=no])])
if test $tcl_cv_pwd_pw_gecos = yes; then
    AC_DEFINE(HAVE_PW_GECOS, 1, [Does struct password have a pw_gecos field?])
fi

#--------------------------------------------------------------------
#	On Mac OS X, we can build either with X11 or with Aqua
#--------------------------------------------------------------------

if test "`uname -s`" = "Darwin" ; then
    AC_MSG_CHECKING([whether to use Aqua])
    AC_ARG_ENABLE(aqua,
	AS_HELP_STRING([--enable-aqua=yes|no],
	    [use Aqua windowingsystem on Mac OS X (default: no)]),
	[tk_aqua=$enableval], [tk_aqua=no])
    if test $tk_aqua = yes -o $tk_aqua = cocoa; then
	tk_aqua=yes
	if test $tcl_corefoundation = no; then
	    AC_MSG_WARN([Aqua can only be used when CoreFoundation is available])
	    tk_aqua=no
	fi
	if test ! -d /System/Library/Frameworks/Cocoa.framework; then
	    AC_MSG_WARN([Aqua can only be used when Cocoa is available])
	    tk_aqua=no
	fi
	if test "`uname -r | awk -F. '{print [$]1}'`" -lt 9; then
	    AC_MSG_WARN([Aqua requires Mac OS X 10.5 or later])
	    tk_aqua=no
	fi
    fi
    AC_MSG_RESULT([$tk_aqua])
    if test $tk_aqua = no; then
	# check if weak linking whole libraries is possible.
	AC_CACHE_CHECK([if ld accepts -weak-l flag], tcl_cv_ld_weak_l, [
	    hold_ldflags=$LDFLAGS
	    LDFLAGS="$LDFLAGS -Wl,-weak-lm"
	    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <math.h>]], [[double f = sin(1.0);]])],
	    [tcl_cv_ld_weak_l=yes],[tcl_cv_ld_weak_l=no])
	    LDFLAGS=$hold_ldflags])
    fi
    AC_CHECK_HEADERS(AvailabilityMacros.h)
else
    tk_aqua=no
fi

if test $tk_aqua = yes; then
    AC_DEFINE(MAC_OSX_TK, 1, [Are we building TkAqua?])
    LIBS="$LIBS -framework Cocoa -framework Carbon -framework IOKit -framework QuartzCore -framework Security -framework CoreGraphics"
    if test -d "/System/Library/Frameworks/UniformTypeIdentifiers.framework"; then
	LIBS="$LIBS -weak_framework UniformTypeIdentifiers"
    fi
    EXTRA_CC_SWITCHES='-x objective-c'
    TK_WINDOWINGSYSTEM=AQUA
    if test -n "${enable_symbols}" -a "${enable_symbols}" != no; then
	AC_DEFINE(TK_MAC_DEBUG, 1, [Are TkAqua debug messages enabled?])
    fi
else
    #--------------------------------------------------------------------
    #	Locate the X11 header files and the X11 library archive.  Try
    #	the ac_path_x macro first, but if it doesn't find the X stuff
    #	(e.g. because there's no xmkmf program) then check through
    #	a list of possible directories.  Under some conditions the
    #	autoconf macro will return an include directory that contains
    #	no include files, so double-check its result just to be safe.
    #--------------------------------------------------------------------

    SC_PATH_X
    TK_WINDOWINGSYSTEM=X11
fi

#--------------------------------------------------------------------
#	Various manipulations on the search path used at runtime to
#	find shared libraries:
#	1. If the X library binaries are in a non-standard directory,
#	   add the X library location into that search path.
#	2. On systems such as AIX and Ultrix that use "-L" as the
#	   search path option, colons cannot be used to separate
#	   directories from each other. Change colons to " -L".
#	3. Create two sets of search flags, one for use in cc lines
#	   and the other for when the linker is invoked directly.  In
#	   the second case, '-Wl,' must be stripped off and commas must
#	   be replaced by spaces.
#--------------------------------------------------------------------

if test "x${x_libraries}" != "x"; then
  if test "x${x_libraries}" != "xNONE"; then
    LIB_RUNTIME_DIR="${LIB_RUNTIME_DIR}:${x_libraries}"
  fi
fi
if test "${TCL_LD_SEARCH_FLAGS}" = '-L${LIB_RUNTIME_DIR}'; then
    LIB_RUNTIME_DIR=`echo ${LIB_RUNTIME_DIR} |sed -e 's/:/ -L/g'`
fi

#--------------------------------------------------------------------
#	Check for the existence of various libraries.  The order here
#	is important, so that then end up in the right order in the
#	command line generated by make.  The -lsocket and -lnsl libraries
#	require a couple of special tricks:
#	1. Use "connect" and "accept" to check for -lsocket, and
#	   "gethostbyname" to check for -lnsl.
#	2. Use each function name only once:  can't redo a check because
#	   autoconf caches the results of the last check and won't redo it.
#	3. Use -lnsl and -lsocket only if they supply procedures that
#	   aren't already present in the normal libraries.  This is because
#	   IRIX 5.2 has libraries, but they aren't needed and they're
#	   bogus:  they goof up name resolution if used.
#	4. On some SVR4 systems, can't use -lsocket without -lnsl too.
#	   To get around this problem, check for both libraries together
#	   if -lsocket doesn't work by itself.
#--------------------------------------------------------------------

if test $tk_aqua = no; then
    AC_CHECK_LIB(Xbsd, main, [LIBS="$LIBS -lXbsd"])
fi

#--------------------------------------------------------------------
# One more check related to the X libraries.  The standard releases
# of Ultrix don't support the "xauth" mechanism, so send won't work
# unless TK_NO_SECURITY is defined.  However, there are usually copies
# of the MIT X server available as well, which do support xauth.
# Check for the MIT stuff and use it if it exists.
#
# Note: can't use ac_check_lib macro (at least, not in Autoconf 2.1)
# because it can't deal with the "-" in the library name.
#--------------------------------------------------------------------

if test -d /usr/include/mit -a $tk_aqua = no; then
    AC_MSG_CHECKING([MIT X libraries])
    tk_oldCFlags=$CFLAGS
    CFLAGS="$CFLAGS -I/usr/include/mit"
    tk_oldLibs=$LIBS
    LIBS="$LIBS -lX11-mit"
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
	#include <X11/Xlib.h>
    ]], [[
	XOpenDisplay(0);
    ]])],[
	AC_MSG_RESULT([yes])
	XLIBSW="-lX11-mit"
	XINCLUDES="-I/usr/include/mit"
    ],[AC_MSG_RESULT(no)])
    CFLAGS=$tk_oldCFlags
    LIBS=$tk_oldLibs
fi

#--------------------------------------------------------------------
#	Check for freetype / fontconfig / Xft support.
#   Only relevant/allowed when using classic X11 backend
#--------------------------------------------------------------------

if test $tk_aqua = no -a "$enable_wayland" = no; then
    AC_MSG_CHECKING([whether to use xft])
    AC_ARG_ENABLE(xft,
	AS_HELP_STRING([--enable-xft],
	    [use freetype/fontconfig/xft (default: on)]),
	[enable_xft=$enableval], [enable_xft="default"])
    XFT_CFLAGS=""
    XFT_LIBS=""
    if test "$enable_xft" = "no" ; then
	AC_MSG_RESULT([$enable_xft])
    fi

    if test $enable_xft = "yes" ; then
	UNIX_FONT_OBJS=tkUnixRFont.o
	AC_DEFINE(HAVE_XFT, 1, [Have we turned on XFT (antialiased fonts)?])
    else
	UNIX_FONT_OBJS=tkUnixFont.o
    fi
    AC_SUBST(XFT_CFLAGS)
    AC_SUBST(XFT_LIBS)
    AC_SUBST(UNIX_FONT_OBJS)
else
    # When Aqua or Wayland → make sure XFT is off
    enable_xft=no
    XFT_CFLAGS=""
    XFT_LIBS=""
    # UNIX_FONT_OBJS already set earlier if Wayland, or left as default
fi

#--------------------------------------------------------------------
#	Check for libcups support
#--------------------------------------------------------------------

if test $tk_aqua = no; then
    AC_MSG_CHECKING([whether to use libcups])
    AC_ARG_ENABLE(libcups,
	AS_HELP_STRING([--enable-libcups],
	    [use libcups (default: on)]),
	[enable_libcups=$enableval], [enable_libcups="default"])
    CUPS_CFLAGS=""
    CUPS_LIBS=""
    if test "$enable_libcups" = "no" ; then
	AC_MSG_RESULT([$enable_libcups])
    else
	found_cups=`cups-config 2>/dev/null`
	dnl make sure package configurator (cups-config)
	dnl says that libcups is present.
	if test "$found_cups" = ""; then
	    found_cups=no
	else
	    found_cups=yes
	    CUPS_CFLAGS="-DHAVE_CUPS"
	    CUPS_LIBS=`cups-config --libs`
	fi
	AC_MSG_RESULT([$found_cups])
	dnl make sure that compiling against CUPS header file doesn't bomb
	if test "$found_cups" = "yes" ; then
	    tk_oldCFlags=$CFLAGS
	    CFLAGS="$CFLAGS $XINCLUDES $CUPS_CFLAGS"
	    tk_oldLibs=$LIBS
	    LIBS="$tk_oldLibs $CUPS_LIBS $XLIBSW"
	    AC_CHECK_HEADER(cups/cups.h, [], [
		found_cups=no
		CUPS_CFLAGS=""
		CUPS_LIBS=""
	    ],[#include <cups/cups.h>])
	    CFLAGS=$tk_oldCFlags
	    LIBS=$tk_oldLibs
	fi
    fi
    AC_SUBST(CUPS_CFLAGS)
    AC_SUBST(CUPS_LIBS)
fi

#--------------------------------------------------------------------
#       Check for Wayland support (non-X11 backend)
#--------------------------------------------------------------------

if test $tk_aqua = no; then
    AC_MSG_CHECKING([whether to use Wayland])
    AC_ARG_ENABLE(wayland,
        AS_HELP_STRING([--enable-wayland],
            [use Wayland (with GLFW/libdecor) instead of X11 (default: off)]),
        [enable_wayland=$enableval], [enable_wayland=no])
    AC_MSG_RESULT([$enable_wayland])

    WAYLAND_CFLAGS=""
    WAYLAND_LIBS=""
    HAVE_WAYLAND=0

    if test "$enable_wayland" = "yes"; then
        # When Wayland is enabled → disable Xft explicitly
        enable_xft=no
        UNIX_FONT_OBJS=tkUnixFont.o   # fallback / non-antialiased unless you later add fontconfig-based rendering

		# ─────────────────────────────────────────────────────────────
		# Wayland-related dependencies (most are required nowadays)
		# ─────────────────────────────────────────────────────────────
		
		AC_MSG_CHECKING([for wayland-client and wayland-cursor ≥ 1.18])
		if pkg-config --exists "wayland-client >= 1.18" \
		   && pkg-config --exists "wayland-cursor >= 1.18"; then
		    AC_MSG_RESULT([yes])
		    WAYLAND_CFLAGS="$(pkg-config --cflags wayland-client wayland-cursor)"
			WAYLAND_LIBS="$(pkg-config --libs wayland-client wayland-cursor)"
		    found_wayland=yes
		    AC_DEFINE([HAVE_WAYLAND], [1], [Define if wayland-client and wayland-cursor are available])
		else
		    AC_MSG_RESULT([no])
		    WAYLAND_CFLAGS=""
		    WAYLAND_LIBS=""
		    found_wayland=no
		    AC_MSG_NOTICE([Wayland client libraries >= 1.18 not found])
		fi
		AC_SUBST([WAYLAND_CFLAGS])
		AC_SUBST([WAYLAND_LIBS])
		
			
		AC_MSG_CHECKING([for xkbcommon ≥ 0.5.0])
		if pkg-config --exists "xkbcommon >= 0.5.0"; then
		    AC_MSG_RESULT([yes])
		    XKBCOMMON_CFLAGS="`pkg-config --cflags xkbcommon`"
		    XKBCOMMON_LIBS="`pkg-config --libs xkbcommon`"
		    found_xkbcommon=yes
		    AC_DEFINE([HAVE_XKBCOMMON], [1], [Define if xkbcommon is available])
		else
		    AC_MSG_RESULT([no])
		    XKBCOMMON_CFLAGS=""
		    XKBCOMMON_LIBS=""
		    found_xkbcommon=no
		    AC_MSG_NOTICE([xkbcommon >= 0.5.0 not found])
		fi
		AC_SUBST([XKBCOMMON_CFLAGS])
		AC_SUBST([XKBCOMMON_LIBS])
		
		
		AC_MSG_CHECKING([for glfw3 >= 3.4 with Wayland support])
		
		if pkg-config --exists "glfw3 >= 3.4"; then
		    AC_MSG_RESULT([glfw3 found])
		    GLFW_CFLAGS="$(pkg-config --cflags glfw3)"
		    GLFW_LIBS="$(pkg-config --libs glfw3)"
		
		    # Check for required Wayland client libraries
		    if pkg-config --exists wayland-client; then
		        found_glfw=yes
		        AC_DEFINE([HAVE_GLFW], [1], [Define if glfw3 with Wayland support is available])
		    else
		        found_glfw=no
		        AC_MSG_NOTICE([GLFW found but Wayland support not detected (missing wayland-client)])
		        GLFW_CFLAGS=""
		        GLFW_LIBS=""
		    fi
		else
		    AC_MSG_RESULT([glfw3 not found])
		    found_glfw=no
		    GLFW_CFLAGS=""
		    GLFW_LIBS=""
		    AC_MSG_NOTICE([glfw3 >= 3.4 not found — backend will be disabled])
		fi
		
		AC_SUBST([GLFW_CFLAGS])
		AC_SUBST([GLFW_LIBS])

		# ─────────────────────────────────────────────────────────────
		# OpenGL ES support (required for NanoVG rendering)
		# ─────────────────────────────────────────────────────────────
		
		AC_MSG_CHECKING([for OpenGL ES 3.0 (glesv2)])
		
		# First try pkg-config
		if pkg-config --exists glesv2; then
		    AC_MSG_RESULT([yes (via pkg-config)])
		    GLES_CFLAGS="$(pkg-config --cflags glesv2)"
		    GLES_LIBS="$(pkg-config --libs glesv2)"
		    found_gles=yes
		    AC_DEFINE([HAVE_GLES], [1], [Define if OpenGL ES is available])
		else
		    # Fallback: try to find the library directly
		    AC_MSG_RESULT([pkg-config not found, checking for library directly])
		    
		    AC_CHECK_HEADER([GLES3/gl3.h], [have_gles_header=yes], [have_gles_header=no])
		    AC_CHECK_LIB([GLESv2], [glGetString], [have_gles_lib=yes], [have_gles_lib=no])
		    
		    if test "$have_gles_header" = yes && test "$have_gles_lib" = yes; then
		        AC_MSG_NOTICE([OpenGL ES library found])
		        GLES_CFLAGS=""
		        GLES_LIBS="-lGLESv2"
		        found_gles=yes
		        AC_DEFINE([HAVE_GLES], [1], [Define if OpenGL ES is available])
		    else
		        AC_MSG_RESULT([no])
		        GLES_CFLAGS=""
		        GLES_LIBS=""
		        found_gles=no
		        AC_MSG_NOTICE([OpenGL ES not found — NanoVG rendering will not work])
		    fi
		fi
		
		AC_SUBST([GLES_CFLAGS])
		AC_SUBST([GLES_LIBS])

		# libdecor is now **required** (especially for GNOME/Mutter client-side decorations)
		AC_MSG_CHECKING([for libdecor-0 ≥ 0.1])
		if pkg-config --exists "libdecor-0 >= 0.1"; then
		    AC_MSG_RESULT([yes])
		    LIBDECOR_CFLAGS="`pkg-config --cflags libdecor-0`"
		    LIBDECOR_LIBS="`pkg-config --libs libdecor-0`"
		    found_libdecor=yes
		    AC_DEFINE([HAVE_LIBDECOR], [1], [Define if libdecor is available])
		else
		    AC_MSG_RESULT([no])
		    LIBDECOR_CFLAGS=""
		    LIBDECOR_LIBS=""
		    found_libdecor=no
		    AC_MSG_NOTICE([libdecor-0 >= 0.1 not found — client-side decorations will not work properly on GNOME])
		fi
		AC_SUBST([LIBDECOR_CFLAGS])
		AC_SUBST([LIBDECOR_LIBS])

		# Check for Ayatana AppIndicator3 (modern system tray/status icon support)
		AC_MSG_CHECKING([for libayatana-appindicator3])
		APPINDICATOR_CFLAGS=""
		APPINDICATOR_LIBS=""
		found_appindicator=no

		if pkg-config --exists "ayatana-appindicator3-0.1" 2>/dev/null; then
		    AC_MSG_RESULT([yes])
		    APPINDICATOR_CFLAGS="`pkg-config --cflags ayatana-appindicator3-0.1`"
		    APPINDICATOR_LIBS="`pkg-config --libs ayatana-appindicator3-0.1`"
		    found_appindicator=yes
		    AC_DEFINE([HAVE_AYATANA_APPINDICATOR], 1,
		        [Define to 1 if libayatana-appindicator3 is available (system tray support)])
		else
		    AC_MSG_RESULT([no])
		    AC_MSG_NOTICE([libayatana-appindicator3 not found — system tray icon support will be unavailable on Wayland])
		fi
		AC_SUBST([APPINDICATOR_CFLAGS])
		AC_SUBST([APPINDICATOR_LIBS])

		
		AC_MSG_CHECKING([for fontconfig ≥ 2.12 (Wayland font support)])
		if pkg-config --exists "fontconfig >= 2.12"; then
		    AC_MSG_RESULT([yes])
		    FONTCONFIG_CFLAGS="`pkg-config --cflags fontconfig`"
		    FONTCONFIG_LIBS="`pkg-config --libs fontconfig`"
		    found_fontconfig=yes
		    AC_DEFINE([HAVE_FONTCONFIG], [1], [Define if fontconfig is available for Wayland])
		else
		    AC_MSG_RESULT([no])
		    FONTCONFIG_CFLAGS=""
		    FONTCONFIG_LIBS=""
		    found_fontconfig=no
		    AC_MSG_NOTICE([fontconfig >= 2.12 not found — font support on Wayland may be limited])
		fi
		AC_SUBST([FONTCONFIG_CFLAGS])
		AC_SUBST([FONTCONFIG_LIBS])

        if test "$found_wayland" = yes \
           && test "$found_xkbcommon" = yes \
           && test "$found_glfw" = yes \
           && test "$found_gles" = yes \
           && test "$found_libdecor" = yes \
           && test "$found_fontconfig" = yes; then

            HAVE_WAYLAND=1

            WAYLAND_CFLAGS="$WAYLAND_CFLAGS $XKBCOMMON_CFLAGS $GLFW_CFLAGS $GLES_CFLAGS $LIBDECOR_CFLAGS $FONTCONFIG_CFLAGS $APPINDICATOR_CFLAGS"
            WAYLAND_LIBS="$WAYLAND_LIBS $XKBCOMMON_LIBS $GLFW_LIBS $GLES_LIBS $LIBDECOR_LIBS $FONTCONFIG_LIBS $APPINDICATOR_LIBS"

            # Still useful if some code paths check for extra libdecor features
            AC_DEFINE([HAVE_LIBDECOR], [1], [Define if libdecor is available])

            AC_DEFINE([TK_USE_WAYLAND], [1], [Define if building with Wayland backend])
            TK_WINDOWINGSYSTEM=WAYLAND

            AC_MSG_NOTICE([Wayland backend enabled with GLFW + OpenGL ES + libdecor (required for GNOME) + fontconfig (Xft disabled)])
              else
            AC_MSG_ERROR([Wayland backend requested but one or more required libraries not found.

Required packages (all mandatory):
  - wayland-client >= 1.18
  - wayland-cursor >= 1.18
  - xkbcommon >= 0.5.0
  - glfw3 >= 3.4
  - glesv2 (OpenGL ES 3.0)          ← required for NanoVG rendering
  - libdecor-0 >= 0.1               ← critical for proper window decorations on GNOME/Mutter
  - fontconfig >= 2.12
  - ayatana-appindicator3-0.1       ← required for system tray / status icon support

Install missing packages (e.g. on Debian/Ubuntu:
  libwayland-dev libxkbcommon-dev libglfw3-dev libgles2-mesa-dev
  libdecor-0-dev libfontconfig-dev libayatana-appindicator3-dev)
])
        fi
    fi

    AC_SUBST([WAYLAND_CFLAGS])
    AC_SUBST([WAYLAND_LIBS])
    AC_SUBST([HAVE_WAYLAND])
fi

#--------------------------------------------------------------------
#	Check for Atk support
#--------------------------------------------------------------------

if test "$tk_aqua" = no; then
  # Combined manual check for ATK, Atk-bridge, GTK, D-Bus, and AT-SPI using pkg-config
  AC_MSG_CHECKING([for ATK, Atk-bridge, D-Bus, and AT-SPI])

  if pkg-config --exists atk \
     && pkg-config --exists atk-bridge-2.0 \
     && pkg-config --exists dbus-1 \
     && pkg-config --exists glib-2.0; then

    AC_MSG_RESULT([yes])
    ATK_CFLAGS="`pkg-config --cflags atk atk-bridge-2.0 dbus-1 glib-2.0`"
    ATK_LIBS="`pkg-config --libs atk atk-bridge-2.0 dbus-1 glib-2.0`"
    AC_DEFINE([USE_ATK], [1], [Define if ATK, Atk-bridge, D-Bus, and GLib are available])
    have_atk=yes
    AC_MSG_NOTICE([All accessibility dependencies found, enabling support])
  else
    AC_MSG_RESULT([no])
    ATK_CFLAGS=""
    ATK_LIBS=""
    have_atk=no
    AC_MSG_NOTICE([One or more accessibility dependencies not found, disabling support])
  fi

  AC_SUBST([ATK_CFLAGS])
  AC_SUBST([ATK_LIBS])
fi



#--------------------------------------------------------------------
# XXX Do this last.
# It might modify XLIBSW which could affect other tests.
#
# Check whether the header and library for the XScreenSaver
# extension are available, and set HAVE_XSS if so.
# XScreenSaver is needed for Tk_GetUserInactiveTime().
#--------------------------------------------------------------------

if test $tk_aqua = no; then
    tk_oldCFlags=$CFLAGS
    CFLAGS="$CFLAGS $XINCLUDES"
    tk_oldLibs=$LIBS
    LIBS="$tk_oldLibs $XLIBSW"
    xss_header_found=no
    xss_lib_found=no
    AC_MSG_CHECKING([whether to try to use XScreenSaver])
    AC_ARG_ENABLE(xss,
	AS_HELP_STRING([--enable-xss],
	    [use XScreenSaver for activity timer (default: on)]),
	[enable_xss=$enableval], [enable_xss=yes])
    if test "$enable_xss" = "no" ; then
	AC_MSG_RESULT([$enable_xss])
    else
	AC_MSG_RESULT([$enable_xss])
	AC_CHECK_HEADER(X11/extensions/scrnsaver.h, [
	    xss_header_found=yes
	],,[#include <X11/Xlib.h>])
	AC_CHECK_FUNC(XScreenSaverQueryInfo,,[
	    AC_CHECK_LIB(Xext, XScreenSaverQueryInfo, [
		XLIBSW="$XLIBSW -lXext"
		xss_lib_found=yes
	    ], [
		AC_CHECK_LIB(Xss, XScreenSaverQueryInfo, [
		    if test "$tcl_cv_ld_weak_l" = yes; then
			# On Darwin, weak link libXss if possible,
			# as it is only available on Tiger or later.
			XLIBSW="$XLIBSW -Wl,-weak-lXss -lXext"
		    else
			XLIBSW="$XLIBSW -lXss -lXext"
		    fi
		    xss_lib_found=yes
		],, -lXext)
	    ])
	])
    fi
    if test $enable_xss = yes -a $xss_lib_found = yes -a $xss_header_found = yes; then
	AC_DEFINE(HAVE_XSS, 1, [Is XScreenSaver available?])
    fi
    CFLAGS=$tk_oldCFlags
    LIBS=$tk_oldLibs
fi

#--------------------------------------------------------------------
#	Figure out whether "char" is unsigned.  If so, set a
#	#define for __CHAR_UNSIGNED__.
#--------------------------------------------------------------------

AC_C_CHAR_UNSIGNED

#--------------------------------------------------------------------
#	The statements below define a collection of symbols related to
#	building libtk as a shared library instead of a static library.
#--------------------------------------------------------------------

eval eval "TK_UNSHARED_LIB_SUFFIX=${UNSHARED_LIB_SUFFIX}"
eval eval "TK_SHARED_LIB_SUFFIX=${SHARED_LIB_SUFFIX}"
if test "$ac_cv_cygwin" = "yes" -a "$SHARED_BUILD" != "0"; then
eval "TK_LIB_FILE=cygtcl9tk${LIB_SUFFIX}"
EXTRA_INSTALL_BINARIES='$(INSTALL_LIBRARY) $(patsubst cyg%.dll,lib%.dll.a,${LIB_FILE}) "$(LIB_INSTALL_DIR)"'
else
eval "TK_LIB_FILE=libtcl9tk${LIB_SUFFIX}"
fi

# tkConfig.sh needs a version of the _LIB_SUFFIX that has been eval'ed
# since on some platforms TK_LIB_FILE contains shell escapes.

eval "TK_LIB_FILE=${TK_LIB_FILE}"

if test "${SHARED_BUILD}" = "1" -a "${SHLIB_SUFFIX}" != ""; then
    SHLIB_LD_LIBS="${SHLIB_LD_LIBS} \${TCL_STUB_LIB_SPEC}"
    TCL_STUB_FLAGS="-DUSE_TCL_STUBS"
fi

test -z "$TK_LIBRARY" && TK_LIBRARY='$(prefix)/lib/tk$(VERSION)'
PRIVATE_INCLUDE_DIR='$(includedir)'
HTML_DIR='$(DISTDIR)/html'
TK_PKG_DIR='tk$(VERSION)'
TK_RSRC_FILE='tk$(VERSION).rsrc'
WISH_RSRC_FILE='wish$(VERSION).rsrc'

# Note:  in the following variable, it's important to use the absolute
# path name of the Tcl directory rather than "..":  this is because
# AIX remembers this path and will attempt to use it at run-time to look
# up the Tcl library.

if test "`uname -s`" = "Darwin" ; then
    SC_ENABLE_FRAMEWORK
    TK_SHLIB_LD_EXTRAS="-compatibility_version ${TK_VERSION} -current_version ${TK_VERSION}`echo ${TK_PATCH_LEVEL} | awk ['{match($0, "\\\.[0-9]+"); print substr($0,RSTART,RLENGTH)}']`"
    TK_SHLIB_LD_EXTRAS="${TK_SHLIB_LD_EXTRAS}"' -install_name "${DYLIB_INSTALL_DIR}/${TK_LIB_FILE}" -unexported_symbols_list $$(f=$(TK_LIB_FILE).E && nm -gp tkMacOSX*.o 2>/dev/null | awk "/^[[0-9a-f]]+ . \.objc/ {print \$$3}" > $$f && nm -gjp "$(TCL_BIN_DIR)"/$(TCL_STUB_LIB_FILE) | grep ^_[[^_]] >> $$f && echo $$f)'
    echo "$LDFLAGS " | grep -q -- '-prebind ' && TK_SHLIB_LD_EXTRAS="${TK_SHLIB_LD_EXTRAS}"' -seg1addr 0xb000000'
    TK_SHLIB_LD_EXTRAS="${TK_SHLIB_LD_EXTRAS}"' -sectcreate __TEXT __info_plist Tk-Info.plist'
    EXTRA_WISH_LIBS='-sectcreate __TEXT __info_plist Wish-Info.plist'
    EXTRA_WISH_LIBS=${EXTRA_WISH_LIBS}' -sectcreate __TEXT __credits_html Credits.html'
    if test "${SHARED_BUILD}" = "0"; then
	EXTRA_WISH_LIBS=${EXTRA_WISH_LIBS}' -ObjC'
    fi
    AC_CONFIG_FILES([Tk-Info.plist:../macosx/Tk-Info.plist.in Wish-Info.plist:../macosx/Wish-Info.plist.in Credits.html:../macosx/Credits.html.in])
    for l in ${LOCALES}; do CFBUNDLELOCALIZATIONS="${CFBUNDLELOCALIZATIONS}<string>$l</string>"; done
    TK_YEAR="`date +%Y`"
fi

if test "$FRAMEWORK_BUILD" = "1" ; then
    AC_DEFINE(TK_FRAMEWORK, 1, [Is Tk built as a framework?])
    # Construct a fake local framework structure to make linking with
    # '-framework Tk' and running of tktest work
    AC_CONFIG_COMMANDS([Tk.framework], [n=Tk &&
	f=$n.framework && v=Versions/$VERSION &&
	rm -rf $f && mkdir -p $f/$v/Resources &&
	ln -s $v/$n $v/Resources $f && ln -s ../../../$n $f/$v &&
	ln -s ../../../../$n-Info.plist $f/$v/Resources/Info.plist &&
	if test $tk_aqua = yes; then ln -s ../../../../$n.rsrc $f/$v/Resources; fi &&
	unset n f v
    ], VERSION=${TK_VERSION} && tk_aqua=${tk_aqua})
    LD_LIBRARY_PATH_VAR="DYLD_FRAMEWORK_PATH"
    if test "${libdir}" = '${exec_prefix}/lib'; then
	# override libdir default
	libdir="/Library/Frameworks"
    fi
    TK_LIB_FILE="Tk"
    TK_LIB_FLAG="-framework Tk"
    TK_BUILD_LIB_SPEC="-F`pwd | sed -e 's/ /\\\\ /g'` -framework Tk"
    TK_LIB_SPEC="-F${libdir} -framework Tk"
    libdir="${libdir}/Tk.framework/Versions/\${VERSION}"
    TK_LIBRARY="${libdir}/Resources/Scripts"
    TK_PKG_DIR="Resources/Scripts"
    TK_RSRC_FILE="Tk.rsrc"
    WISH_RSRC_FILE="Wish.rsrc"
    includedir="${libdir}/Headers"
    PRIVATE_INCLUDE_DIR="${libdir}/PrivateHeaders"
    HTML_DIR="${libdir}/Resources/Documentation/Reference/Tk"
    EXTRA_INSTALL="install-private-headers html-tk"
    EXTRA_BUILD_HTML='@ln -fs contents.htm "$(HTML_INSTALL_DIR)"/TkTOC.html'
    EXTRA_INSTALL_BINARIES='@echo "Installing Info.plist to $(LIB_INSTALL_DIR)/Resources/" && $(INSTALL_DATA_DIR) "$(LIB_INSTALL_DIR)/Resources" && $(INSTALL_DATA) Tk-Info.plist "$(LIB_INSTALL_DIR)/Resources/Info.plist"'
    EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Installing Credits.html to $(LIB_INSTALL_DIR)/Resources/" && $(INSTALL_DATA_DIR) "$(LIB_INSTALL_DIR)/Resources" && $(INSTALL_DATA) Credits.html "$(LIB_INSTALL_DIR)/Resources"'
    EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Installing license.terms to $(LIB_INSTALL_DIR)/Resources/" && $(INSTALL_DATA) "$(TOP_DIR)/license.terms" "$(LIB_INSTALL_DIR)/Resources"'
    if test $tk_aqua = yes; then
	EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Installing Images to $(LIB_INSTALL_DIR)/Resources/" && $(INSTALL_DATA_DIR) "$(LIB_INSTALL_DIR)/Resources" && for i in Tk.tiff Tk.icns; do $(INSTALL_DATA) "$(MAC_OSX_DIR)/$$i" "$(LIB_INSTALL_DIR)/Resources"; done'
	EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Installing wish$(VERSION) script to $(INSTALL_ROOT)/'"${bindir}"'/" && $(INSTALL_DATA_DIR) "$(INSTALL_ROOT)/'"${bindir}"'" && printf > "$(INSTALL_ROOT)/'"${bindir}"'/wish$(VERSION)" "#!/bin/sh\n\"\$$(dirname \$$0)'"`eval d="${bindir}"; echo "$d" | sed -e 's#/[^/][^/]*#/..#g'`"'$(bindir)/Wish\" \"\$$@\"" && chmod +x "$(INSTALL_ROOT)/'"${bindir}"'/wish$(VERSION)"'
	bindir="${libdir}/Resources/Wish.app/Contents/MacOS"
	EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Installing Info.plist to $(BIN_INSTALL_DIR)/.." && $(INSTALL_DATA) Wish-Info.plist "$(BIN_INSTALL_DIR)/../Info.plist" && mv -f "$(BIN_INSTALL_DIR)/wish$(VERSION)" "$(BIN_INSTALL_DIR)/Wish"'
	EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Installing Wish.icns to $(BIN_INSTALL_DIR)/../Resources" && $(INSTALL_DATA_DIR) "$(BIN_INSTALL_DIR)/../Resources"'
	EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && $(INSTALL_DATA) "$(MAC_OSX_DIR)/Tk.icns" "$(BIN_INSTALL_DIR)/../Resources/Wish.icns"'
	EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Installing Wish.sdef to $(BIN_INSTALL_DIR)/../Resources" && $(INSTALL_DATA) "$(MAC_OSX_DIR)/Wish.sdef" "$(BIN_INSTALL_DIR)/../Resources"'
	EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Installing Credits.html to $(BIN_INSTALL_DIR)/../Resources" && $(INSTALL_DATA) Credits.html "$(BIN_INSTALL_DIR)/../Resources"'
    fi
    EXTRA_INSTALL_BINARIES="$EXTRA_INSTALL_BINARIES"' && echo "Finalizing Tk.framework" && rm -f "$(LIB_INSTALL_DIR)/../Current" && ln -s "$(VERSION)" "$(LIB_INSTALL_DIR)/../Current" && for f in "$(LIB_FILE)" tkConfig.sh Resources Headers PrivateHeaders; do rm -f "$(LIB_INSTALL_DIR)/../../$$f" && ln -s "Versions/Current/$$f" "$(LIB_INSTALL_DIR)/../.."; done && f="$(STUB_LIB_FILE)" && rm -f "$(LIB_INSTALL_DIR)/../../$$f" && ln -s "Versions/$(VERSION)/$$f" "$(LIB_INSTALL_DIR)/../.."'
    # Don't use AC_DEFINE for the following as the framework version define
    # needs to go into the Makefile even when using autoheader, so that we
    # can pick up a potential make override of VERSION. Also, don't put this
    # into CFLAGS as it should not go into tkConfig.sh
    EXTRA_CC_SWITCHES="$EXTRA_CC_SWITCHES"' -DTK_FRAMEWORK_VERSION=\"$(VERSION)\"'
else
    if test $tk_aqua = yes; then
	EXTRA_INSTALL_BINARIES='@echo "Installing Images to $(LIB_INSTALL_DIR)/" && $(INSTALL_DATA_DIR) "$(LIB_INSTALL_DIR)" && for i in Tk.tiff Tk.icns; do $(INSTALL_DATA) "$(MAC_OSX_DIR)/$$i" "$(LIB_INSTALL_DIR)"; done'
    fi
    # libdir must be a fully qualified path and not ${exec_prefix}/lib
    eval libdir="$libdir"
    TK_LIB_FLAG="-ltcl9"
    if test "${ac_cv_cygwin}" = "yes" -a "$SHARED_BUILD" = "1"; then
	TK_LIB_FLAG="${TK_LIB_FLAG}tk`echo ${TK_VERSION} | tr -d .`"
	TK_BUILD_LIB_SPEC="-L\$(TOP_DIR)/win ${TK_LIB_FLAG}"
    else
	if test "${TCL_LIB_VERSIONS_OK}" = "ok"; then
	    TK_LIB_FLAG="${TK_LIB_FLAG}tk${TK_VERSION}"
	else
	    TK_LIB_FLAG="${TK_LIB_FLAG}tk`echo ${TK_VERSION} | tr -d .`"
	fi
	TK_BUILD_LIB_SPEC="-L`pwd | sed -e 's/ /\\\\ /g'` ${TK_LIB_FLAG}"
    fi
    TK_LIB_SPEC="-L${libdir} ${TK_LIB_FLAG}"
fi

#--------------------------------------------------------------------
#	Zipfs support - Tip 430
#--------------------------------------------------------------------
AC_ARG_ENABLE(zipfs,
    AS_HELP_STRING([--enable-zipfs],[build with Zipfs support (default: on)]),
    [tcl_ok=$enableval], [tcl_ok=yes])
if test "$tcl_ok" = "yes" -a "x$enable_framework" != "xyes"; then
    #
    # Find a native compiler
    #
    AX_CC_FOR_BUILD
    #
    # Find a native zip implementation
    #
    SC_ZIPFS_SUPPORT
	ZIPFS_BUILD=1
	TK_ZIP_FILE=libtk${TK_MAJOR_VERSION}.${TK_MINOR_VERSION}${TK_PATCH_LEVEL}.zip
else
	ZIPFS_BUILD=0
	TK_ZIP_FILE=
fi
# Do checking message here to not mess up interleaved configure output
AC_MSG_CHECKING([for building with zipfs])
if test "${ZIPFS_BUILD}" = 1; then
    if test "${SHARED_BUILD}" = 0; then
       ZIPFS_BUILD=2;
       AC_DEFINE(ZIPFS_BUILD, 2, [Are we building with zipfs enabled?])
       INSTALL_LIBRARIES=install-demos
       AC_MSG_RESULT([yes])
     else
       AC_DEFINE(ZIPFS_BUILD, 1, [Are we building with zipfs enabled?])\
       INSTALL_LIBRARIES=install-demos
       AC_MSG_RESULT([yes])
    fi
else
AC_MSG_RESULT([no])
INSTALL_LIBRARIES=install-libraries
INSTALL_MSGS=install-msgs
fi
AC_SUBST(ZIPFS_BUILD)
AC_SUBST(TK_ZIP_FILE)
AC_SUBST(INSTALL_LIBRARIES)
AC_SUBST(INSTALL_MSGS)


#--------------------------------------------------------------------
#       The statements below define various symbols relating to Tk
#       stub support.
#--------------------------------------------------------------------

# Replace ${VERSION} with contents of ${TK_VERSION}
    TK_STUB_LIB_FILE="libtkstub.a"
    TK_STUB_LIB_FLAG="-ltkstub"
eval "TK_STUB_LIB_DIR=\"${libdir}\""

TK_BUILD_STUB_LIB_SPEC="-L`pwd | sed -e 's/ /\\\\ /g'` ${TK_STUB_LIB_FLAG}"
TK_STUB_LIB_SPEC="-L${TK_STUB_LIB_DIR} ${TK_STUB_LIB_FLAG}"
TK_BUILD_STUB_LIB_PATH="`pwd`/${TK_STUB_LIB_FILE}"
TK_STUB_LIB_PATH="${TK_STUB_LIB_DIR}/${TK_STUB_LIB_FILE}"

# Install time header dir can be set via --includedir
eval "TK_INCLUDE_SPEC=\"-I${includedir}\""

#------------------------------------------------------------------------
# Demo dir
#------------------------------------------------------------------------

AS_IF([test x"${DEMO_DIR}" = x], [DEMO_DIR='$(TK_LIBRARY)/demos'])
eval "TK_DEMO_DIR=\"`echo ${DEMO_DIR} | tr '()' '{}'`\""
eval "TK_DEMO_DIR=\"`echo ${TK_DEMO_DIR} | tr '()' '{}'`\""
AC_SUBST(DEMO_DIR)
AC_SUBST(TK_DEMO_DIR)

#------------------------------------------------------------------------
# tkConfig.sh refers to this by a different name
#------------------------------------------------------------------------

TK_SHARED_BUILD=${SHARED_BUILD}

AC_SUBST(TK_VERSION)
AC_SUBST(TK_MAJOR_VERSION)
AC_SUBST(TK_MINOR_VERSION)
AC_SUBST(TK_PATCH_LEVEL)
AC_SUBST(TK_YEAR)

AC_SUBST(TK_LIB_FILE)
AC_SUBST(TK_LIB_FLAG)
AC_SUBST(TK_LIB_SPEC)
AC_SUBST(TK_STUB_LIB_FILE)
AC_SUBST(TK_STUB_LIB_FLAG)
AC_SUBST(TK_STUB_LIB_SPEC)
AC_SUBST(TK_STUB_LIB_PATH)
AC_SUBST(TK_INCLUDE_SPEC)
AC_SUBST(TK_BUILD_STUB_LIB_SPEC)
AC_SUBST(TK_BUILD_STUB_LIB_PATH)

AC_SUBST(TK_SRC_DIR)

AC_SUBST(TK_SHARED_BUILD)
AC_SUBST(LD_LIBRARY_PATH_VAR)

AC_SUBST(TK_BUILD_LIB_SPEC)

AC_SUBST(TCL_STUB_FLAGS)
AC_SUBST(XINCLUDES)
AC_SUBST(XLIBSW)
AC_SUBST(LOCALES)

AC_SUBST(TK_WINDOWINGSYSTEM)
AC_SUBST(TK_PKG_DIR)
AC_SUBST(TK_LIBRARY)
AC_SUBST(LIB_RUNTIME_DIR)
AC_SUBST(PRIVATE_INCLUDE_DIR)
AC_SUBST(HTML_DIR)

AC_SUBST(EXTRA_CC_SWITCHES)
AC_SUBST(EXTRA_APP_CC_SWITCHES)
AC_SUBST(EXTRA_INSTALL)
AC_SUBST(EXTRA_INSTALL_BINARIES)
AC_SUBST(EXTRA_BUILD_HTML)
AC_SUBST(EXTRA_WISH_LIBS)
AC_SUBST(CFBUNDLELOCALIZATIONS)

AC_SUBST(TK_RSRC_FILE)
AC_SUBST(WISH_RSRC_FILE)
AC_SUBST(LIB_RSRC_FILE)
AC_SUBST(APP_RSRC_FILE)
AC_SUBST(REZ)
AC_SUBST(REZ_FLAGS)

AC_CONFIG_FILES([
    Makefile:../unix/Makefile.in
    tkConfig.sh:../unix/tkConfig.sh.in
    tk.pc:../unix/tk.pc.in
])
AC_OUTPUT

dnl Local Variables:
dnl mode: autoconf
dnl End:
