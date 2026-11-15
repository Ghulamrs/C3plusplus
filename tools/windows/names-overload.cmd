@echo off
rem  tests/names.sh, tests/overload.sh and tests/win-structure.sh on the Windows box, their home (Q9).
rem
rem  Usage:  names-overload.cmd <root> [compiler]
rem          <root>      the tree, e.g. C:\Users\GRA\work\rtsdiv\ws-a
rem          [compiler]  the cxx1 to test; default <root>\cxx3-msvc.exe,
rem                      which msvc\build.cmd makes
rem
rem  Both suites ask clang, and the two scripts are POSIX sh - so they run under
rem  Git's bash with Visual Studio's LLVM first on PATH. That directory is the
rem  one Visual Studio 2022 installs its optional clang into; tests/names.sh
rem  asks `command -v clang++` and tools/mangled-names calls `clang++` by name,
rem  and overload.sh takes the same binary through CLANG. Measured 2026-10-08
rem  (plan REVIEW-PLAN-2026-10-08 WS-A, first hour): clang 19.1.5 there,
rem  overload.sh 31 agreed / 0 differed, names.sh 398 passed / 6 failed from a
rem  tree built by msvc\build.cmd - so the control-room rule holds for these
rem  two suites too. Two of the six are this box's and not the compiler's:
rem  a case that includes <cstdio> cannot be compiled by this clang for
rem  arm64-darwin, which has no Darwin headers here (ifdef-comment,
rem  using-declaration-chain); the Mac compares those. The other four are real
rem  and host-independent (docs/review-2026-10-08/ws-a.md names them).
rem
rem  Two things about this clang worth knowing before trusting it as an
rem  oracle: its default target is x86_64-pc-windows-msvc, so a program
rem  compiled without -target is compiled as MSVC would see it - with
rem  -fdelayed-template-parsing on, which turns two-phase lookup off; pass
rem  -fno-delayed-template-parsing for any template-lookup question. And
rem  -target x86_64-linux-gnu has no headers on this box, so a case that
rem  includes one is compared on the Mac or not at all.
rem
rem  The output directories (tests\out-names, tests\out-overload) are the
rem  scripts' own; read a FAIL line's report there.
setlocal
if "%~1"=="" (echo names-overload.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set CXX1=%~2
if "%CXX1%"=="" set CXX1=%ROOT%\cxx3-msvc.exe
if not exist "%CXX1%" (echo names-overload.cmd: no compiler at %CXX1% - run msvc\build.cmd first & exit /b 2)

rem  C:\a\b -> /c/a/b, the spelling bash wants.
set UROOT=%ROOT::=%
set UROOT=/%UROOT:\=/%
set UCXX1=%CXX1::=%
set UCXX1=/%UCXX1:\=/%

rem  The LLVM of whichever Visual Studio 2022 is installed: the Windows box has the Build Tools (R103).
set LLVM=
for %%E in ("C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" "C:\Program Files\Microsoft Visual Studio\2022\BuildTools" "C:\Program Files\Microsoft Visual Studio\2022\Community" "C:\Program Files\Microsoft Visual Studio\2022\Professional" "C:\Program Files\Microsoft Visual Studio\2022\Enterprise") do (
    if not defined LLVM if exist "%%~E\VC\Tools\Llvm\x64\bin\clang++.exe" set "LLVM=%%~E\VC\Tools\Llvm\x64\bin"
)
if not defined LLVM (echo names-overload.cmd: NOT RUN - no clang++.exe under any Visual Studio 2022 - install the "C++ Clang tools" component & exit /b 3)
set ULLVM=%LLVM::=%
set ULLVM=/%ULLVM:\=/%
rem  A python that runs: Git's bash finds the Store's python3 stub first, which runs nothing.
set PYTHON=
for %%P in ("C:\Program Files\Python312\python.exe" "C:\Program Files\Python313\python.exe" "C:\Program Files\Python311\python.exe") do (
    if not defined PYTHON if exist %%P set "PYTHON=%%~P"
)
if not defined PYTHON (echo names-overload.cmd: NOT RUN - no python for win-structure's checks & exit /b 3)
set UPYTHON=%PYTHON::=%
set UPYTHON=/%UPYTHON:\=/%

set STATUS=0
for %%S in (names overload win-structure) do (
    "C:\Program Files\Git\bin\bash.exe" -c "export PATH='%ULLVM%':$PATH; export CXX1='%UCXX1%' CLANG=clang++ PYTHON='%UPYTHON%'; cd '%UROOT%' && tests/%%S.sh" < NUL
    if errorlevel 1 set STATUS=1
)
endlocal & exit /b %STATUS%
