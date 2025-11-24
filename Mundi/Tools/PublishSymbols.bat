@echo off
setlocal

:: ========================================================
:: [Setup 0] Check Configuration
:: ========================================================
set CONFIG=%1

if "%CONFIG%"=="" (
    echo [SymStore] Configuration argument is missing. Skipping symbol upload.
    exit /b 0
)

:: ========================================================
:: [Setup 1] Path to symstore.exe
:: ========================================================
set TOOL_DIR=%~dp0SymStore

:: ========================================================
:: [Setup 2] Build Output Directory (Relative Path)
:: ========================================================
set BIN_DIR=%~dp0..\..\Binaries\%CONFIG%

:: ========================================================
:: [Setup 3] Symbol Server Path & IP Check (with timeout)
:: ========================================================
set SERVER_IP=172.21.11.109
set STORE_PATH=\\%SERVER_IP%\SymbolStore

echo [SymStore] Checking server availability (timeout: 3 seconds)...

:: Create temporary PowerShell script for timeout check
set "PS_SCRIPT=%TEMP%\check_symstore_%RANDOM%.ps1"

(
echo $ErrorActionPreference = 'Stop'
echo $timeout = 3
echo try {
echo     $result = Test-Path -Path '%STORE_PATH%' -PathType Container -ErrorAction Stop
echo     if ($result^) { exit 0 } else { exit 1 }
echo } catch {
echo     exit 1
echo }
) > "%PS_SCRIPT%"

:: Run PowerShell script with timeout
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%"
set RESULT=%ERRORLEVEL%

:: Cleanup
del "%PS_SCRIPT%" 2>nul

if %RESULT% NEQ 0 (
    echo.
    echo [Warning] Cannot access Symbol Store path: "%STORE_PATH%"
    echo           Server might be unreachable or you lack permissions.
    echo           Skipping symbol upload.
    exit /b 0
)

echo [SymStore] Server is accessible.

:: ========================================================
:: [Setup 4] Version Tag (Auto-generated from Date_Time)
:: ========================================================
set "NOW_TIME=%time: =0%"
set VERSION_TAG=Build_%CONFIG%_%date:~0,4%-%date:~5,2%-%date:~8,2%_%NOW_TIME:~0,2%%NOW_TIME:~3,2%

echo.
echo ========================================================
echo   Target Config : %CONFIG%
echo   Source Path   : %BIN_DIR%
echo   Target Server : %STORE_PATH%
echo ========================================================

:: Validate Path
if not exist "%BIN_DIR%" (
    echo [Error] Directory "%BIN_DIR%" not found!
    exit /b 1
)

echo [SymStore] Starting upload...

:: 1. Upload .pdb files
"%TOOL_DIR%\symstore.exe" add /f "%BIN_DIR%\*.pdb" /s "%STORE_PATH%" /t "MundiEngine" /v "%VERSION_TAG%" /r /o

:: 2. Upload .exe files
"%TOOL_DIR%\symstore.exe" add /f "%BIN_DIR%\*.exe" /s "%STORE_PATH%" /t "MundiEngine" /v "%VERSION_TAG%" /r /o

:: 3. Upload .dll files
"%TOOL_DIR%\symstore.exe" add /f "%BIN_DIR%\*.dll" /s "%STORE_PATH%" /t "MundiEngine" /v "%VERSION_TAG%" /r /o

echo.
echo [Success] Uploaded symbols for %CONFIG% version.