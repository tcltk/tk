@echo off

::  This is an example batchfile for building everything. Please
::  edit this (or make your own) for your needs and wants using
::  the instructions for calling makefile.vc found in makefile.vc
::
::  RCS: @(#) $Id: buildall.vc.bat,v 1.2.2.2 2002/06/10 05:38:27 wolfsuit Exp $

echo Sit back and have a cup of coffee while this grinds through ;)
echo You asked for *everything*, remember?
echo.

if "%MSVCDir%" == "" call C:\dev\devstudio60\vc98\bin\vcvars32.bat
set INSTALLDIR=C:\progra~1\tcl
set TCLDIR=..\..\tcl_head

nmake -nologo -f makefile.vc release winhelp OPTS=none
if errorlevel 1 goto error
nmake -nologo -f makefile.vc release OPTS=static,linkexten
if errorlevel 1 goto error
nmake -nologo -f makefile.vc core OPTS=static,msvcrt
if errorlevel 1 goto error
nmake -nologo -f makefile.vc core OPTS=static,threads
if errorlevel 1 goto error
nmake -nologo -f makefile.vc core OPTS=static,msvcrt,threads
if errorlevel 1 goto error
nmake -nologo -f makefile.vc release OPTS=threads
if errorlevel 1 goto error
goto end

:error
echo *** BOOM! ***

:end
echo done!
pause
