@echo off
call "%~dp0env.cmd"
cd /d "%PROJ%"
if not exist "%PROJ%\build\tests" mkdir "%PROJ%\build\tests"
cl /nologo /O2 /std:c17 /I "%PROJ%\runtime" /I "%RECOMP_INC%" ^
  "%PROJ%\runtime\continuation.c" ^
  "%PROJ%\src\tests\test_continuation.c" ^
  /Fe:"%PROJ%\build\tests\test_continuation.exe"
if errorlevel 1 exit /b 1
"%PROJ%\build\tests\test_continuation.exe"
if errorlevel 1 exit /b 1

cl /nologo /O2 /std:c17 /I "%PROJ%\runtime" /I "%RECOMP_INC%" ^
  "%PROJ%\runtime\continuation.c" ^
  "%PROJ%\runtime\stateful_thread.c" ^
  "%PROJ%\src\tests\test_stateful_thread.c" ^
  /Fe:"%PROJ%\build\tests\test_stateful_thread.exe"
if errorlevel 1 exit /b 1
"%PROJ%\build\tests\test_stateful_thread.exe" ^
  "%PROJ%\build\tests\thread_roundtrip.bin"
exit /b %errorlevel%
