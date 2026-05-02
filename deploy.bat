@echo off
setlocal

set "ROOT=%~dp0"
set "GAME_DIR=C:\Program Files (x86)\Steam\steamapps\common\RESIDENT EVIL REVELATIONS 2"
set "SCRIPT_DIR=%GAME_DIR%\scripts"
set "BUILD_DIR=%ROOT%build"
set "POWERSHELL_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

if not exist "%BUILD_DIR%\BioPatch.asi" (
    echo Missing %BUILD_DIR%\BioPatch.asi
    echo Run build.bat first.
    exit /b 1
)

if not exist "%SCRIPT_DIR%" mkdir "%SCRIPT_DIR%"

if not exist "%POWERSHELL_EXE%" (
    echo Missing PowerShell executable: %POWERSHELL_EXE%
    exit /b 1
)

"%POWERSHELL_EXE%" -NoProfile -Command ^
    "$hash = { param([string]$Path) " ^
    "  $stream = [System.IO.File]::OpenRead($Path); " ^
    "  try { " ^
    "    $sha = [System.Security.Cryptography.SHA256]::Create(); " ^
    "    try { return ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', ''); } " ^
    "    finally { $sha.Dispose() } " ^
    "  } finally { $stream.Dispose() } " ^
    "}; " ^
    "$legacyData = Join-Path '%SCRIPT_DIR%' 'REPatch'; " ^
    "$data = Join-Path '%SCRIPT_DIR%' 'BioPatch'; " ^
    "if ((Test-Path -LiteralPath $legacyData) -and -not (Test-Path -LiteralPath $data)) { Move-Item -LiteralPath $legacyData -Destination $data } " ^
    "Copy-Item -LiteralPath '%BUILD_DIR%\BioPatch.asi' -Destination '%SCRIPT_DIR%\BioPatch.asi' -Force; " ^
    "$src = & $hash '%BUILD_DIR%\BioPatch.asi'; " ^
    "$dst = & $hash '%SCRIPT_DIR%\BioPatch.asi'; " ^
    "if ($src -ne $dst) { throw 'BioPatch.asi hash mismatch after copy.' } " ^
    "if (Test-Path -LiteralPath '%SCRIPT_DIR%\\REPatch.asi') { Remove-Item -LiteralPath '%SCRIPT_DIR%\\REPatch.asi' -Force } " ^
    "if (Test-Path -LiteralPath '%SCRIPT_DIR%\\REPatch.pdb') { Remove-Item -LiteralPath '%SCRIPT_DIR%\\REPatch.pdb' -Force } " ^
    "if (Test-Path -LiteralPath '%SCRIPT_DIR%\\REPatch.ini') { Remove-Item -LiteralPath '%SCRIPT_DIR%\\REPatch.ini' -Force }" || (
    echo Failed to deploy BioPatch.asi
    exit /b 1
)

"%POWERSHELL_EXE%" -NoProfile -Command ^
    "Copy-Item -LiteralPath '%ROOT%BioPatch.ini' -Destination '%SCRIPT_DIR%\BioPatch.ini' -Force" || (
    echo Failed to copy BioPatch.ini
    exit /b 1
)

if exist "%BUILD_DIR%\BioPatch.pdb" (
    "%POWERSHELL_EXE%" -NoProfile -Command ^
        "Copy-Item -LiteralPath '%BUILD_DIR%\BioPatch.pdb' -Destination '%SCRIPT_DIR%\BioPatch.pdb' -Force" || (
        echo Failed to copy BioPatch.pdb
        exit /b 1
    )
)

echo Deployed BioPatch to:
echo   %SCRIPT_DIR%
