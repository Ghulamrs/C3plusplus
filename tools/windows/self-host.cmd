@echo off
rem  **M1's stages 1 and 2, on the target they are for (R5.2).** Stage 0 is cxx3-msvc.exe, built by cl
rem  through msvc\build.cmd; it compiles and links every source in src\ into cxx3-stage1.exe, and
rem  stage 1 does the same into cxx3-stage2.exe. Two compilers built by two different ones, given one
rem  input: each source's assembly from stage 1 and from stage 2 must be the same, byte for byte.
rem
rem  Usage:  self-host.cmd <root> [flags for every compile, -O2 say]
rem  Writes  <root>\selfhost\cxx3-stage1.exe and cxx3-stage2.exe, the assembly of every source from
rem          each in <root>\selfhost\s1\ and s2\, and <root>\selfhost\differ.txt naming the sources
rem          whose two are not the same. Only cxx3's own include\ and lib\ are on the path.
setlocal enabledelayedexpansion
if "%~1"=="" (echo self-host.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set FLAGS=%~2
set OUT=%ROOT%\selfhost
call "%ROOT%\msvc\build.cmd" >nul
if errorlevel 1 (echo self-host.cmd: stage 0 did not build & exit /b 1)
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%\s1"
mkdir "%OUT%\s2"

rem  The include directories are compiled in as string literals, so with forward slashes - see build.cmd.
set "ROOTFWD=%ROOT:\=/%"
set DEFS=-I "%ROOT%\src" -DCXX1_INCLUDE_DIR=\"%ROOTFWD%/lib\" -DCXX1_CXX_INCLUDE_DIR=\"%ROOTFWD%/include\"
set SRCS=
for %%d in (src src\parser src\backend src\optimizer src\sema) do for %%f in ("%ROOT%\%%d\*.cpp") do set SRCS=!SRCS! "%%f"

"%ROOT%\cxx3-msvc.exe" -nologo %FLAGS% %DEFS% !SRCS! -o "%OUT%\cxx3-stage1.exe" > "%OUT%\stage1.log" 2>&1
rem  A crash's status is negative, which `if errorlevel 1` does not see - so the status, and the file.
if !errorlevel! neq 0 (echo self-host.cmd: stage 0 did not build stage 1 - see %OUT%\stage1.log & exit /b 1)
if not exist "%OUT%\cxx3-stage1.exe" (echo self-host.cmd: stage 0 wrote no stage 1 & exit /b 1)
echo self-host.cmd: stage 1 built
"%OUT%\cxx3-stage1.exe" -nologo %FLAGS% %DEFS% !SRCS! -o "%OUT%\cxx3-stage2.exe" > "%OUT%\stage2.log" 2>&1
if !errorlevel! neq 0 (echo self-host.cmd: stage 1 did not build stage 2, status !errorlevel! - see %OUT%\stage2.log & exit /b 1)
if not exist "%OUT%\cxx3-stage2.exe" (echo self-host.cmd: stage 1 wrote no stage 2 & exit /b 1)
echo self-host.cmd: stage 2 built

rem  The fixed point: each source's assembly from the two stages, the clock pinned - Driver.cpp's -version
rem  names the build's __DATE__ and __TIME__, and the two stages are built at two moments.
set CLOCK="-D__DATE__=\"Jan 01 2000\"" -D__TIME__=\"00:00:00\"
set N=0
set SAME=0
type nul > "%OUT%\differ.txt"
for %%d in (src src\parser src\backend src\optimizer src\sema) do for %%f in ("%ROOT%\%%d\*.cpp") do (
    set /a N+=1
    "%OUT%\cxx3-stage1.exe" -nologo -S %FLAGS% %DEFS% %CLOCK% "%%f" -o "%OUT%\s1\%%~nf.s" > nul 2>&1
    "%OUT%\cxx3-stage2.exe" -nologo -S %FLAGS% %DEFS% %CLOCK% "%%f" -o "%OUT%\s2\%%~nf.s" > nul 2>&1
    fc /b "%OUT%\s1\%%~nf.s" "%OUT%\s2\%%~nf.s" > nul 2>&1
    if errorlevel 1 (echo %%~nxf>> "%OUT%\differ.txt") else (set /a SAME+=1)
)
echo self-host.cmd: %SAME% of %N% sources emit the same assembly from stage 1 and stage 2
if not "%SAME%"=="%N%" (endlocal & exit /b 1)
endlocal
