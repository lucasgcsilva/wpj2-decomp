@echo off
REM Constroi wpj2_probe.exe: o CPU recompilado mais um runtime minimo que
REM registra cada funcao alcancada e cada bloco de MMIO tocado.
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

REM A tabela de lookup sai de funcs.h, entao envelhece a cada renomeacao de
REM funcao. Regenerar sempre e mais barato que depurar um simbolo fantasma.
echo === regenerando a tabela de funcoes ===
python "%PROJ%\tools\gen_table.py" "%PROJ%\src\RecompiledFuncs\funcs.h" "%PROJ%\runtime\func_table.c"
if errorlevel 1 exit /b 1

echo === injetando hooks de traco ===
python "%PROJ%\tools\trace_inject.py" "%PROJ%\src\RecompiledFuncs" "%PROJ%\src\RecompiledFuncsTraced" "%PROJ%\native_overrides.txt"
if errorlevel 1 exit /b 1

if exist "%PROJ%\build\objp" rmdir /s /q "%PROJ%\build\objp"
mkdir "%PROJ%\build\objp" 2>nul

set INC=/I "%RECOMP_INC%" /I "%PROJ%\src\RecompiledFuncsTraced" /I "%PROJ%\runtime"
set FLAGS=/nologo /c /O1 /std:c17 /MP /DRECOMP_TRACING /DRECOMP_POLLING /wd4101 /wd4102 /wd4189

echo === compilando o CPU recompilado ===
cl %FLAGS% %INC% "%PROJ%\src\RecompiledFuncsTraced\funcs_*.c" /Fo"%PROJ%\build\objp\\" >nul
if errorlevel 1 (echo FALHOU A COMPILACAO DOS FONTES RECOMPILADOS & exit /b 1)

echo === compilando o runtime ===
cl /nologo /c /O2 /std:c17 /EHa /MP /DRECOMP_TRACING /DRECOMP_POLLING %INC% ^
   "%PROJ%\runtime\runtime.c" "%PROJ%\runtime\sched.c" "%PROJ%\runtime\hle.c" ^
   "%PROJ%\runtime\pif.c" "%PROJ%\runtime\mempak.c" "%PROJ%\runtime\rsp.c" "%PROJ%\runtime\video.c" "%PROJ%\runtime\audio.c" ^
   "%PROJ%\runtime\legendas.c" ^
   "%PROJ%\runtime\func_table.c" ^
   /Fo"%PROJ%\build\objp\\"
if errorlevel 1 (echo FALHOU O RUNTIME & exit /b 1)

REM O microcodigo de audio recompilado pelo RSPRecomp e a ponte que o expoe.
REM Sao C++ e nao podem entrar na linha acima, que usa /std:c17. Sem eles o
REM link falha em wpj2_native_audio_rsp, referenciado por run_acmd_list.
echo === compilando o RSP nativo (C++) ===
cl /nologo /c /O2 /std:c++20 /EHsc /MP %INC% ^
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
