@echo off
REM Sonda da sequencia: titulo -> START -> menu "Start" -> A -> selecao de save.
REM
REM Usa WPJ2_INPUT_POLLS (roteiro por leitura de controle) em vez de
REM WPJ2_INPUT (roteiro em milissegundos). O roteiro por leitura e
REM deterministico: conta os CMD_READ_BTN que o proprio jogo faz, entao nao
REM depende do escalonador do host nem da velocidade da maquina.
REM
REM Botoes, em hexadecimal (formato de 16 bits do controle N64):
REM   A=8000  B=4000  Z=2000  START=1000
REM   Dup=0800 Ddown=0400 Dleft=0200 Dright=0100
REM   L=0020  R=0010  Cup=0008 Cdown=0004 Cleft=0002 Cright=0001
REM
REM Uso:
REM   sequencia_menu.cmd 25                 calibra, sem entrada
REM   sequencia_menu.cmd 25 "300:1000;303:0000"   aperta START na leitura 300
REM setlocal e obrigatorio: sem ele o WPJ2_INPUT_POLLS definido aqui vazava
REM para o console e continuava valendo em execucoes seguintes. Um roteiro
REM gravado faz o botoes_agora() ignorar o teclado por completo, e quem
REM rodasse o TESTAR.bat na mesma janela nao conseguia apertar nada.
setlocal
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

set DUR=%1
if "%DUR%"=="" set DUR=25
set WPJ2_TIMEOUT=%DUR%

REM Os roteiros ficam AQUI, nao como argumento: o ';' separa comandos no
REM PowerShell e a linha chegava truncada no primeiro passo.
REM
REM Calibracao observada sem entrada:
REM   estado 8/1  (logo ENIX) -> 8/26 (titulo, espera START) por volta da
REM   atualizacao 577; segurar START levou direto a 12/50, atravessando
REM   titulo, menu e selecao de save de uma vez.
set ROTEIRO=
if /i "%~2"=="start"   set ROTEIRO=600:1000;612:0000
if /i "%~2"=="start_a" set ROTEIRO=600:1000;612:0000;750:8000;762:0000
if /i "%~2"=="start_aa" set ROTEIRO=600:1000;612:0000;750:8000;762:0000;900:8000;912:0000
if /i "%~2"=="segura"  set ROTEIRO=300:1000
set WPJ2_INPUT_POLLS=%ROTEIRO%

REM "fixo" nao usa roteiro: prende o botao desde o primeiro quadro. Serve para
REM separar "o contador de leituras nunca chega ao passo" de "o botao nao
REM chega ao jogo". Se nem assim o titulo reage, o defeito e no caminho da
REM entrada, nao no roteiro.
set WPJ2_BUTTONS=
if /i "%~2"=="fixo_start" set WPJ2_BUTTONS=0x1000
if /i "%~2"=="fixo_a"     set WPJ2_BUTTONS=0x8000

REM Terceiro argumento: taxa de retrace. Serve para testar se a cadencia de
REM leitura do controle esta atrelada ao retrace: medimos ~10 leituras/s com
REM retrace de 60 Hz, ou seja seis retraces por leitura. Se dobrar o retrace
REM dobrar as leituras, a conclusao do SI esta presa ao evento de video.
if not "%~3"=="" set WPJ2_RETRACE=%~3

REM Quadros salvos permitem ver em qual tela paramos, sem abrir janela.
if not exist "%PROJ%\temp\projeto\menu" mkdir "%PROJ%\temp\projeto\menu"
set WPJ2_OUT=%PROJ%\temp\projeto\menu\menu_
set WPJ2_FRAME_SAMPLES=8
set WPJ2_AUDIO=
set WPJ2_AUDIO_FAST=1

echo === sequencia de menu: %DUR% s, roteiro="%~2" ===
"%PROJ%\wpj2_probe.exe" "%ROM%" > "%PROJ%\temp\projeto\menu\menu.log" 2>&1
echo.
findstr /C:"entr :" /C:"estado" /C:"leituras de controle" "%PROJ%\temp\projeto\menu\menu.log"
echo --- quadros gerados ---
dir /b "%PROJ%\temp\projeto\menu\menu_*.ppm" 2>nul
