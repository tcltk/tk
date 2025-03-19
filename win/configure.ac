#! /bin/bash -norc
# This file is an input file used by the GNU "autoconf" program to
# generate the file "configure", which is run during Tk installation
# to configure the system for the local environment.

AC_INIT([tk],[9.1])
AC_CONFIG_SRCDIR([../generic/tk.h])
AC_PREREQ([2.72])

# The following define is needed when building with Cygwin since newer
# versions of autoconf incorrectly set SHELL to /bin/bash instead of
# /bin/sh. The bash shell seems to suffer from some strange failures.
SHELL=/bin/sh

TK_VERSION=9.1
TK_MAJOR_VERSION=9
TK_MINOR_VERSION=1
TK_PATCH_LEVEL="a0"
VER=$TK_MAJOR_VERSION$TK_MINOR_VERSION

#------------------------------------------------------------------------
# Handle the --prefix=... option
#------------------------------------------------------------------------

if test "${prefix}" = "NONE"; then
    prefix=/usr/local
fi
if test "${exec_prefix}" = "NONE"; then
    exec_prefix=$prefix
fi
# libdir must be a fully qualified path (not ${exec_prefix}/lib)
eval libdir="$libdir"

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
AC_CHECK_INCLUDES_DEFAULT

AC_CHECK_TOOL(AR, ar)
AC_CHECK_TOOL(RANLIB, ranlib)
AC_CHECK_TOOL(RC, windres)

#--------------------------------------------------------------------
# Checks to see if the make program sets the $MAKE variable.
#--------------------------------------------------------------------

AC_PROG_MAKE_SET

#--------------------------------------------------------------------
# Determines the correct binary file extension (.o, .obj, .exe etc.)
#--------------------------------------------------------------------

AC_OBJEXT
AC_EXEEXT

#--------------------------------------------------------------------
# The statements below define a collection of symbols related to
# building libtk as a shared library instead of a static library.
#--------------------------------------------------------------------

SC_ENABLE_SHARED

#--------------------------------------------------------------------
# Locate and source the tclConfig.sh file.
#--------------------------------------------------------------------

SC_PATH_TCLCONFIG($TK_PATCH_LEVEL)
SC_LOAD_TCLCONFIG

if test "${TCL_MAJOR_VERSION}" -lt 9 ; then
    AC_MSG_ERROR([${TCL_BIN_DIR}/tclConfig.sh is for Tcl ${TCL_VERSION}.
Tk ${TK_VERSION}${TK_PATCH_LEVEL} needs Tcl 9.0+.
Use --with-tcl= option to indicate location of tclConfig.sh file for Tcl 9.0 or better.])
fi

#--------------------------------------------------------------------
# The statements below define a collection of compile flags.  This
# macro depends on the value of SHARED_BUILD, and should be called
# after SC_ENABLE_SHARED checks the configure switches.
#--------------------------------------------------------------------

SC_CONFIG_CFLAGS

AC_CHECK_TYPES([intptr_t, uintptr_t],,,[[
#include <stdint.h>
]])

#--------------------------------------------------------------------
# man2tcl needs this so that it can use errno.h
#--------------------------------------------------------------------

AC_CHECK_HEADER(errno.h, , MAN2TCLFLAGS="-DNO_ERRNO_H")
AC_SUBST(MAN2TCLFLAGS)

#-------------------------------------------
#     Check for _strtoi64
#-------------------------------------------

AC_CACHE_CHECK([availability of _strtoi64], tcl_cv_strtoi64, [
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdlib.h>]],
    [[_strtoi64(0,0,0)]])],[tcl_cv_strtoi64=yes],[tcl_cv_strtoi64=no])])
if test $tcl_cv_strtoi64 = no; then
    AC_DEFINE(NO_STRTOI64, 1, [Is _strtoi64 function available?])
fi

AC_CHECK_TYPE([intptr_t], [
    AC_DEFINE([HAVE_INTPTR_T], 1, [Do we have the intptr_t type?])], [
    AC_CACHE_CHECK([for pointer-size signed integer type], tcl_cv_intptr_t, [
    for tcl_cv_intptr_t in "int" "long" "__int64" none; do
	if test "$tcl_cv_intptr_t" != none; then
	    AC_COMPILE_IFELSE([AC_LANG_BOOL_COMPILE_TRY([AC_INCLUDES_DEFAULT],
		    [[sizeof (void *) <= sizeof ($tcl_cv_intptr_t)]])],
		[tcl_ok=yes], [tcl_ok=no])
	    test "$tcl_ok" = yes && break; fi
    done])
    if test "$tcl_cv_intptr_t" != none; then
	AC_DEFINE_UNQUOTED([intptr_t], [$tcl_cv_intptr_t], [Signed integer
	   type wide enough to hold a pointer.])
    fi
])
AC_CHECK_TYPE([uintptr_t], [
    AC_DEFINE([HAVE_UINTPTR_T], 1, [Do we have the uintptr_t type?])], [
    AC_CACHE_CHECK([for pointer-size unsigned integer type], tcl_cv_uintptr_t, [
    for tcl_cv_uintptr_t in "unsigned int" "unsigned long" "unsigned __int64" \
	    none; do
	if test "$tcl_cv_uintptr_t" != none; then
	    AC_COMPILE_IFELSE([AC_LANG_BOOL_COMPILE_TRY([AC_INCLUDES_DEFAULT],
		    [[sizeof (void *) <= sizeof ($tcl_cv_uintptr_t)]])],
		[tcl_ok=yes], [tcl_ok=no])
	    test "$tcl_ok" = yes && break; fi
    done])
    if test "$tcl_cv_uintptr_t" != none; then
	AC_DEFINE_UNQUOTED([uintptr_t], [$tcl_cv_uintptr_t], [Unsigned integer
	   type wide enough to hold a pointer.])
    fi
])

#--------------------------------------------------------------------
# Set the default compiler switches based on the --enable-symbols
# option.  This macro depends on C flags, and should be called
# after SC_CONFIG_CFLAGS macro is called.
#--------------------------------------------------------------------

SC_ENABLE_SYMBOLS

#--------------------------------------------------------------------
# Embed the manifest if we can determine how
#--------------------------------------------------------------------

SC_EMBED_MANIFEST(wish.exe.manifest)

SC_BUILD_TCLSH
SC_PROG_TCLSH

#--------------------------------------------------------------------
#	Zipfs support - Tip 430
#--------------------------------------------------------------------
AC_ARG_ENABLE(zipfs,
    AS_HELP_STRING([--enable-zipfs],
	[build with Zipfs support (default: on)]),
    [tcl_ok=$enableval], [tcl_ok=yes])
if test "$tcl_ok" = "yes" ; then
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
	TCL_ZIP_FILE=
fi
# Do checking message here to not mess up interleaved configure output
AC_MSG_CHECKING([for building with zipfs])
if test "${ZIPFS_BUILD}" = 1; then
    if test "${SHARED_BUILD}" = 0; then
       ZIPFS_BUILD=2;
       AC_DEFINE(ZIPFS_BUILD, 2, [Are we building with zipfs enabled?])
       INSTALL_LIBRARIES=install-libraries-zipfs-static
       AC_MSG_RESULT([yes])
     else
       AC_DEFINE(ZIPFS_BUILD, 1, [Are we building with zipfs enabled?])\
       INSTALL_LIBRARIES=install-libraries-zipfs-shared
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


#------------------------------------------------------------------------
# tkConfig.sh refers to this by a different name
#------------------------------------------------------------------------

TK_SHARED_BUILD=${SHARED_BUILD}

#--------------------------------------------------------------------
# Perform final evaluations of variables with possible substitutions.
#--------------------------------------------------------------------

TK_SHARED_LIB_SUFFIX="\${NODOT_VERSION}${DLLSUFFIX}"
TK_UNSHARED_LIB_SUFFIX="\${NODOT_VERSION}${LIBSUFFIX}"

eval "TK_SRC_DIR=\"`cd $srcdir/..; $CYGPATH $(pwd)`\""

eval "TK_DLL_FILE=tcl9tk$VER${DLLSUFFIX}"
if test ${SHARED_BUILD} = 0 -o "$GCC" != "yes" ; then
 eval "TK_LIB_FILE=${LIBPREFIX}tcl9tk${VER}${LIBSUFFIX}"
else
 eval "TK_LIB_FILE=${LIBPREFIX}tcl9tk${VER}${DLLSUFFIX}.a"
fi

eval "TK_STUB_LIB_FILE=${LIBPREFIX}tkstub${LIBSUFFIX}" ;
# FIXME: All of this var junk needs to be done in tcl.m4 !!!!
# I left out the other vars that also need to get defined here.
# we also need to double check about spaces in path names
TK_LIB_FLAG="-ltcl9"
eval "TK_LIB_FLAG=\"${TK_LIB_FLAG}tk${VER}${LIBFLAGSUFFIX}\""
TK_LIB_SPEC="-L${libdir} ${TK_LIB_FLAG}"
eval "TK_BUILD_LIB_SPEC=\"-L`$CYGPATH $(pwd)` ${TK_LIB_FLAG}\""

eval "TK_STUB_LIB_FLAG=\"-ltkstub${LIBFLAGSUFFIX}\""
eval "TK_BUILD_STUB_LIB_SPEC=\"-L`$CYGPATH $(pwd)` ${TK_STUB_LIB_FLAG}\""

TK_STUB_LIB_SPEC="-L${libdir} ${TK_STUB_LIB_FLAG}"
TK_STUB_LIB_PATH="${libdir}/${TK_STUB_LIB_FILE}"
eval "TK_BUILD_STUB_LIB_PATH=\"`$CYGPATH $(pwd)`/${TK_STUB_LIB_FILE}\""

eval "DLLSUFFIX=${DLLSUFFIX}"
eval "LIBPREFIX=${LIBPREFIX}"
eval "LIBSUFFIX=${LIBSUFFIX}"
eval "EXESUFFIX=${EXESUFFIX}"

CFG_TK_SHARED_LIB_SUFFIX=${TK_SHARED_LIB_SUFFIX}
CFG_TK_UNSHARED_LIB_SUFFIX=${TK_UNSHARED_LIB_SUFFIX}

#--------------------------------------------------------------------
# Adjust the defines for how the resources are built depending
# on symbols and static vs. shared.
#--------------------------------------------------------------------

if test ${SHARED_BUILD} = 0; then
    RC_DEFINES="${RC_DEFINE} STATIC_BUILD"
    TK_RES=""
else
    RC_DEFINES=""
    TK_RES='tk.$(RES)'
fi

# The wish.exe.manifest requires these
# TK_WIN_VERSION is the 4 dotted pair Windows version format which needs
# the release level, and must account for interim release versioning
case "$TK_PATCH_LEVEL" in
     *a*) TK_RELEASE_LEVEL=0 ;;
     *b*) TK_RELEASE_LEVEL=1 ;;
     *)   TK_RELEASE_LEVEL=2 ;;
esac
TK_WIN_VERSION="$TK_VERSION.$TK_RELEASE_LEVEL.`echo $TK_PATCH_LEVEL | tr -d ab.`"
AC_SUBST(TK_WIN_VERSION)
# X86|AMD64|IA64 for manifest
AC_SUBST(MACHINE)

AC_SUBST(TK_VERSION)
AC_SUBST(TK_MAJOR_VERSION)
AC_SUBST(TK_MINOR_VERSION)
AC_SUBST(TK_PATCH_LEVEL)
AC_SUBST(TK_LIB_FILE)
AC_SUBST(TK_DLL_FILE)
AC_SUBST(TK_STUB_LIB_FILE)
AC_SUBST(TK_STUB_LIB_FLAG)
AC_SUBST(TK_BUILD_STUB_LIB_SPEC)
AC_SUBST(TK_SRC_DIR)
AC_SUBST(TK_BIN_DIR)

AC_SUBST(TCL_VERSION)
AC_SUBST(TCL_MAJOR_VERSION)
AC_SUBST(TCL_MINOR_VERSION)
AC_SUBST(TCL_PATCH_LEVEL)

AC_SUBST(TCL_SRC_DIR)
AC_SUBST(TCL_BIN_DIR)
AC_SUBST(CFG_TK_SHARED_LIB_SUFFIX)
AC_SUBST(CFG_TK_UNSHARED_LIB_SUFFIX)

AC_SUBST(CFLAGS_DEFAULT)
AC_SUBST(EXTRA_CFLAGS)
AC_SUBST(CYGPATH)
AC_SUBST(DEPARG)
AC_SUBST(CC_OBJNAME)
AC_SUBST(CC_EXENAME)

# win/tcl.m4 doesn't set (LDFLAGS)
AC_SUBST(LDFLAGS_DEFAULT)
AC_SUBST(LDFLAGS_DEBUG)
AC_SUBST(LDFLAGS_OPTIMIZE)
AC_SUBST(LDFLAGS_CONSOLE)
AC_SUBST(LDFLAGS_WINDOW)
AC_SUBST(AR)
AC_SUBST(RANLIB)
AC_SUBST(TK_RES)

AC_SUBST(STLIB_LD)
AC_SUBST(SHLIB_LD)
AC_SUBST(SHLIB_LD_LIBS)
AC_SUBST(SHLIB_CFLAGS)
AC_SUBST(SHLIB_SUFFIX)
AC_SUBST(TK_SHARED_BUILD)

AC_SUBST(LIBS)
AC_SUBST(LIBS_GUI)
AC_SUBST(DLLSUFFIX)
AC_SUBST(LIBPREFIX)
AC_SUBST(LIBSUFFIX)
AC_SUBST(EXESUFFIX)
AC_SUBST(LIBRARIES)
AC_SUBST(MAKE_LIB)
AC_SUBST(MAKE_STUB_LIB)
AC_SUBST(POST_MAKE_LIB)
AC_SUBST(MAKE_DLL)
AC_SUBST(MAKE_EXE)

AC_SUBST(TK_LIB_FLAG)
AC_SUBST(TK_LIB_SPEC)
AC_SUBST(TK_BUILD_LIB_SPEC)
AC_SUBST(TK_STUB_LIB_SPEC)
AC_SUBST(TK_STUB_LIB_PATH)
AC_SUBST(TK_BUILD_STUB_LIB_PATH)

# undefined at this point for win
AC_SUBST(TK_CC_SEARCH_FLAGS)
AC_SUBST(TK_LD_SEARCH_FLAGS)

AC_SUBST(RC)
AC_SUBST(RC_OUT)
AC_SUBST(RC_TYPE)
AC_SUBST(RC_INCLUDE)
AC_SUBST(RC_DEFINE)
AC_SUBST(RC_DEFINES)
AC_SUBST(RES)

AC_CONFIG_FILES([Makefile tkConfig.sh wish.exe.manifest])
AC_OUTPUT

dnl Local Variables:
dnl mode: autoconf
dnl End:
