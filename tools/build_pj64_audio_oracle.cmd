@echo off
setlocal
REM Compila somente o plugin de audio-oraculo em Win32. O ambiente recebido
REM pelo Codex pode conter PATH e Path simultaneamente, algo que o MSBuild nao
REM aceita; vcvars32 recria uma unica variavel coerente para este processo.
set "PATH="
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
set "PATH=C:\Program Files\Git\cmd;%PATH%"
set "GIT_CONFIG_COUNT=1"
set "GIT_CONFIG_KEY_0=safe.directory"
set "GIT_CONFIG_VALUE_0=E:/projetos/project-wonder-j2-decomp/tools/Project64-source"
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  "%~dp0Project64-source\Source\Project64-audio\Project64-audio.vcxproj" ^
  /m /p:Configuration=Release /p:Platform=Win32 /verbosity:minimal
exit /b %errorlevel%
