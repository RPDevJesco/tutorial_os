@echo off
REM Tutorial-OS Build Script for Windows
REM
REM This script wraps Docker commands for easy building.
REM
REM Usage:
REM   build.bat              - Build all boards
REM   build.bat all          - Build all boards
REM   build.bat rpi-zero2w-gpi   - Build specific board
REM   build.bat shell        - Interactive shell in container
REM   build.bat clean        - Remove output directory
REM   build.bat rebuild      - Rebuild Docker image and build all

setlocal EnableDelayedExpansion

REM =============================================================================
REM Configuration
REM =============================================================================

set IMAGE_NAME=tutorial-os-builder
set CONTAINER_NAME=tutorial-os-build
set OUTPUT_DIR=output

REM =============================================================================
REM Check Docker
REM =============================================================================

where docker >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Docker is not installed or not in PATH.
    echo         Please install Docker Desktop for Windows first.
    exit /b 1
)

REM =============================================================================
REM Parse Arguments
REM =============================================================================

if "%1"=="" goto :build_all
if "%1"=="all" goto :build_all
if "%1"=="help" goto :show_help
if "%1"=="-h" goto :show_help
if "%1"=="--help" goto :show_help
if "%1"=="clean" goto :clean
if "%1"=="shell" goto :shell
if "%1"=="bash" goto :shell
if "%1"=="rebuild" goto :rebuild
if "%1"=="image" goto :build_image_force
goto :build_specific

REM =============================================================================
REM Commands
REM =============================================================================

:show_help
echo Tutorial-OS Build Script for Windows
echo.
echo Usage: build.bat [command] [options]
echo.
echo Commands:
echo   all                    Build all boards (default)
echo   ^<board^>                Build specific board(s)
echo   shell                  Start interactive shell in container
echo   clean                  Remove output directory
echo   rebuild                Rebuild Docker image and build all
echo   image                  Build Docker image only
echo   help                   Show this help message
echo.
echo Available boards:
echo   - rpi-zero2w-gpi       Raspberry Pi Zero 2W + GPi Case
echo   - rpi-cm4-io           Raspberry Pi CM4 + IO Board
echo   - radxa-rock2a         Radxa Rock 2A
echo.
echo Examples:
echo   build.bat                          Build all boards
echo   build.bat rpi-zero2w-gpi           Build single board
echo   build.bat rpi-zero2w-gpi rpi-cm4-io    Build multiple boards
echo   build.bat shell                    Debug in container
echo.
echo Output structure:
echo   output\
echo   +-- rpi-zero2w-gpi\
echo   ^|   +-- kernel8.img
echo   ^|   +-- kernel.elf
echo   ^|   +-- kernel.list
echo   ^|   +-- boot\
echo   ^|       +-- config.txt
echo   +-- rpi-cm4-io\
echo   ^|   +-- ...
echo   +-- radxa-rock2a\
echo       +-- Image
echo       +-- boot\
echo           +-- extlinux\
goto :eof

:clean
echo [INFO] Removing output directory...
if exist "%OUTPUT_DIR%" (
    rmdir /s /q "%OUTPUT_DIR%"
    echo [SUCCESS] Output directory removed
) else (
    echo [INFO] Output directory doesn't exist
)
goto :eof

:shell
call :build_image
if %ERRORLEVEL% neq 0 exit /b 1

echo [INFO] Starting interactive shell...
docker run --rm -it ^
    -v "%cd%\%OUTPUT_DIR%:/output" ^
    --name %CONTAINER_NAME% ^
    %IMAGE_NAME% ^
    bash
goto :eof

:rebuild
call :clean
call :build_image_force
if %ERRORLEVEL% neq 0 exit /b 1
call :run_build all
goto :show_summary

:build_image_force
echo [INFO] Building Docker image: %IMAGE_NAME%
docker build -t %IMAGE_NAME% .
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to build Docker image
    exit /b 1
)
echo [SUCCESS] Docker image built successfully
goto :eof

:build_image
REM Check if image exists
docker image inspect %IMAGE_NAME% >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [INFO] Docker image not found. Building...
    call :build_image_force
)
goto :eof

:build_all
call :build_image
if %ERRORLEVEL% neq 0 exit /b 1
call :run_build all
goto :show_summary

:build_specific
call :build_image
if %ERRORLEVEL% neq 0 exit /b 1

REM Pass all arguments to the build
set BOARDS=
:arg_loop
if "%1"=="" goto :run_specific
set BOARDS=%BOARDS% %1
shift
goto :arg_loop

:run_specific
call :run_build %BOARDS%
goto :show_summary

:run_build
REM Create output directory
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo [INFO] Starting build...
docker run --rm ^
    -v "%cd%\%OUTPUT_DIR%:/output" ^
    --name %CONTAINER_NAME% ^
    %IMAGE_NAME% ^
    %*
goto :eof

:show_summary
echo.
echo [SUCCESS] Build complete! Output files:
if exist "%OUTPUT_DIR%" (
    for /r "%OUTPUT_DIR%" %%f in (*) do (
        echo   %%f
    )
)
goto :eof