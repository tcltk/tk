@echo off
rem RCS: @(#) $Id: rmd.bat,v 1.3.20.1 2002/04/02 21:17:03 hobbs Exp $

if not exist %1\nul goto end

echo Removing directory %1

if "%OS%" == "Windows_NT" goto winnt

deltree /y %1
if errorlevel 1 goto end
goto success

:winnt
rmdir /s /q %1
if errorlevel 1 goto end

:success
echo Deleted directory %1

:end
