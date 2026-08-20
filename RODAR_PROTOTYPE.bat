@echo off
setlocal
REM Prototipo interativo v0.2: abertura 2D e primeira cena 3D.
REM Uso: RODAR_PROTOTYPE.bat [segundos]
REM A cadencia e NTSC (60 Hz): sem fast-forward, START injetado ou salto de
REM estado. O modo AUDIO_FAST so evita a sintese PCM, preservando tarefas,
REM interrupcoes e o fluxo logico do jogo.
set "WPJ2_WINDOW=1"
set "WPJ2_WINDOW_HOLD_LAST=0"
set "WPJ2_RETRACE=60"
set "WPJ2_FRAME_SAMPLES=1"
set "WPJ2_TEMP=%~dp0temp\projeto\prototype"
if not exist "%WPJ2_TEMP%" mkdir "%WPJ2_TEMP%"
set "WPJ2_OUT=%WPJ2_TEMP%\prototype_v0.2_"
set "WPJ2_AUDIO_FAST=1"
REM A abertura usa texturas CI8; nesta rota o carregamento linear da TMEM e o
REM comportamento correto. A intercalação continua ligada no wpj2_visual 3D.
set "WPJ2_TMEM_INTERLEAVE=1"
set "WPJ2_AUTOCUTSCENE=0"
set "WPJ2_HOLD_STATE_8_1=0"
set "WPJ2_HOLD_STATE_12_50=1"
set "WPJ2_SECONDS=%~1"
if "%WPJ2_SECONDS%"=="" set "WPJ2_SECONDS=120"
set "WPJ2_PROTO=%~dp0prototipos\wpj2_proto_v0.2_abertura3d_20260811.exe"

REM Este executavel e congelado entre validacoes: nunca recompilar por conta
REM propria. A geracao de uma nova versao e feita somente por solicitacao.
if not exist "%WPJ2_PROTO%" (
    echo ERRO: prototipo v0.2 nao encontrado. Solicite uma nova compilacao do prototipo.
    pause
    exit /b 1
)
if "%WPJ2_DEBUG%"=="1" (
    "%WPJ2_PROTO%" "E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64" %WPJ2_SECONDS% > "%WPJ2_TEMP%\prototype_v0.2_.console.txt" 2>&1
) else (
    "%WPJ2_PROTO%" "E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64" %WPJ2_SECONDS%
)
set "WPJ2_EXIT=%errorlevel%"
echo.
if "%WPJ2_DEBUG%"=="1" (
    echo Prototipo v0.2 encerrado apos %WPJ2_SECONDS% segundo(s). Log: temp\projeto\prototype\prototype_v0.2_.console.txt
) else (
    echo Prototipo v0.2 encerrado apos %WPJ2_SECONDS% segundo(s). Modo release: sem logs ou dumps.
)
pause
exit /b %WPJ2_EXIT%
