@echo off
rem ===================================================================
rem  QtCalculator 一键构建脚本（Windows / MinGW / qmake）
rem
rem  说明：本机环境的 qmake 无法通过 QProcess 探测编译器
rem        （报 QProcess: CreateFile failed），因此改用预先写好的
rem        .qmake.stash 缓存提供编译器信息，见 tools/gen_qmake_stash.py。
rem ===================================================================
setlocal
set QTDIR=D:\Qt\6.11.2\mingw_64
set MINGWDIR=D:\Qt\Tools\mingw1310_64
set PATH=%QTDIR%\bin;%MINGWDIR%\bin;%PATH%
set PROJ=%~dp0

if not exist "%PROJ%.qmake.stash" (
    echo [1/3] 生成 qmake 编译器缓存 ...
    "C:\Users\plm18\.workbuddy\binaries\python\versions\3.13.12\python.exe" "%PROJ%tools\gen_qmake_stash.py"
)

if not exist "%PROJ%build" mkdir "%PROJ%build"
cd /d "%PROJ%build"

echo [2/3] 运行 qmake ...
qmake.exe -spec win32-g++ "%PROJ%QtCalculator.pro"
if errorlevel 1 goto :fail

echo [3/3] 编译 ...
mingw32-make.exe -j4
if errorlevel 1 goto :fail

echo.
echo 构建成功： %PROJ%build\release\QtCalculator.exe
goto :eof

:fail
echo.
echo 构建失败，请检查上方错误信息。
exit /b 1
