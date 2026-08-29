@echo off
REM Gera a variante com continuations usada pelo build principal. Compila os
REM 37 fontes para validar a ABI/fluxo antes da etapa de link.
call "%~dp0env.cmd"
cd /d "%PROJ%"

"%WPJ2_PYTHON_DIR%\python.exe" "%PROJ%\tools\trace_inject.py" ^
  "%PROJ%\src\RecompiledFuncs" "%PROJ%\src\RecompiledFuncsTraced" ^
  "%PROJ%\native_overrides.txt"
if errorlevel 1 exit /b 1

"%WPJ2_PYTHON_DIR%\python.exe" "%PROJ%\src\scripts\injetar_continuacoes.py" ^
  "%PROJ%\src\RecompiledFuncsTraced" "%PROJ%\src\RecompiledFuncsStateful"
if errorlevel 1 exit /b 1

if exist "%PROJ%\build\objs" rmdir /s /q "%PROJ%\build\objs"
mkdir "%PROJ%\build\objs"
cl /nologo /c /O2 /std:c17 /MP /DRECOMP_STATEFUL /DRECOMP_TRACING /DRECOMP_POLLING /DWPJ2_GENERATED_CODE ^
  /I "%RECOMP_INC%" /I "%PROJ%\src\RecompiledFuncsStateful" ^
  /I "%PROJ%\runtime" ^
  "%PROJ%\src\RecompiledFuncsStateful\funcs_*.c" ^
  /Fo"%PROJ%\build\objs\\"
if errorlevel 1 exit /b 1

echo stateful codegen: objetos compilados com sucesso
exit /b 0
