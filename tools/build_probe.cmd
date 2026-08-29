@echo off
REM Constroi wpj2_probe.exe com continuacoes serializaveis. O estado das
REM OSThreads deixa de depender de fibers do Windows, permitindo F2/F4 real.
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

REM A tabela de lookup sai de funcs.h, entao envelhece a cada renomeacao de
REM funcao. Regenerar sempre e mais barato que depurar um simbolo fantasma.
echo === regenerando a tabela de funcoes ===
python "%PROJ%\tools\gen_table.py" "%PROJ%\src\RecompiledFuncs\funcs.h" "%PROJ%\runtime\func_table.c"
if errorlevel 1 exit /b 1

echo === gerando CPU stateful ===
call "%PROJ%\tools\build_stateful_codegen.cmd"
if errorlevel 1 exit /b 1

if exist "%PROJ%\build\objp" rmdir /s /q "%PROJ%\build\objp"
mkdir "%PROJ%\build\objp" 2>nul

set INC=/I "%RECOMP_INC%" /I "%PROJ%\src\RecompiledFuncsStateful" /I "%PROJ%\runtime"
set FLAGS=/nologo /c /O2 /std:c17 /MP /DRECOMP_STATEFUL /DRECOMP_TRACING /DRECOMP_POLLING /wd4101 /wd4102 /wd4189

echo === copiando o CPU recompilado ===
copy /y "%PROJ%\build\objs\*.obj" "%PROJ%\build\objp\" >nul
if errorlevel 1 (echo FALHOU A COPIA DOS OBJETOS RECOMPILADOS & exit /b 1)

echo === compilando o runtime ===
REM Fontes comuns em tools\runtime_sources.cmd. Aqui ficam apenas as que sao
REM exclusivas deste build: o escalonador stateful e as continuacoes.
call "%PROJ%\tools\runtime_sources.cmd"
cl /nologo /c /O2 /std:c17 /EHa /MP /DRECOMP_STATEFUL /DRECOMP_TRACING /DRECOMP_POLLING %INC% ^
   %RUNTIME_COMUM% ^
   "%PROJ%\runtime\sched_stateful.c" ^
   "%PROJ%\runtime\continuation.c" "%PROJ%\runtime\stateful_thread.c" ^
   /Fo"%PROJ%\build\objp\\"
if errorlevel 1 (echo FALHOU O RUNTIME & exit /b 1)

REM O microcodigo de audio recompilado pelo RSPRecomp e a ponte que o expoe.
REM Sao C++ e nao podem entrar na linha acima, que usa /std:c17. Sem eles o
REM link falha em wpj2_native_audio_rsp, referenciado por run_acmd_list.
echo === compilando o RSP nativo (C++) ===
cl /nologo /c /O2 /std:c++20 /EHsc /MP /DRECOMP_STATEFUL /DRECOMP_TRACING /DRECOMP_POLLING %INC% ^
   /FI"%PROJ%\runtime\rsp_safe_dma.h" ^
   /I "%PROJ%\tools\N64ModernRuntime-source\librecomp\include" ^
   /I "%PROJ%\tools\N64ModernRuntime-source\ultramodern\include" ^
   "%PROJ%\runtime\rsp_native.cpp" "%PROJ%\src\gerado\rsp_audio\rsp_audio_recompiled.cpp" ^
   /Fo"%PROJ%\build\objp\\"
if errorlevel 1 (echo FALHOU O RSP NATIVO & exit /b 1)

echo === linkando ===
REM O mapa e o que transforma "falhou no RVA 0x4D6A" em um nome de funcao.
link /nologo /MAP:"%PROJ%\build\wpj2_probe.map" /DEFAULTLIB:user32.lib /DEFAULTLIB:gdi32.lib ^
     /OUT:"%PROJ%\wpj2_probe.exe" "%PROJ%\build\objp\*.obj"
if errorlevel 1 exit /b 1
dir "%PROJ%\wpj2_probe.exe"

if /I "%~1"=="rt64" (
  echo === configurando a ponte RT64 nativa ===
  if not exist "C:\PROGRA~1\MIB055~1\2022\COMMUN~1\Common7\IDE\COMMON~1\MICROS~1\CMake\Ninja\ninja.exe" (
    echo Ninja do Visual Studio nao encontrado.
    exit /b 1
  )
  cmake -S "%PROJ%\src\rt64_bridge" -B "%PROJ%\build\rt64_bridge_native2" -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=C:\PROGRA~1\MIB055~1\2022\COMMUN~1\Common7\IDE\COMMON~1\MICROS~1\CMake\Ninja\ninja.exe"
  if errorlevel 1 exit /b 1
  echo === compilando a ponte RT64 nativa ===
  cmake --build "%PROJ%\build\rt64_bridge_native2" --target wpj2_rt64_bridge --parallel
  if errorlevel 1 exit /b 1
  dir "%PROJ%\build\rt64_runtime\wpj2_rt64_bridge.dll"
)
