@echo off
REM Versao para demonstracao visual: mantem RECOMP_POLLING, indispensavel ao
REM escalonador cooperativo. Por padrao e uma compilacao de liberacao: quando
REM WPJ2_DEBUG=0, a telemetria nao produz console, arquivos ou custo de I/O.
REM Para investigar uma regressao especifica, defina WPJ2_VISUAL_DEBUG_BUILD=1
REM antes de chamar este script.
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"
if "%~1"=="" (
    set "WPJ2_EXE=%PROJ%\wpj2_visual.exe"
    set "WPJ2_MAP=%PROJ%\build\wpj2_visual.map"
) else (
    set "WPJ2_EXE=%~f1"
    set "WPJ2_MAP=%~dpn1.map"
)

echo === preparando CPU visual ===
python "%PROJ%\tools\gen_table.py" "%PROJ%\src\RecompiledFuncs\funcs.h" "%PROJ%\runtime\func_table.c"
if errorlevel 1 exit /b 1
python "%PROJ%\tools\trace_inject.py" "%PROJ%\src\RecompiledFuncs" "%PROJ%\src\RecompiledFuncsTraced" "%PROJ%\native_overrides.txt"
if errorlevel 1 exit /b 1

if exist "%PROJ%\build\objv" rmdir /s /q "%PROJ%\build\objv"
mkdir "%PROJ%\build\objv" 2>nul
set INC=/I "%RECOMP_INC%" /I "%PROJ%\src\RecompiledFuncsTraced" /I "%PROJ%\runtime"
set "WPJ2_BUILD_MODE=/DWPJ2_RELEASE"
if not "%WPJ2_VISUAL_DEBUG_BUILD%"=="" set "WPJ2_BUILD_MODE="
set "WPJ2_TRACE_MODE="
if not "%WPJ2_TRACE_BUILD%"=="" set "WPJ2_TRACE_MODE=/DRECOMP_TRACING"
set FLAGS=/nologo /c /O2 /std:c17 /MP /DRECOMP_POLLING %WPJ2_BUILD_MODE% /wd4101 /wd4102 /wd4189
if not "%WPJ2_TRACE_MODE%"=="" set "FLAGS=%FLAGS% %WPJ2_TRACE_MODE%"

echo === compilando CPU visual ===
cl %FLAGS% %INC% "%PROJ%\src\RecompiledFuncsTraced\funcs_*.c" /Fo"%PROJ%\build\objv\\" >nul
if errorlevel 1 exit /b 1
echo === compilando runtime visual ===
cl /nologo /c /O2 /std:c17 /EHa /MP /DRECOMP_POLLING %WPJ2_BUILD_MODE% %WPJ2_TRACE_MODE% %INC% ^
   "%PROJ%\runtime\runtime.c" "%PROJ%\runtime\sched.c" "%PROJ%\runtime\hle.c" ^
   "%PROJ%\runtime\legendas.c" ^
   "%PROJ%\runtime\pif.c" "%PROJ%\runtime\mempak.c" "%PROJ%\runtime\rsp.c" "%PROJ%\runtime\video.c" "%PROJ%\runtime\audio.c" ^
   "%PROJ%\runtime\rt64_backend.c" ^
   "%PROJ%\runtime\func_table.c" /Fo"%PROJ%\build\objv\\"
if errorlevel 1 exit /b 1
echo === compilando microcodigo RSP de audio ===
cl /nologo /c /O2 /std:c++17 /EHsc /MP /DRECOMP_POLLING %WPJ2_BUILD_MODE% %WPJ2_TRACE_MODE% %INC% ^
   /I "%PROJ%\runtime\third_party" /I "%PROJ%\tools\N64ModernRuntime-source\librecomp\include" ^
   "%PROJ%\runtime\rsp_native.cpp" "%PROJ%\src\gerado\rsp_audio\rsp_audio_recompiled.cpp" /Fo"%PROJ%\build\objv\\"
if errorlevel 1 exit /b 1
link /nologo /MAP:"%WPJ2_MAP%" /DEFAULTLIB:user32.lib /DEFAULTLIB:gdi32.lib /DEFAULTLIB:winmm.lib ^
     /OUT:"%WPJ2_EXE%" "%PROJ%\build\objv\*.obj"
if errorlevel 1 exit /b 1
dir "%WPJ2_EXE%"
