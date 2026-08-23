@echo off
REM Sonda de entrada: observa o que a LOGICA DO JOGO recebe, nao o que o PIF
REM entrega.
REM
REM Ja esta medido que o PIF devolve 0x1000 em toda leitura de controle. O que
REM faltava era o outro lado. Com os simbolos da decompilacao de referencia
REM (tools/wonder-source), agora sabemos onde olhar:
REM
REM   gContPad        = 0x80182558   estado do controle visto pelo jogo
REM   gControllerRaw  = 0x80180DA8
REM   __osContPifRam  = 0x801AFB40   fita joybus (o nosso PIF ja logava isso)
REM
REM Se gContPad mostrar 0x1000 e o titulo nao reagir, o defeito esta na logica
REM do jogo. Se mostrar zero, esta entre o PIF e o osContGetReadData.
setlocal
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

set "SAIDA=%PROJ%\temp\projeto\input"
if not exist "%SAIDA%" mkdir "%SAIDA%"

set WPJ2_TIMEOUT=%1
if "%WPJ2_TIMEOUT%"=="" set WPJ2_TIMEOUT=25
REM Botao preso desde o primeiro quadro: elimina a variavel do roteiro e do
REM instante do toque.
set WPJ2_BUTTONS=%2
if "%WPJ2_BUTTONS%"=="" set WPJ2_BUTTONS=0x1000
set WPJ2_AUDIO=
set WPJ2_AUDIO_FAST=1
set WPJ2_OUT=%SAIDA%\

echo === sonda de entrada: %WPJ2_TIMEOUT% s, botoes=%WPJ2_BUTTONS% ===
"%PROJ%\wpj2_probe.exe" "%ROM%" > "%SAIDA%\input.log" 2>&1

echo.
echo --- o que o jogo recebeu ---
findstr /C:"gContPad" /C:"gControllerRaw" "%SAIDA%\input.log"
echo.
echo --- o que o PIF entregou ---
findstr /C:"MUDOU" "%SAIDA%\input.log"
echo.
echo --- estado da maquina ---
findstr /C:"subestado principal" "%SAIDA%\input.log"
echo.
echo Log completo: %SAIDA%\input.log
