@echo off
REM ============================================================
REM  MDC firmware dev kit build script (Windows CMD)
REM  Usage: build.bat [clean]
REM  Output: build\mdc_devkit.bin / .hex / .elf
REM  NOTE: keep this file pure ASCII (cmd parses as ANSI/GBK)
REM ============================================================
setlocal
cd /d "%~dp0"

REM --- Check toolchain ---
where arm-none-eabi-gcc >nul 2>&1
if %errorlevel% neq 0 (
    echo [FAIL] arm-none-eabi-gcc not found. Install ARM GNU toolchain and add to PATH.
    goto :end
)
echo [INFO] Toolchain OK

REM --- Detect make command ---
set "MAKE=make"
where mingw32-make >nul 2>&1 && set "MAKE=mingw32-make"

if "%~1"=="clean" goto :clean
goto :build

:clean
echo [INFO] Cleaning...
"%MAKE%" clean
goto :end

:build
echo [INFO] Building with %MAKE% ...
"%MAKE%" -j8 all
if %errorlevel% neq 0 (
    echo [FAIL] Build failed.
    goto :end
)
echo [OK] BUILD SUCCESS: build\mdc_devkit.bin / .hex / .elf

:end
endlocal
