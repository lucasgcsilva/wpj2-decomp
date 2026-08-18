@echo off
REM Compila a saida crua do N64Recomp em wpj2_recompiled.lib.
REM
REM Este alvo existe para responder uma pergunta so: o C gerado e valido? Ele
REM nao linka nem executa nada, entao um erro aqui e sempre do recompilador ou
REM dos limites de funcao nos simbolos, nunca do runtime.
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

if exist "%PROJ%\build\obj" rmdir /s /q "%PROJ%\build\obj"
mkdir "%PROJ%\build\obj" 2>nul

set INC=/I "%RECOMP_INC%" /I "%PROJ%\RecompiledFuncs" /I "%PROJ%\runtime"
set FLAGS=/nologo /c /O1 /std:c17 /MP /wd4101 /wd4102 /wd4189

echo === compilando RecompiledFuncs ===
cl %FLAGS% %INC% "%PROJ%\RecompiledFuncs\funcs_*.c" /Fo"%PROJ%\build\obj\\"
if errorlevel 1 (echo FALHOU A COMPILACAO DOS FONTES RECOMPILADOS & exit /b 1)

echo === arquivando ===
lib /nologo /OUT:"%PROJ%\build\wpj2_recompiled.lib" "%PROJ%\build\obj\*.obj"
if errorlevel 1 (echo FALHOU O LIB & exit /b 1)

echo === OK ===
dir "%PROJ%\build\wpj2_recompiled.lib"
