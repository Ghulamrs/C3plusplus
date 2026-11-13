@echo off
rem  example-link.cmd <root> <flags>: examples\'s three sources through cxx3 -c (CoffEmitter, the
rem  default -masm=masm), linked by Microsoft's link.exe with the CRT by hand, and run; cl's build of the
rem  same sources is the expected output. Writes examples\el-*.out; prints one verdict.
setlocal
set ROOT=%~1
set FLAGS=%~2
call "%~dp0vcvars.cmd" >nul
if errorlevel 1 (echo example-link.cmd: no vcvars64 & exit /b 1)
set MSLINK=
for /f "delims=" %%l in ('where link.exe') do if not defined MSLINK set "MSLINK=%%l"
echo example-link.cmd: link is %MSLINK%
cd /d "%ROOT%\examples"
del /q el-*.obj el-*.exe el-*.out 2>nul
for %%f in (demo report shapes) do (
    "%ROOT%\cxx3-msvc.exe" %FLAGS% -c %%f.cpp -o el-%%f.obj
    if errorlevel 1 (echo example-link.cmd: cxx3 -c %%f.cpp FAILED & exit /b 1)
)
"%MSLINK%" /nologo /subsystem:console /stack:8388608 /out:el-demo.exe el-demo.obj el-report.obj el-shapes.obj libcmt.lib libcpmt.lib libucrt.lib libvcruntime.lib kernel32.lib legacy_stdio_definitions.lib
if errorlevel 1 (echo example-link.cmd: link.exe FAILED & exit /b 1)
el-demo.exe > el-demo.out
echo example-link.cmd: el-demo.exe exit %errorlevel%
cl /nologo /EHsc /O2 demo.cpp report.cpp shapes.cpp /Fe:el-cl.exe >nul
if errorlevel 1 (echo example-link.cmd: cl FAILED & exit /b 1)
del /q demo.obj report.obj shapes.obj 2>nul
el-cl.exe > el-cl.out
fc /b el-demo.out el-cl.out >nul
if errorlevel 1 (echo example-link.cmd: DIFFER from cl & type el-demo.out & exit /b 1)
echo example-link.cmd: same output as cl
type el-demo.out
endlocal
