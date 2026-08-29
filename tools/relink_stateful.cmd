@echo off
REM Recompila os modulos centrais alterados durante a estabilizacao do
REM dispatcher e relinka com o CPU stateful ja gerado.
call "%~dp0env.cmd"
cd /d "%PROJ%"
set INC=/I "%RECOMP_INC%" /I "%PROJ%\src\RecompiledFuncsStateful" /I "%PROJ%\runtime"
cl /nologo /c /O2 /std:c17 /EHa /DRECOMP_STATEFUL /DRECOMP_TRACING /DRECOMP_POLLING %INC% ^
  "%PROJ%\runtime\runtime.c" "%PROJ%\runtime\sched_stateful.c" "%PROJ%\runtime\continuation.c" ^
  "%PROJ%\runtime\stateful_thread.c" "%PROJ%\runtime\rt64_backend.c" ^
  /Fo"%PROJ%\build\objs_stateful_runtime\\"
if errorlevel 1 exit /b 1
link /nologo /MAP:"%PROJ%\build\wpj2_stateful.map" ^
  /DEFAULTLIB:user32.lib /DEFAULTLIB:gdi32.lib /DEFAULTLIB:winmm.lib ^
  /OUT:"%PROJ%\build\wpj2_stateful.exe" "%PROJ%\build\objs_stateful_runtime\*.obj"
