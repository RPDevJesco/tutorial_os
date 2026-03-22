@echo off
setlocal EnableDelayedExpansion
:: =============================================================================
:: build.bat — Tutorial-OS Unified Build Script (Windows)
:: =============================================================================
::
:: Wraps Docker to provide the same build experience as build.sh on Linux/macOS.
:: All cross-compilation toolchains (ARM64, RISC-V, x86_64, ARM32) and Rust
:: nightly live inside the Docker container — nothing to install locally.
::
:: Usage:
::   build.bat help                           Show this help
::   build.bat rpi-zero2w-gpi                 C build (default), release
::   build.bat rpi-zero2w rust                Rust build, release
::   build.bat milkv-mars rust debug          Rust build, debug
::   build.bat all                            All boards, C
::   build.bat all rust                       All boards, Rust
::   build.bat shell                          Interactive shell in container
::   build.bat clean                          Remove build artifacts
::   build.bat rebuild                        Rebuild Docker image from scratch
::   build.bat image                          Build Docker image only
::
:: Requires: Docker Desktop for Windows
:: =============================================================================

set "IMAGE_NAME=tutorial-os-builder"

:: =============================================================================
:: No arguments — show help
:: =============================================================================

if "%~1"=="" goto :usage

:: =============================================================================
:: Parse first argument
:: =============================================================================

if "%~1"=="help"   goto :usage
if "%~1"=="-h"     goto :usage
if "%~1"=="--help" goto :usage
if "%~1"=="clean"  goto :clean
if "%~1"=="shell"  goto :shell
if "%~1"=="bash"   goto :shell
if "%~1"=="rebuild" goto :rebuild
if "%~1"=="image"  goto :build_image_force

:: Fall through to build
goto :build

:: =============================================================================
:: Help
:: =============================================================================

:usage
echo.
echo  Tutorial-OS — Unified Build Script (Windows)
echo  ==============================================
echo.
echo  Usage: build.bat ^<board^> [lang] [profile]
echo.
echo  Languages:
echo    c        C implementation (default)
echo    rust     Rust implementation
echo.
echo  Boards:
echo    ARM64 (Raspberry Pi):
echo      rpi-zero2w-gpi   Pi Zero 2W, 3B, 3B+ (C: GPi 640x480)
echo      rpi-zero2w       Pi Zero 2W, 3B, 3B+ (Rust / C: default res)
echo      rpi-cm4-io       Pi 4, CM4, Pi 400 (C)
echo      rpi-cm4          Pi 4, CM4, Pi 400 (Rust)
echo      rpi-cm5-io       Pi 5, CM5 (C)
echo      rpi-5            Pi 5, CM5 (Rust)
echo.
echo    RISC-V:
echo      milkv-mars       Milk-V Mars (StarFive JH7110)
echo      orangepi-rv2     Orange Pi RV2 (SpacemiT K1)
echo.
echo    x86_64 (UEFI):
echo      lattepanda-mu    LattePanda MU (Intel N100/N305)
echo      lattepanda-iota  LattePanda IOTA (Intel N150)
echo.
echo  Commands:
echo    all [lang]         Build all boards
echo    clean              Clean all artifacts (build/ + target/ + output/)
echo    shell              Interactive shell in container
echo    rebuild            Rebuild Docker image from scratch
echo    image              Build Docker image only
echo.
echo  Examples:
echo    build.bat rpi-zero2w-gpi              C build, release
echo    build.bat milkv-mars rust             Rust build, release
echo    build.bat milkv-mars rust debug       Rust build, debug
echo    build.bat all                         All boards, C
echo    build.bat all rust                    All boards, Rust
echo    build.bat shell                       Debug in container
echo    build.bat clean                       Clean everything
echo.
echo  Requires: Docker Desktop for Windows
echo.
exit /b 0

:: =============================================================================
:: Clean — runs locally, no Docker needed
:: =============================================================================

:clean
echo [INFO] Cleaning all build artifacts...
if exist build\  rmdir /s /q build
if exist output\ rmdir /s /q output
cargo clean 2>nul
echo [SUCCESS] Clean.
exit /b 0

:: =============================================================================
:: Interactive shell
:: =============================================================================

:shell
call :check_docker
call :ensure_image
if !ERRORLEVEL! neq 0 exit /b 1

echo [INFO] Starting interactive shell...
docker run --rm -it ^
    -v "%cd%:/src" ^
    -w /src ^
    %IMAGE_NAME% ^
    bash
exit /b 0

:: =============================================================================
:: Rebuild — nuke image and rebuild from scratch
:: =============================================================================

:rebuild
call :check_docker
echo [INFO] Removing existing image...
docker rmi %IMAGE_NAME% 2>nul
call :build_image_force
exit /b !ERRORLEVEL!

:: =============================================================================
:: Build — delegate to build.sh inside Docker
:: =============================================================================

:build
call :check_docker
call :ensure_image
if !ERRORLEVEL! neq 0 exit /b 1

:: Collect all arguments to pass through to build.sh
set "ARGS="
:arg_loop
if "%~1"=="" goto :run_build
set "ARGS=!ARGS! %~1"
shift
goto :arg_loop

:run_build
echo [INFO] Building:%ARGS%
docker run --rm ^
    -v "%cd%:/src" ^
    -w /src ^
    %IMAGE_NAME% ^
    ./build.sh%ARGS%

if !ERRORLEVEL! neq 0 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [SUCCESS] Build complete. Output:
if exist output\ (
    for /r output\ %%f in (*) do echo   %%f
)
exit /b 0

:: =============================================================================
:: Docker helpers
:: =============================================================================

:check_docker
where docker >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Docker is not installed or not in PATH.
    echo         Install Docker Desktop for Windows:
    echo         https://www.docker.com/products/docker-desktop/
    exit /b 1
)
goto :eof

:ensure_image
docker image inspect %IMAGE_NAME% >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [INFO] Docker image '%IMAGE_NAME%' not found. Building...
    call :build_image_force
)
goto :eof

:build_image_force
echo [INFO] Building Docker image: %IMAGE_NAME%
docker build -t %IMAGE_NAME% .
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to build Docker image.
    echo         Make sure Docker Desktop is running.
    exit /b 1
)
echo [SUCCESS] Docker image built.
goto :eof