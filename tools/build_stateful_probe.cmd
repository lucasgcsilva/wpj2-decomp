@echo off
REM Build isolado do runtime sem fibers para diagnostico. O build principal
REM usa a mesma arquitetura e gera wpj2_probe.exe.
call "%~dp0env.cmd"
cd /d "%PROJ%"

call "%PROJ%\tools\build_stateful_codegen.cmd"
if errorlevel 1 exit /b 1

if exist "%PROJ%\build\objs_stateful_runtime" rmdir /s /q "%PROJ%\build\objs_stateful_runtime"
mkdir "%PROJ%\build\objs_stateful_runtime" 2>nul
set INC=/I "%RECOMP_INC%" /I "%PROJ%\src\RecompiledFuncsStateful" /I "%PROJ%\runtime"
set FLAGS=/nologo /c /O2 /std:c17 /EHa /MP /DRECOMP_STATEFUL /DRECOMP_TRACING /DRECOMP_POLLING /wd4101 /wd4102 /wd4189

cl %FLAGS% %INC% ^
  "%PROJ%\runtime\runtime.c" "%PROJ%\runtime\sched_stateful.c" ^
  "%PROJ%\runtime\continuation.c" "%PROJ%\runtime\stateful_thread.c" ^
  "%PROJ%\runtime\hle.c" "%PROJ%\runtime\pif.c" "%PROJ%\runtime\mempak.c" ^
  "%PROJ%\runtime\rsp.c" "%PROJ%\runtime\video.c" "%PROJ%\runtime\audio.c" ^
  "%PROJ%\runtime\rt64_backend.c" "%PROJ%\runtime\legendas.c" ^
  "%PROJ%\runtime\func_table.c" /Fo"%PROJ%\build\objs_stateful_runtime\\"
if errorlevel 1 exit /b 1

cl /nologo /c /O2 /std:c++20 /EHsc /MP /DRECOMP_STATEFUL /DRECOMP_TRACING /DRECOMP_POLLING %INC% ^
  /FI"%PROJ%\runtime\rsp_safe_dma.h" ^
  /I "%PROJ%\tools\N64ModernRuntime-source\librecomp\include" ^
  /I "%PROJ%\tools\N64ModernRuntime-source\ultramodern\include" ^
  "%PROJ%\runtime\rsp_native.cpp" "%PROJ%\src\gerado\rsp_audio\rsp_audio_recompiled.cpp" ^
  /Fo"%PROJ%\build\objs_stateful_runtime\\"
if errorlevel 1 exit /b 1

copy /y "%PROJ%\build\objs\*.obj" "%PROJ%\build\objs_stateful_runtime\" >nul
link /nologo /MAP:"%PROJ%\build\wpj2_stateful.map" ^
  /DEFAULTLIB:user32.lib /DEFAULTLIB:gdi32.lib /DEFAULTLIB:winmm.lib ^
  /OUT:"%PROJ%\build\wpj2_stateful.exe" "%PROJ%\build\objs_stateful_runtime\*.obj"
if errorlevel 1 exit /b 1
echo stateful runtime: %PROJ%\build\wpj2_stateful.exe
