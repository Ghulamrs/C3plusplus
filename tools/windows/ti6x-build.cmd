@echo off
rem ti6x-build.cmd [dir] - the C6000 tools built on this box from their sources, since the C6000 does not run on
rem the Mac: ASM6x, LNK6x, VM6747 and SIM6747 with cl, then RTS6x's libraries with them and this tree's cxx3.
rem <dir> (default C:\Users\GRA\work\ti6x) holds the five repositories' sources, shipped from the Mac without their
rem build output. Each tool is built from all of src\*.cpp - VM6747's own msvc\build.cmd names its sources by hand
rem and has fallen behind them. CPP11 names the cxx3 for RTS6x (default: the self-host tree's stage 0).
setlocal
set T=%~1
if "%T%"=="" set T=C:\Users\GRA\work\ti6x
if "%CPP11%"=="" set CPP11=C:\Users\GRA\work\selfhost-tree\cxx3-msvc.exe
call "%~dp0vcvars.cmd" >nul
cd /d %T%
echo started %date% %time% > %T%\build-all.log
for %%x in (ASM6x LNK6x) do call :one %%x build\%%x.exe
call :one VM6747 vm6747.exe
call :one SIM6747 sim6747.exe
set ASM6X=%T%\ASM6x\build\ASM6x.exe
pushd %T%\RTS6x
call build.cmd >> %T%\build-all.log 2>&1
echo RTS6x exit %errorlevel% >> %T%\build-all.log
popd
for %%x in (ASM6x\build\ASM6x.exe LNK6x\build\LNK6x.exe VM6747\vm6747.exe SIM6747\sim6747.exe) do %T%\%%x --version >> %T%\build-all.log 2>&1
echo build-all-done %date% %time% >> %T%\build-all.log
exit /b 0

:one
pushd %T%\%1
if not exist build\obj mkdir build\obj
cl -nologo -std:c++14 -O2 -EHsc -D_CRT_SECURE_NO_WARNINGS -Fo"build\obj\\" -Fe"%2" src\*.cpp >> %T%\build-all.log 2>&1
echo %1 exit %errorlevel% >> %T%\build-all.log
popd
exit /b 0
