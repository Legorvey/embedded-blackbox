#Requires -Version 5.1
<#
.SYNOPSIS
    Automated build & verification runner for `blackbox_storage`.
.DESCRIPTION
    Runs all verification steps feasible on the local machine:
    1. Cross-compiles `components/blackbox_storage/blackbox_storage.c` and `main/main.c`
       for target `esp32s3` using `xtensa-esp32s3-elf-gcc` (GCC 14.2.0) and the local
       ESP-IDF v5.4 (`idf-release_v5.4-2f7dcd86-v1`) SDK headers with `-Wall -Wextra -Werror`.
    2. Compiles and executes the host-side unit and fault-injection test suite
       (`tests/test_blackbox_storage_host.c`) using `BB_STORAGE_HOST_TEST`.
#>

$ErrorActionPreference = "Stop"
$ProjRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjRoot "build_test_host"
$SdSimDir = Join-Path $ProjRoot "build_test_sdcard"

# Clean stale temporary directories before starting
if (Test-Path $SdSimDir) { Remove-Item -Recurse -Force $SdSimDir }
if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

try {
    Write-Host "============================================================"
    Write-Host " STEP 1: ESP32-S3 Cross-Compilation Check (ESP-IDF v5.4 SDK)"
    Write-Host "============================================================"

    $S3Sdk = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esp32-arduino-libs\idf-release_v5.4-2f7dcd86-v1\esp32s3"
    $XtensaGcc = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esp-x32\2411\bin\xtensa-esp32s3-elf-gcc.exe"

    if ((Test-Path $XtensaGcc) -and (Test-Path $S3Sdk)) {
        & $XtensaGcc --version | Select-Object -First 1
        $S3SdkFwd = $S3Sdk.Replace('\', '/')
        $ProjRootFwd = $ProjRoot.Replace('\', '/')
        $BuildDirFwd = $BuildDir.Replace('\', '/')

        & $XtensaGcc "-iprefix" "$S3SdkFwd/include/" `
            "@$S3SdkFwd/flags/includes" `
            "@$S3SdkFwd/flags/c_flags" `
            "@$S3SdkFwd/flags/defines" `
            "-I$S3SdkFwd/qio_qspi/include" `
            "-I$ProjRootFwd/components/blackbox_storage/include" `
            -Wall -Wextra -Werror `
            -c "$ProjRootFwd/components/blackbox_storage/blackbox_storage.c" `
            -o "$BuildDirFwd/blackbox_storage_esp32s3.o"
        if ($LASTEXITCODE -ne 0) { throw "ESP32-S3 cross-compile of blackbox_storage.c failed" }

        & $XtensaGcc "-iprefix" "$S3SdkFwd/include/" `
            "@$S3SdkFwd/flags/includes" `
            "@$S3SdkFwd/flags/c_flags" `
            "@$S3SdkFwd/flags/defines" `
            "-I$S3SdkFwd/qio_qspi/include" `
            "-I$ProjRootFwd/components/blackbox_storage/include" `
            -Wall -Wextra -Werror `
            -c "$ProjRootFwd/main/main.c" `
            -o "$BuildDirFwd/main_esp32s3.o"
        if ($LASTEXITCODE -ne 0) { throw "ESP32-S3 cross-compile of main.c failed" }

        & $XtensaGcc "-I$ProjRootFwd/components/blackbox_storage/include" `
            -DBB_STORAGE_HOST_TEST=1 `
            -std=gnu17 -Wall -Wextra -Werror `
            -c "$ProjRootFwd/tests/test_blackbox_storage_host.c" `
            -o "$BuildDirFwd/test_blackbox_storage_host_check.o"
        if ($LASTEXITCODE -ne 0) { throw "GCC strict check of test_blackbox_storage_host.c failed" }

        Get-Item "$BuildDir\blackbox_storage_esp32s3.o", "$BuildDir\main_esp32s3.o", "$BuildDir\test_blackbox_storage_host_check.o" | Format-Table Name, Length -AutoSize
        Write-Host "[PASS] ESP32-S3 cross-compilation against ESP-IDF v5.4 headers succeeded with 0 warnings."
    } else {
        Write-Host "[SKIP] xtensa-esp32s3-elf-gcc or ESP-IDF v5.4 SDK headers not found at default path."
    }

    Write-Host ""
    Write-Host "============================================================"
    Write-Host " STEP 2: Host Unit & Fault-Injection Test Suite"
    Write-Host "============================================================"

    $HostSuitesExecuted = 0

    $MinGwGcc = "C:\Universitas Padjadjaran\Dev-Cpp\MinGW64\bin\gcc.exe"
    if (-not (Test-Path $MinGwGcc)) {
        $GccCmd = Get-Command gcc.exe -ErrorAction SilentlyContinue
        if ($GccCmd) { $MinGwGcc = $GccCmd.Source }
    }

    if (Test-Path $MinGwGcc) {
        Write-Host "[HOST] Running strict C11 build via MinGW GCC..."
        & $MinGwGcc --version | Select-Object -First 1
        & $MinGwGcc -std=gnu11 -Wall -Wextra -Werror `
            -DBB_STORAGE_HOST_TEST=1 `
            -I"$ProjRoot\components\blackbox_storage\include" `
            "$ProjRoot\components\blackbox_storage\blackbox_storage.c" `
            "$ProjRoot\tests\test_blackbox_storage_host.c" `
            -o "$BuildDir\test_blackbox_storage_gcc.exe"
        if ($LASTEXITCODE -ne 0) { throw "MinGW GCC build failed" }

        Push-Location $ProjRoot
        try {
            & "$BuildDir\test_blackbox_storage_gcc.exe"
            if ($LASTEXITCODE -ne 0) { throw "MinGW GCC host unit test suite reported failures" }
        } finally {
            Pop-Location
        }
        $HostSuitesExecuted++
    }

    $LccDir = "C:\Program Files\MATLAB\R2026a\sys\lcc64\lcc64"
    if (Test-Path "$LccDir\bin\lcc64.exe") {
        Write-Host ""
        Write-Host "[HOST] Running compatibility build via MATLAB lcc64..."
        & "$LccDir\bin\lcc64.exe" -noregistrylookup `
            -DBB_STORAGE_HOST_TEST=1 `
            -I"$LccDir\include64" `
            -I"$ProjRoot\components\blackbox_storage\include" `
            -Fo"$BuildDir\blackbox_storage.obj" `
            "$ProjRoot\components\blackbox_storage\blackbox_storage.c"
        if ($LASTEXITCODE -ne 0) { throw "lcc64 compile of blackbox_storage.c failed" }

        & "$LccDir\bin\lcc64.exe" -noregistrylookup `
            -DBB_STORAGE_HOST_TEST=1 `
            -I"$LccDir\include64" `
            -I"$ProjRoot\components\blackbox_storage\include" `
            -Fo"$BuildDir\test_blackbox_storage_host.obj" `
            "$ProjRoot\tests\test_blackbox_storage_host.c"
        if ($LASTEXITCODE -ne 0) { throw "lcc64 compile of test_blackbox_storage_host.c failed" }

        & "$LccDir\bin\lcclnk64.exe" -subsystem console `
            -L"$LccDir\lib64" `
            -o "$BuildDir\test_blackbox_storage_lcc.exe" `
            "$BuildDir\blackbox_storage.obj" `
            "$BuildDir\test_blackbox_storage_host.obj"
        if ($LASTEXITCODE -ne 0) { throw "lcclnk64 link failed" }

        Push-Location $ProjRoot
        try {
            & "$BuildDir\test_blackbox_storage_lcc.exe"
            if ($LASTEXITCODE -ne 0) { throw "lcc64 host unit test suite reported failures" }
        } finally {
            Pop-Location
        }
        $HostSuitesExecuted++
    }

    if ($HostSuitesExecuted -eq 0) {
        throw "No host C compiler (MinGW GCC or lcc64) found; host tests were not executed."
    }
} finally {
    # Clean up temporary build/test directories even on failure
    if (Test-Path $SdSimDir) { Remove-Item -Recurse -Force $SdSimDir }
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
}
