@echo off
rem cpp11-build.cmd <dir> <flags> <suffix> <source>... - cpp11's C6000 objects, built on this box: the C6000 does
rem not run on the Mac. Each source is copied to <dir>\cpp11-src\<name>.cpp (cpp11 compiles C++; a .c goes in
rem under a .cpp name, and apart, since a caller's folder of programs is read as programs), compiled by %CXX3%
rem -arch tms6747 <flags>, and assembled by %ASM6X% %ASM6X_FLAGS% to <dir>\<name>.<suffix>.obj. Where LNK6X, TILIB and MAP are set the object is linked too, as
rem TI's linker is given it: <dir>\<name>.<suffix>-lnk6x.out and .map. A failure is printed and the rest go on.
setlocal enabledelayedexpansion
set "D=%~1"
set "F=%~2"
set "S=%~3"
if not exist "%CXX3%" (echo cpp11-build: no cxx3 at %CXX3% & exit /b 2)
if not exist "%ASM6X%" (echo cpp11-build: no asm6x at %ASM6X% & exit /b 2)
if not exist "%D%" mkdir "%D%"
set "W=%D%\cpp11-src"
if not exist "%W%" mkdir "%W%"
set BAD=0
:next
shift
shift
shift
:each
if "%~1"=="" goto done
set "N=%~n1"
copy /y "%~1" "%W%\!N!.cpp" >nul
set "L=%W%\!N!.%S%.log"
"%CXX3%" -arch tms6747 -nologo %F% -S "%W%\!N!.cpp" -o "%W%\!N!.%S%.s" > "!L!" 2>&1
if errorlevel 1 (echo cpp11-build: cpp11 %F% did not build !N! & type "!L!" & set /a BAD+=1 & shift & goto each)
"%ASM6X%" %ASM6X_FLAGS% "%W%\!N!.%S%.s" -o "%D%\!N!.%S%.obj" >> "!L!" 2>&1
if errorlevel 1 (echo cpp11-build: asm6x did not assemble !N! %F% & type "!L!" & set /a BAD+=1 & shift & goto each)
if not "%LNK6X%"=="" if exist "%TILIB%\rts6740_elf_eh.lib" (
  "%LNK6X%" --cgt=7.4.4 -mv6740 --abi=eabi "%D%\!N!.%S%.obj" --heap_size=0x800 --stack_size=0x800 -i"%TILIB%" --rom_model "%MAP%" -lrts6740_elf_eh.lib -m "%D%\!N!.%S%-lnk6x.map" -o "%D%\!N!.%S%-lnk6x.out" > "%W%\!N!.%S%.lnk6x.log" 2>&1
  if errorlevel 1 (echo cpp11-build: LNK6x did not link !N! %F% & type "%W%\!N!.%S%.lnk6x.log")
)
shift
goto each
:done
exit /b %BAD%
