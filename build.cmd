<# :
@echo off
rem Batch part: runs the rest of this file as PowerShell (to cmd the first line is a label,
rem to PowerShell this block is a comment), so no execution policy change is needed.
pushd "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "& ([scriptblock]::Create((Get-Content -Raw -LiteralPath '%~f0'))) %*"
set "result=%errorlevel%"
popd
exit /b %result%
#>

# Builds rawPSense with the MSYS2 UCRT64 toolchain and puts a portable copy (exe + Qt plugins + DLLs)
# in dist\rawPSense. Run from cmd, PowerShell or by double-clicking; no MSYS2 shell needed.
#
#   build.cmd            incremental build (closes a running rawPSense; dist\ is always erased and recreated)
#   build.cmd -Clean     also wipe build\ first
#
# MSYS2 is expected in C:\msys64; set MSYS2_ROOT if it lives elsewhere.
param([switch]$Clean)

$ErrorActionPreference = 'Stop'

function Run([string]$exe) {
    & $exe @args
    if ($LASTEXITCODE) { throw "$exe failed (exit code $LASTEXITCODE)" }
}

# Started by double-clicking: cmd's parent is Explorer, and the window would close before the output can be read
function StartedFromExplorer {
    $cmd = Get-CimInstance Win32_Process -Filter "ProcessId = $((Get-CimInstance Win32_Process -Filter "ProcessId = $PID").ParentProcessId)"
    $parent = Get-CimInstance Win32_Process -Filter "ProcessId = $($cmd.ParentProcessId)"
    return $parent -and $parent.Name -eq 'explorer.exe'
}

$failed = $false
try {
    $msys = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }
    $bin = Join-Path $msys 'ucrt64\bin'
    foreach ($tool in 'cmake', 'ninja', 'g++', 'objdump', 'windeployqt-qt5') {
        if (-not (Test-Path (Join-Path $bin "$tool.exe"))) {
            throw "$tool not found in $bin. In the MSYS2 UCRT64 shell run:`n" +
                  "  pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt5-base,qt5-tools}"
        }
    }
    $env:PATH = "$bin;$env:PATH"

    if ($Clean -and (Test-Path build)) {
        Remove-Item build -Recurse -Force
    }

    # A running rawPSense locks its exe and DLLs in dist\ and build\, so close it first. It runs
    # elevated, so stopping it from an unelevated build needs an elevated taskkill (one UAC prompt).
    $running = @(Get-Process rawPSense -ErrorAction SilentlyContinue)
    if ($running) {
        Write-Host "Closing the running rawPSense..."
        try {
            $running | Stop-Process -Force -ErrorAction Stop
        } catch {
            try {
                Start-Process taskkill -ArgumentList '/F /IM rawPSense.exe' -Verb RunAs -WindowStyle Hidden -Wait
            } catch {
                throw "Cannot close the running rawPSense ($($_.Exception.Message)). Close it from the tray and try again."
            }
        }
        $running | ForEach-Object { $_.WaitForExit(10000) | Out-Null }
    }

    # Erase the previous build up front, so a failed build never leaves an old copy looking current
    if (Test-Path dist) {
        try {
            Remove-Item dist -Recurse -Force
        } catch {
            throw "Cannot erase the previous build in dist\ ($($_.Exception.Message)). Close rawPSense if it's running from there and try again."
        }
    }

    Run cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    Run cmake --build build

    $dist = Join-Path (Get-Location) 'dist\rawPSense'
    New-Item -ItemType Directory $dist | Out-Null
    Copy-Item build\rawPSense.exe $dist

    Run windeployqt-qt5 --release --no-translations --no-angle --no-opengl-sw --verbose 0 (Join-Path $dist 'rawPSense.exe')

    # windeployqt only copies Qt itself; also copy every MSYS2 DLL that anything in dist imports
    # (libstdc++, zlib, icu, ...), following imports recursively. System DLLs aren't in $bin, so they're skipped.
    $queue = New-Object System.Collections.Generic.Queue[string]
    Get-ChildItem $dist -Recurse -Include *.exe, *.dll | ForEach-Object { $queue.Enqueue($_.FullName) }
    while ($queue.Count) {
        foreach ($line in & objdump -p $queue.Dequeue()) {
            if ($line -notmatch 'DLL Name: (\S+)') { continue }
            $source = Join-Path $bin $Matches[1]
            $target = Join-Path $dist $Matches[1]
            if ((Test-Path $source) -and -not (Test-Path $target)) {
                Copy-Item $source $target
                $queue.Enqueue($target)
            }
        }
    }

    $count = (Get-ChildItem $dist -Recurse -File).Count
    Write-Host "Built $dist\rawPSense.exe ($count files). The folder is self-contained and can be copied anywhere."
} catch {
    $failed = $true
    Write-Host "Build failed: $_" -ForegroundColor Red
} finally {
    if (StartedFromExplorer) {
        Write-Host 'Press Enter to close.'
        [void][Console]::ReadLine()
    }
}
if ($failed) { exit 1 }
