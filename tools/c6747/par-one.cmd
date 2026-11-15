@echo off
rem par-one.cmd <name> - <name>.out on the C6747 cycle-accurate simulator, stopped at 30 minutes;
rem writes <name>.result, .stdout and .log. One run of bench-par.cmd.
rem eclipsec is called with a workspace of its own - dss.bat gives every run the one default - and
rem its output goes to a file: the RESULT line read back through for /f lost runs started together.
if "%1"==":capped" goto capped
rem A cap of RUNCAP seconds (30 minutes when unset): a run past it is killed and its result says capped.
setlocal
if "%RUNCAP%"=="" set RUNCAP=1860
set /a SIMMS=(RUNCAP-30)*1000
set W=%~dp0
set W=%W:~0,-1%
set WF=%W:\=/%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
cd /d "%W%"
if not exist ws mkdir ws 2>nul
set CAP_EXE=%CCS55%\eclipse\eclipsec.exe
set "CAP_ARGS=-nosplash -data "%W%\ws\%1" -application com.ti.ccstudio.apps.runScript -dss.rhinoArgs ""%WF%/runca.js" "%WF%/c6747ca-windows.ccxml" "%WF%/%1.out" "%WF%/%1.stdout" 0 %SIMMS%""
set CAP_SEC=%RUNCAP%
set CAP_OUT=%1.log
call :capped
if "%CAPRC%"=="124" ((echo BOX %1 capped=%RUNCAP%s)> %1.result & echo RC-CAP %1: the simulator run passed %RUNCAP% s - FAIL & goto :eof)
findstr /b RESULT %1.log > %1.r 2>nul
set R=
set /p R=< %1.r
rem In parentheses: a line ending in a digit, wall_ms=2, would make "2>" a redirection of stderr.
rem Apart, by goto: an if line is parsed whole, and %R:RESULT =% with R empty ends cmd before .result is written.
if "%R%"=="" goto :none
(echo BOX %1 %R:RESULT =%)> %1.result
goto :written
:none
(echo BOX %1 timeout-or-failed)> %1.result
:written
del %1.r 2>nul
goto :eof
rem :capped - CAP_EXE with CAP_ARGS, output to CAP_OUT, killed (its tree, by PID) past CAP_SEC seconds;
rem exits 124 then. The arguments go by the environment, so no quoting reaches PowerShell.
:capped
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command "$p=Start-Process -FilePath $env:CAP_EXE -ArgumentList $env:CAP_ARGS -NoNewWindow -PassThru -RedirectStandardOutput $env:CAP_OUT -RedirectStandardError ($env:CAP_OUT+'.err'); if($p.WaitForExit([int]$env:CAP_SEC*1000)){exit 0}; function K($i){Get-CimInstance Win32_Process -Filter ('ParentProcessId='+$i) | ForEach-Object {K $_.ProcessId}; Stop-Process -Id $i -Force -ErrorAction SilentlyContinue}; K $p.Id; exit 124"
set CAPRC=%ERRORLEVEL%
if exist "%CAP_OUT%.err" (type "%CAP_OUT%.err" >> "%CAP_OUT%" & del "%CAP_OUT%.err")
exit /b %CAPRC%
