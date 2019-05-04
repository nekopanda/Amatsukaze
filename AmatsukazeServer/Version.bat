@echo off
for /f "delims=" %%A in ('git describe --tags') do set VER=%%A
echo ^<#>%1
echo string version="%VER%"; >>%1
echo #^>>>%1
