@echo off
rem  par.cmd <n> <script> [args...] - runs "<script> :shard <k> <n> [args...]" for k = 1..n at
rem  once and waits for all of them; each shard's output is printed after, in shard order. The
rem  box has twenty threads, and a script that walks its cases one by one leaves nineteen idle.
setlocal
set N=%~1
set SCRIPT=%~f2
set ARGS=%3 %4 %5 %6 %7 %8 %9
set FIRST=%CXX1_PAR_FIRST%
if "%FIRST%"=="" set FIRST=1
set LAST=%CXX1_PAR_LAST%
if "%LAST%"=="" set LAST=%N%
rem  %RANDOM% is seeded from the clock, so two par.cmd started together draw the same number:
rem  the name takes the time too, and a name already taken is drawn again.
rem  CXX1_PAR_FIRST and CXX1_PAR_LAST run shards FIRST..LAST of the n only, so a pass too long for one job is two.
:tag
set STAMP=%TIME: =0%
set STAMP=%STAMP::=%
set STAMP=%STAMP:.=%
set STAMP=%STAMP:,=%
set TAG=%TEMP%\par-%RANDOM%%RANDOM%-%STAMP%
mkdir "%TAG%" 2>nul || goto tag
for /l %%k in (%FIRST%,1,%LAST%) do start "" /b cmd /c ""%SCRIPT%" :shard %%k %N% %ARGS% > "%TAG%\%%k.log" 2>&1 & echo.> "%TAG%\%%k.done""
:wait
for /l %%k in (%FIRST%,1,%LAST%) do if not exist "%TAG%\%%k.done" (ping -n 2 127.0.0.1 >nul & goto wait)
for /l %%k in (%FIRST%,1,%LAST%) do type "%TAG%\%%k.log"
rmdir /s /q "%TAG%"
endlocal
