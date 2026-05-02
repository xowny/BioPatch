@echo off
setlocal

set "ROOT=%~dp0"
set "VSDEVCMD=C:\\PROGRA~2\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
set "BUILD_DIR=%ROOT%build"

if not exist "%VSDEVCMD%" (
    echo Could not find VsDevCmd.bat at:
    echo   %VSDEVCMD%
    exit /b 1
)

call "%VSDEVCMD%" -arch=x86 -host_arch=x64 >nul
if errorlevel 1 (
    echo Failed to initialize the Visual Studio build environment.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

pushd "%BUILD_DIR%"
cl /nologo /std:c++20 /EHsc /MD /O2 /Oi /Gy /LD ^
  /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
  "%ROOT%src\main.cpp" ^
  /link /OUT:BioPatch.asi /PDB:BioPatch.pdb /OPT:REF /OPT:ICF dbghelp.lib user32.lib
set "ERR=%ERRORLEVEL%"
popd

exit /b %ERR%
