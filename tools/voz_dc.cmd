@echo off
REM Isola vozes e grava o PCM de cada uma, para medir o DC individual.
REM
REM A medicao de 5i mostrou que o offset e proporcional a atividade: some
REM quando ha poucas vozes, salta para ~800 quando elas dobram. Se UMA voz
REM sozinha ja produzir media nao-nula, a origem esta no decodificador ADPCM
REM ou na mistura por voz - nao no acumulo entre vozes.
REM
REM WPJ2_AUDIO_VOICE=-1 (ou ausente) deixa a mistura completa;
REM 0..N deixa passar apenas aquela voz aos buses.
setlocal
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

set WPJ2_AUDIO=1
set WPJ2_AUDIO_PLAY=1
set WPJ2_AUDIO_FAST=
set WPJ2_AUDIO_NO_WET=0
set WPJ2_TIMEOUT=22

for %%V in (0 1 2 3) do (
    set WPJ2_AUDIO_VOICE=%%V
    call :uma %%V
)
echo.
echo Pronto. WAVs em temp\projeto\audio_vozes\voz_*.wav
goto :eof

:uma
setlocal
set WPJ2_AUDIO_VOICE=%1
if not exist "%PROJ%\temp\projeto\audio_vozes" mkdir "%PROJ%\temp\projeto\audio_vozes"
set WPJ2_AUDIO_WAV=%PROJ%\temp\projeto\audio_vozes\voz_%1.wav
echo === voz %1 ===
"%PROJ%\wpj2_probe.exe" "%ROM%" > "%PROJ%\temp\projeto\audio_vozes\voz_%1.log" 2>&1
endlocal
goto :eof
