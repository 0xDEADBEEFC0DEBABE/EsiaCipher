@echo off
rem build.bat [configure] [target]: Cipher in Release with Ninja into build\ (configures on the first run or with
rem "configure"). The demo: build\bin\cipher.exe.
rem
rem Needs Visual Studio 2022 with the C++ workload (its CMake and Ninja come with it). clang-cl when LLVM is installed
rem (or Visual Studio's own clang component), else MSVC's cl. Esia: ..\Esia when it is there, else fetched from GitHub
rem at configure time (CMakeLists.txt). The Vulkan backend wants the Vulkan SDK (VULKAN_SDK, else the newest one in
rem C:\VulkanSDK); without it Esia builds the other backends.
setlocal
set "HERE=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio 2022 is needed: vswhere.exe was not found
    exit /b 9
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (
    echo Visual Studio's C++ tools are needed
    exit /b 9
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 9
set "PATH=%PATH%;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if not defined VULKAN_SDK for /d %%d in ("C:\VulkanSDK\*") do set "VULKAN_SDK=%%d"
rem clang-cl: the LLVM installer's, else Visual Studio's own; neither: cl
set "TOOLCHAIN="
if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" (
    set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
    set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%HERE%cmake\clang-cl.cmake"
) else if exist "%VS%\VC\Tools\Llvm\x64\bin\clang-cl.exe" (
    set "PATH=%VS%\VC\Tools\Llvm\x64\bin;%PATH%"
    set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%HERE%cmake\clang-cl.cmake"
)
cd /d "%HERE%" || exit /b 9
set "TARGET=cipher_demo"
if not "%2"=="" set "TARGET=%2"
if "%1"=="configure" goto configure
if not exist build\build.ninja goto configure
goto build
:configure
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release %TOOLCHAIN% > build-configure.log 2>&1 || (type build-configure.log & exit /b 1)
:build
cmake --build build --target %TARGET% > build.log 2>&1 || (findstr /c:"error" /c:"FAILED" build.log & exit /b 2)
echo built %TARGET%
exit /b 0
