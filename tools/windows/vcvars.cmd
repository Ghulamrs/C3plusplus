@echo off
rem  vcvars64.bat of the newest Visual Studio vswhere knows - any version or edition, VS 2022 or 18 -
rem  run in the caller's environment, so its errorlevel is the caller's. The 2022 Community path is the
rem  fallback for a machine whose installer has no vswhere. CXX1_VCVARS_VER picks an older toolset installed
rem  beside it - 14.44 is v143 (cl 19.44, VS 2022's) inside VS 18, whose default is v145 (cl 19.51).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if defined CXX1_VCVARS_VER (call "%VCVARS%" -vcvars_ver=%CXX1_VCVARS_VER%) else call "%VCVARS%"
