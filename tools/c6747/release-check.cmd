@echo off
rem release-check.cmd - the Windows half of tools/c6747/release-check, run in a
rem directory the Mac driver shipped: leveled cpp11 assembly <name>.<level>.s and
rem ASM6x's object <name>.<level>.asm6x.obj, plus runca.js, par-one.cmd, the ccxml
rem and C6747.cmd. It answers the two release questions on the box the emulator
rem cannot:
rem   encoding - TI's assembler (cl6x 8.2.2) assembles each .s --no_compress, and
rem              dis6x disassembles both its object and ASM6x's, to out\.
rem   run      - each -O2 ASM6x object is linked by cl6x 7.4.4 against the EH
rem              runtime and C6747.cmd and run on the C6747 cycle-accurate
rem              simulator through DSS (par-one.cmd), in parallel.
rem The Mac compares; this box only builds, disassembles and runs.
setlocal enabledelayedexpansion
set W=%~dp0
set W=%W:~0,-1%
if "%CCS55%"=="" set CCS55=C:\ti\ccsv5
if "%CGT74%"=="" set CGT74=C:\ti\ccsv7\tools\compiler\ti-cgt-c6000_8.2.2
if "%C6747_EHLIB%"=="" set C6747_EHLIB=C:\ti\c6747-lib
if "%MAXRUNS%"=="" set MAXRUNS=6
rem Caps in seconds: one link, one simulator run (par-one.cmd), and the wait for them all.
if "%LINKCAP%"=="" set LINKCAP=120
if "%RUNCAP%"=="" set RUNCAP=180
if "%GATHERCAP%"=="" set GATHERCAP=1200
rem System32 by full path: under Git's bash, find is GNU find, and "find /c" walked all of C:.
set S32=%SystemRoot%\System32
set CG55=%CCS55%\tools\compiler\c6000_7.4.4
if not exist "%CGT74%\bin\cl6x.exe" (echo RC-ERROR no cl6x 8.2.2 at %CGT74% & exit /b 2)
if not exist "%CGT74%\bin\dis6x.exe" (echo RC-ERROR no dis6x at %CGT74% & exit /b 2)
if not exist "%CG55%\bin\cl6x.exe" (echo RC-ERROR no cl6x 7.4.4 at %CG55% & exit /b 2)
if not exist "%C6747_EHLIB%\rts6740_elf_eh.lib" (echo RC-ERROR no rts6740_elf_eh.lib in %C6747_EHLIB% & exit /b 2)
cd /d "%W%"
if exist out rmdir /s /q out
mkdir out
del /q *.result *.stdout *.out 2>nul

rem --- encoding: TI assembles every .s, dis6x on both objects ---
for %%f in (*.s) do (
  set B=%%~nf
  "%CGT74%\bin\cl6x" -mv6740 --abi=eabi --no_compress -c "%%f" --output_file=out\!B!.ti.obj > out\!B!.ti.log 2>&1
  if exist out\!B!.ti.obj (
    "%CGT74%\bin\dis6x" out\!B!.ti.obj > out\!B!.ti.dis 2>&1
  ) else (
    echo TI-ASM-FAILED !B! > out\!B!.ti.dis
    type out\!B!.ti.log >> out\!B!.ti.dis
  )
  if exist "!B!.asm6x.obj" (
    "%CGT74%\bin\dis6x" "!B!.asm6x.obj" > out\!B!.asm6x.dis 2>&1
  ) else (
    echo NO-ASM6X-OBJ !B! > out\!B!.asm6x.dis
  )
)

rem --- run: link each -O2 ASM6x object, then the simulator in parallel ---
set LINK=-z --heap_size=0x800 --stack_size=0x4000 -i"%C6747_EHLIB%" --rom_model "%W%\C6747.cmd" -lrts6740_elf_eh.lib
set WANT=0
set LINKFAIL=0
for %%o in (*.O2.asm6x.obj) do (
  set N=%%~no
  set N=!N:.O2.asm6x=!
  set CAP_EXE=%CG55%\bin\cl6x.exe
  set CAP_ARGS=-mv6740 --abi=eabi "%%o" %LINK% -m "!N!.map" -o "!N!.out"
  set CAP_SEC=%LINKCAP%
  set CAP_OUT=!N!.link.log
  call "%W%\par-one.cmd" :capped
  if !ERRORLEVEL!==124 (del "!N!.out" 2>nul & echo RC-CAP !N!: the link passed %LINKCAP% s - FAIL & echo link capped at %LINKCAP% s>> "!N!.link.log")
  if exist "!N!.out" (set /a WANT+=1) else (echo BOX !N! link=FAILED> "!N!.result" & set /a LINKFAIL+=1 & type "!N!.link.log")
)
if exist ws rmdir /s /q ws 2>nul
mkdir ws 2>nul
set STARTED=0
for %%p in (*.out) do if not exist %%~np.result call :start %%~np
set /a WANT+=LINKFAIL
set WAITED=0
:gather
set DONE=0
for %%r in (*.result) do set /a DONE+=1
if !DONE! geq !WANT! goto gathered
if !WAITED! geq %GATHERCAP% goto gathercap
"%S32%\ping.exe" -n 11 127.0.0.1 >nul
set /a WAITED+=10
goto gather
:gathercap
for %%p in (*.out) do if not exist %%~np.result (echo RC-CAP %%~np: no result in %GATHERCAP% s - FAIL & (echo BOX %%~np capped=gather)> %%~np.result)
:gathered
rem A DSS session that never started - some started together fail (T8) - is run again, alone.
for %%r in (*.result) do "%S32%\findstr.exe" /c:"count=" /c:"capped=" /c:"link=FAILED" %%r >nul || (del %%r & call "%W%\par-one.cmd" %%~nr)
copy /y *.result out\ >nul 2>&1
copy /y *.stdout out\ >nul 2>&1
echo done> out\release-check.done
exit /b 0

:start
rem Runs in flight are this check's own: started, less those with a result (link failures have one too).
set /a RUNNING=STARTED+LINKFAIL
for %%r in (*.result) do set /a RUNNING-=1
if %RUNNING% geq %MAXRUNS% ("%S32%\ping.exe" -n 11 127.0.0.1 >nul & goto start)
start "%1" /b cmd /c ""%W%\par-one.cmd" %1"
set /a STARTED+=1
"%S32%\ping.exe" -n 4 127.0.0.1 >nul
exit /b 0
