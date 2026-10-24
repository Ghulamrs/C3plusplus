@echo off
REM Build this example on Windows, with NMAKE and Makefile.windows.
REM
REM ©2026 G. R. Akhtar - C++03 with C++11 extensions
REM
REM **Run it by its full path and with no `cmd /c` in front.** ml64 and link
REM reach PATH only after vcvars64.bat, which this calls - and a chain of
REM quoted paths typed at an ssh shell loses a quote, which is why the work is
REM in a file rather than on a command line. ide\build-vs.cmd carries the same
REM note for the same reason.
REM
REM     build-win.cmd                 uses cxx3.exe from PATH
REM     build-win.cmd C:\path\cxx3.exe
setlocal
set HERE=%~dp0
set CXX1=%1
if "%CXX1%"=="" set CXX1=cxx3.exe

REM The newest Visual Studio vswhere knows, VS 2022 or 18; the 2022 Community path where there is no vswhere.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
if errorlevel 1 ( echo build-win.cmd: no vcvars64 - ml64 and link come from it & exit /b 1 )

cd /d "%HERE%"
nmake /nologo /f Makefile.windows CXX1="%CXX1%"
if errorlevel 1 ( echo build-win.cmd: FAILED & exit /b 1 )
demo.exe
if errorlevel 1 ( echo build-win.cmd: the program failed & exit /b 1 )
echo build-win.cmd: built and ran demo.exe
endlocal
