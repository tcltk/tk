@echo off
rem RCS: @(#) $Id: mkd.bat,v 1.3.20.1 2002/04/02 21:17:03 hobbs Exp $

if exist %1\nul goto end

md %1
if errorlevel 1 goto end

echo Created directory %1

:end


