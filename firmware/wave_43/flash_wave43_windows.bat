@echo off
set PORT=%1
if "%PORT%"=="" set PORT=COM3

python -m esptool --chip esp32p4 -p %PORT% -b 115200 ^
  --before default_reset --after hard_reset write_flash 0x0 ^
  "%~dp0kernsigner-wave43-0.0.7-rc1-untested-full.bin"
