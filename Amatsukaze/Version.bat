@echo off
for /f "delims=" %%A in ('git describe --tags') do set VER=%%A
echo #define AMATSUKAZE_VERSION "%VER%">Version.h
