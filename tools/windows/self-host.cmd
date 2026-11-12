@echo off
rem  **M1's stages, and M3b.6's stage 3, on the target they are for (R5.2)**, as tools/self-host is on Linux and
rem  macOS. Stage 0 is cxx3-msvc.exe, built by cl through msvc\build.cmd. Stage k compiles every source in src\
rem  to an object, one source a job, into selfhost\s<k>\; those objects, linked by stage k, are stage k+1. The
rem  claims are two: stage 0 = stage 1 = stage 2, and stage 2 = stage 3, each source's object byte for byte.
rem
rem  Usage:  self-host.cmd <root> [flags for every compile, -O2 say] [stages, "0 1 2 3" by default]
rem  Writes  <root>\selfhost\cxx3-stage<k>.exe, the objects in s<k>\, and differ.txt naming each source whose
rem          objects differ, with the two stages. The clock is pinned in every compile, so a stage built here says
rem          01-01-2000. Stage 0 starts afresh; a later stage needs the one before it, so "0 1", "2", "3" can be
rem          three box jobs. Only cxx3's own include\ and lib\ are on the path. Unlike tools/self-host it has no
rem          cap of its own (cmd has none): the caller caps it, and it names no function in a difference.
setlocal enabledelayedexpansion
if "%~1"=="" (echo self-host.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set FLAGS=%~2
set STAGES=%~3
if "!STAGES!"=="" set STAGES=0 1 2 3
rem  The default spelling: the project's masm and LINK (6bcb5d2 and 959dac2 or later, for thread_local),
rem  named by CPP11_AS and CPP11_LD or taken from beside stage 0 - stage 1 runs from elsewhere, so it is told.
if not defined CPP11_AS if exist "%ROOT%\masm.exe" set "CPP11_AS=%ROOT%\masm.exe"
if not defined CPP11_LD if exist "%ROOT%\link.exe" set "CPP11_LD=%ROOT%\link.exe"
if not defined CPP11_AS (echo self-host.cmd: no masm.exe beside cxx3-msvc.exe and no CPP11_AS - ml64 cannot link it & exit /b 2)
set OUT=%ROOT%\selfhost

rem  The include directories are compiled in as string literals, so with forward slashes - see build.cmd.
set "ROOTFWD=%ROOT:\=/%"
set DEFS=-I "%ROOT%\src" -DCXX1_INCLUDE_DIR=\"%ROOTFWD%/lib\" -DCXX1_CXX_INCLUDE_DIR=\"%ROOTFWD%/include\"
rem  The clock pinned: Driver.cpp's -version names the build's __DATE__ and __TIME__.
set CLOCK="-D__DATE__=\"Jan 01 2000\"" -D__TIME__=\"00:00:00\"
set DIRS=src src\parser src\backend src\optimizer src\sema src\syntax src\ir src\ir\opt src\encoder
set N=0
for %%d in (%DIRS%) do for %%f in ("%ROOT%\%%d\*.cpp") do set /a N+=1
for %%s in (%STAGES%) do (
    call :stage %%s
    if errorlevel 1 (endlocal & exit /b 1)
)

rem  The comparison, over the stages present.
set A=0
set C=0
set H012=1
set H23=1
for %%s in (0 1 2) do if not exist "%OUT%\s%%s" set H012=0
for %%s in (2 3) do if not exist "%OUT%\s%%s" set H23=0
type nul > "%OUT%\differ.txt"
for %%d in (%DIRS%) do for %%f in ("%ROOT%\%%d\*.cpp") do (
    if !H012!==1 (
        set OK=1
        call :same 0 1 %%~nf
        if errorlevel 1 set OK=0
        call :same 1 2 %%~nf
        if errorlevel 1 set OK=0
        if !OK!==1 set /a A+=1
    )
    if !H23!==1 (
        call :same 2 3 %%~nf
        if not errorlevel 1 set /a C+=1
    )
)
set STATUS=0
if !H012!==1 echo self-host.cmd: !A! of %N% sources the same object from stages 0, 1 and 2
if !H012!==1 if not "!A!"=="%N%" set STATUS=1
if !H23!==1 echo self-host.cmd: !C! of %N% sources the same object from stages 2 and 3
if !H23!==1 if not "!C!"=="%N%" set STATUS=1
if !H012!==0 if !H23!==0 echo self-host.cmd: neither stages 0-2 nor 2-3 are all in %OUT% yet - nothing compared
type "%OUT%\differ.txt"
endlocal & exit /b %STATUS%

:stage
set S=%1
if "%S%"=="0" (
    call "%ROOT%\msvc\build.cmd" >nul
    if errorlevel 1 (echo self-host.cmd: stage 0 did not build & exit /b 1)
    if exist "%OUT%" rmdir /s /q "%OUT%"
    mkdir "%OUT%"
    copy /y "%ROOT%\cxx3-msvc.exe" "%OUT%\cxx3-stage0.exe" >nul
)
if not exist "%OUT%\cxx3-stage%S%.exe" (echo self-host.cmd: no stage %S% in %OUT% - run the stage before it first & exit /b 1)
rem  SELFHOST_RESUME=1 keeps the stage's objects from an earlier job and compiles only the rest.
if not "%SELFHOST_RESUME%"=="1" if exist "%OUT%\s%S%" rmdir /s /q "%OUT%\s%S%"
if not exist "%OUT%\s%S%\part" mkdir "%OUT%\s%S%\part"
set GOT=0
for %%d in (%DIRS%) do for %%f in ("%ROOT%\%%d\*.cpp") do if not exist "%OUT%\s%S%\%%~nf.obj" (
    rem  Written under part\ and moved, so a job killed mid-compile leaves no object a resumed job would keep.
    "%OUT%\cxx3-stage%S%.exe" -nologo -c %FLAGS% %DEFS% %CLOCK% "%%f" -o "%OUT%\s%S%\part\%%~nf.obj" > "%OUT%\s%S%\%%~nf.log" 2>&1
    rem  A crash's status is negative, which `if errorlevel 1` does not see - so the status.
    if !errorlevel! neq 0 (if exist "%OUT%\s%S%\part\%%~nf.obj" del /q "%OUT%\s%S%\part\%%~nf.obj") else move /y "%OUT%\s%S%\part\%%~nf.obj" "%OUT%\s%S%\%%~nf.obj" >nul
)
for %%d in (%DIRS%) do for %%f in ("%ROOT%\%%d\*.cpp") do (
    if exist "%OUT%\s%S%\%%~nf.obj" set /a GOT+=1
)
echo self-host.cmd: stage %S% compiled !GOT! of %N% sources
if %S% geq 3 exit /b 0
if not "!GOT!"=="%N%" exit /b 0
set /a NEXT=S+1
set OBJS=
for %%o in ("%OUT%\s%S%\*.obj") do set OBJS=!OBJS! "%%o"
"%OUT%\cxx3-stage%S%.exe" -nologo %FLAGS% !OBJS! -o "%OUT%\cxx3-stage!NEXT!.exe" > "%OUT%\link!NEXT!.log" 2>&1
if !errorlevel! neq 0 (echo self-host.cmd: stage %S% did not link stage !NEXT!, status !errorlevel! - see %OUT%\link!NEXT!.log & exit /b 0)
if not exist "%OUT%\cxx3-stage!NEXT!.exe" (echo self-host.cmd: stage %S% wrote no stage !NEXT! & exit /b 0)
echo self-host.cmd: stage !NEXT! built
exit /b 0

:same
if not exist "%OUT%\s%1\%3.obj" goto :differs
if not exist "%OUT%\s%2\%3.obj" goto :differs
fc /b "%OUT%\s%1\%3.obj" "%OUT%\s%2\%3.obj" > nul 2>&1
if errorlevel 1 goto :differs
exit /b 0
:differs
echo %3.cpp: stage %1 against stage %2>> "%OUT%\differ.txt"
exit /b 1
