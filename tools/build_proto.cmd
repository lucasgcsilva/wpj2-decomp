@echo off
REM Gera uma versao imutavel de prototipo no caminho informado.
REM A release nao coleta diagnostico nem gera dumps por padrao.
if "%~1"=="" (
    echo Uso: build_proto.cmd caminho\do\wpj2_proto_vX.Y.exe
    exit /b 2
)
set "WPJ2_RELEASE_BUILD=1"
call "%~dp0build_visual.cmd" "%~f1"
exit /b %errorlevel%
