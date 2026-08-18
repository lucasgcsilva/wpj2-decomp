@echo off
setlocal
call "%~dp0env.cmd"
cd /d "%PROJ%"
if not exist "%PROJ%\build\audio_oracle" mkdir "%PROJ%\build\audio_oracle"
cl /nologo /O2 /std:c++17 /EHsc /I "%RECOMP_INC%" /I "%PROJ%\RecompiledFuncs" /I "%PROJ%\runtime" /I "%PROJ%\runtime\third_party" /I "%PROJ%\tools\N64ModernRuntime-source\librecomp\include" ^
  /DWPJ2_AUDIO_ORACLE /Gy "%PROJ%\tools\audio_oracle_test.cpp" "%PROJ%\runtime\rsp.c" ^
  /Fe"%PROJ%\build\audio_oracle\audio_oracle_test.exe"
if errorlevel 1 exit /b 1
echo Pronto: build\audio_oracle\audio_oracle_test.exe
