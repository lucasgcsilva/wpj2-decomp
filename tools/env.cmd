@echo off
REM Ambiente de build compartilhado do project-wonder-j2-decomp.
REM
REM Protegido contra chamada dupla: cada chamada do vcvars64 acrescenta
REM diretorios ao PATH, e encadear scripts que o chamam estoura o limite da
REM linha de comando ("the input line is too long").
if defined WPJ2_ENV_READY goto :eof
set WPJ2_ENV_READY=1
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
set "PROJ=E:\projetos\project-wonder-j2-decomp"
set "RECOMP_INC=%PROJ%\tools\N64Recomp-source\include"
set "ROM=E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64"
REM O Codex fornece um Python isolado; expô-lo no PATH mantém RODAR.bat
REM reproduzível mesmo quando o Python de usuário não estiver disponível.
set "WPJ2_PYTHON_DIR=C:\Users\lucas\.cache\codex-runtimes\codex-primary-runtime\dependencies\python"
if exist "%WPJ2_PYTHON_DIR%\python.exe" set "PATH=%WPJ2_PYTHON_DIR%;%PATH%"
goto :eof

:eof
