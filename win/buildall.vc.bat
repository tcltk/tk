@echo off
if "%MSVCDir%" == "" call c:\dev\devstudio60\vc98\bin\vcvars32.bat

set INSTALLDIR=C:\tclTestArea
set TCLDIR=..\..\tcl_head

nmake -nologo -f makefile.vc release
nmake -nologo -f makefile.vc release OPTS=static
nmake -nologo -f makefile.vc core    OPTS=static,msvcrt
nmake -nologo -f makefile.vc release OPTS=static,threads
nmake -nologo -f makefile.vc core    OPTS=static,msvcrt,threads
nmake -nologo -f makefile.vc release OPTS=threads
pause
