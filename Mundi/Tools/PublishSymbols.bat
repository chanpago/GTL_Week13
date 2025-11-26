@echo off
setlocal enabledelayedexpansion

:: ========================================================
:: [Setup 0] Check Configuration
:: ========================================================
set CONFIG=%1

if "%CONFIG%"=="" (
    echo [SymStore] Configuration argument is missing. Skipping symbol upload.
    exit /b 0
)

:: ========================================================
:: [Setup 1] Path Setup
:: ========================================================
:: Directory containing symstore.exe and pdbstr.exe
set TOOL_DIR=%~dp0SymStore
:: Target binary directory to process
set BIN_DIR=%~dp0..\..\Binaries\%CONFIG%

:: Path to pdbstr.exe
set PDBSTR_EXE=%TOOL_DIR%\pdbstr.exe

:: ========================================================
:: [Setup 2] Git Information Extraction
:: ========================================================
echo [Git] Extracting version info...

for /f "delims=" %%i in ('git rev-parse --short HEAD') do set GIT_HASH=%%i
for /f "delims=" %%i in ('git remote get-url origin') do set GIT_URL_RAW=%%i

:: Remove .git extension from URL to support raw content access
set GIT_URL=%GIT_URL_RAW:.git=%

if "%GIT_HASH%"=="" set GIT_HASH=NoGit
if "%GIT_URL%"=="" set GIT_URL=UnknownRepo

echo [Git] Current Commit: %GIT_HASH%
echo [Git] Repository: %GIT_URL%

:: ========================================================
:: [Setup 3] Source Indexing
:: Inject source repository info into PDB files.
:: ========================================================
REM 1. Get Git Root Path (CORRECTED: Up 2 levels only)
REM Assuming batch is in Mundi/Tools/ or similar depth
pushd "%~dp0..\.."
set "REPO_ROOT=%CD%"
popd

REM 2. Get Project Directory (Where to search for sources)
pushd "%~dp0..\.."
set "SEARCH_DIR=%CD%"
popd

if exist "%PDBSTR_EXE%" (
    echo [SourceIndexing] Injecting source info into PDBs...
    
    set SRCSRV_INI=%TEMP%\srcsrv_%GIT_HASH%.ini
    
    REM Write srcsrv.ini Header
    (
        echo SRCSRV: variables ------------------------------------------
        echo VERSION=1
        echo VERCTRL=http
        echo SRCSRV: variables ------------------------------------------
        echo MYSERVER=%GIT_URL%/raw/%GIT_HASH%
        echo SRCSRV: source files ---------------------------------------
    ) > "!SRCSRV_INI!"

    REM 3. Iterate through all source files
    for /r "%SEARCH_DIR%" %%f in (*.cpp *.h *.inl) do (
        set "FULL_PATH=%%f"
        
        REM Calculate relative path by removing the Repo Root path
        REM Before: ...\GTL_Week12\Mundi\... -> \GTL_Week12\Mundi\... (WRONG)
        REM Now:    ...\GTL_Week12\Mundi\... -> \Mundi\... (CORRECT)
        set "REL_PATH=!FULL_PATH:%REPO_ROOT%=!"
        
        REM Convert backslashes to forward slashes for the URL
        set "URL_PATH=!REL_PATH:\=/!"
        
        REM Write mapping
        echo !FULL_PATH!*MYSERVER!URL_PATH! >> "!SRCSRV_INI!"
    )

    REM Inject the generated map into PDB files
    for %%f in ("%BIN_DIR%\*.pdb") do (
        "%PDBSTR_EXE%" -w -p:"%%f" -i:"!SRCSRV_INI!" -s:srcsrv
    )
    
    echo [SourceIndexing] Done.
) else (
    echo [Warning] pdbstr.exe not found in TOOL_DIR. Skipping Source Indexing.
)

:: ========================================================
:: [Setup 4] Symbol Server Connection Check
:: ========================================================
set SERVER_IP=172.21.11.109
set STORE_PATH=\\%SERVER_IP%\SymbolStore

echo [SymStore] Checking server availability...

:: Check Port 445 (SMB) with 3000ms timeout
powershell -Command "$t = New-Object System.Net.Sockets.TcpClient; $r = $t.BeginConnect('%SERVER_IP%', 445, $null, $null); if ($r.AsyncWaitHandle.WaitOne(3000)) { exit 0 } else { exit 1 }"

if errorlevel 1 (
    echo [Warning] Server connection timeout or unreachable.
    exit /b 0
)

if not exist "%STORE_PATH%" (
    echo [Warning] Server reachable but path "%STORE_PATH%" not found.
    exit /b 0
)

:: ========================================================
:: [Setup 5] Upload to Symbol Server
:: ========================================================
set "NOW_TIME=%time: =0%"
:: Version Tag: Includes Config, GitHash, and Timestamp
set VERSION_TAG=Build_%CONFIG%_%GIT_HASH%_%date:~0,4%-%date:~5,2%-%date:~8,2%_%NOW_TIME:~0,2%%NOW_TIME:~3,2%

echo ========================================================
echo    Target Config : %CONFIG%
echo    Git Commit    : %GIT_HASH%
echo    Target Server : %STORE_PATH%
echo    Version Tag   : %VERSION_TAG%
echo ========================================================

if not exist "%BIN_DIR%" (
    echo [Error] Directory "%BIN_DIR%" not found!
    exit /b 1
)

echo [SymStore] Starting upload...

:: Add Git info to the transaction comment
set COMMENT_MSG="Git: %GIT_HASH% (%GIT_URL%)"

:: 1. Upload .pdb files
"%TOOL_DIR%\symstore.exe" add /f "%BIN_DIR%\*.pdb" /s "%STORE_PATH%" /t "MundiEngine" /v "%VERSION_TAG%" /c %COMMENT_MSG% /r /o

:: 2. Upload .exe files
"%TOOL_DIR%\symstore.exe" add /f "%BIN_DIR%\*.exe" /s "%STORE_PATH%" /t "MundiEngine" /v "%VERSION_TAG%" /c %COMMENT_MSG% /r /o

:: 3. Upload .dll files
"%TOOL_DIR%\symstore.exe" add /f "%BIN_DIR%\*.dll" /s "%STORE_PATH%" /t "MundiEngine" /v "%VERSION_TAG%" /c %COMMENT_MSG% /r /o

echo.
echo [Success] Uploaded symbols for %CONFIG% version with Git Info.