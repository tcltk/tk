@echo off
rem RCS: @(#) $Id: mkd.bat,v 1.4.2.1 2002/06/10 05:38:27 wolfsuit Exp $

if exist %1\nul goto end

md %1
if errorlevel 1 goto end

echo Created directory %1

:end


