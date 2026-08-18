@echo off
REM Executa com janela e teclado ligado ao controle do N64.
REM
REM Ate agora os botoes so existiam por variavel de ambiente, o que servia
REM para reproduzir uma sequencia mas nao para explorar menus. Com o teclado
REM da para navegar e confirmar visualmente em que tela o jogo esta.
REM
REM Uso:  JOGAR.bat        300 s (padrao)
REM       JOGAR.bat 600    dez minutos
setlocal
call "%~dp0tools\env.cmd"
cd /d "%PROJ%"

set WPJ2_TIMEOUT=%1
if "%WPJ2_TIMEOUT%"=="" set WPJ2_TIMEOUT=300
set WPJ2_WINDOW=1
set WPJ2_OUT=%PROJ%\lab\jogar_

REM Segundo argumento: "audio" liga a sintese completa. Sem ele, o padrao e
REM pular ADPCM/RESAMPLE/ENVMIX (WPJ2_AUDIO_FAST=1).
REM
REM Motivo: com a sintese completa, a medicao mostrou que o jogo faz UMA unica
REM leitura de controle e nunca mais pede outra - enquanto o teclado continua
REM entregando dezenas de eventos. Sem a sintese, as sondas mediam ~10 leituras
REM por segundo. A suspeita e que o custo do audio interpretado em C impede a
REM thread de controle de ser escalonada.
REM
REM Este e o teste que separa as duas causas:
REM   JOGAR.bat 300          audio leve  -> se o START responder, e escalonamento
REM   JOGAR.bat 300 audio    audio cheio -> reproduz o defeito observado
set WPJ2_AUDIO=1
set WPJ2_AUDIO_PLAY=1
if /i "%~2"=="audio" (
    set WPJ2_AUDIO_FAST=
) else (
    set WPJ2_AUDIO_FAST=1
)

echo.
echo  Wonder Project J2 - execucao com teclado
echo  ---------------------------------------
echo   Enter ........ START
echo   X / Espaco ... A
echo   Z ............ B
echo   C ............ Z (gatilho)
echo   A / S ........ L / R
echo   Setas ........ direcional
echo.
echo   F5 ... captura de quadro      F6 ... historico
echo   F2/F4  checkpoint             F11 .. alterna voz do audio
echo.
echo  Fecha sozinho em %WPJ2_TIMEOUT% s, ou feche a janela.
echo.
REM Grava tudo em arquivo alem da tela: o console e verboso e as linhas que
REM interessam ([pif] MUDOU, quando uma tecla e apertada) rolam para fora
REM antes de dar tempo de ler.
"%PROJ%\wpj2_probe.exe" "%ROM%" > "%PROJ%\lab\jogar.log" 2>&1
echo.
echo  --- teclas que chegaram ao runtime ---
findstr /C:"controle] botoes" "%PROJ%\lab\jogar.log" > "%PROJ%\lab\_teclas.txt"
find /c /v "zzz" < "%PROJ%\lab\_teclas.txt"
echo.
echo  --- leituras de controle que o JOGO pediu ---
findstr /C:"leitura=" "%PROJ%\lab\jogar.log" > "%PROJ%\lab\_leituras.txt"
find /c /v "zzz" < "%PROJ%\lab\_leituras.txt"
echo.
echo  --- valores que o jogo recebeu ---
findstr /C:"MUDOU" "%PROJ%\lab\jogar.log"
echo.
echo  Se as teclas forem dezenas e as leituras forem 1, o jogo parou de
echo  perguntar - o defeito e de escalonamento, nao de entrada.
echo.
echo  Log completo em lab\jogar.log
