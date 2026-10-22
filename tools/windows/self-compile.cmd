@echo off
rem  **M1's measurement, on the target it is for.** R5.2 makes x86_64-windows the
rem  self-hosting target, so the gap list is taken here and not on a Mac: stage 0 is
rem  cxx1-msvc.exe, built by cl through msvc\build.cmd, and it compiles every one of its
rem  own sources for x86_64-windows with -keep-going, with only its own include\ and lib\
rem  on the path - never msvc\compat\, which is cl's crutch and not cxx1's.
rem
rem  Usage:  self-compile.cmd <root> [extra include directory]
rem  Writes  <root>\selfout\<source>.err and .rc, one pair per source, its basename
rem          (basenames do not repeat across src\). The extra directory is put first on
rem          the path, for measuring past a header the library does not have yet.
setlocal enabledelayedexpansion
if "%~1"=="" (echo self-compile.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set EXTRA=
if not "%~2"=="" set EXTRA=-I "%~2"
call "%ROOT%\msvc\build.cmd" >nul
if errorlevel 1 (echo self-compile.cmd: stage 0 did not build & exit /b 1)
if exist "%ROOT%\selfout" rmdir /s /q "%ROOT%\selfout"
mkdir "%ROOT%\selfout"
set N=0
set OK=0
for /r "%ROOT%\src" %%f in (*.cpp) do (
    set /a N+=1
    "%ROOT%\cxx1-msvc.exe" -nologo -S -arch x86_64-windows -keep-going=500 !EXTRA! "%%f" -o "%ROOT%\selfout\%%~nf.s" > "%ROOT%\selfout\%%~nxf.err" 2>&1
    set RC=!errorlevel!
    echo !RC!> "%ROOT%\selfout\%%~nxf.rc"
    if "!RC!"=="0" set /a OK+=1
)
echo self-compile.cmd: %OK% of %N% sources compiled by stage 0
endlocal
