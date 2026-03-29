@echo off
setlocal

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-manager-driver.ps1"
exit /b %ERRORLEVEL%
