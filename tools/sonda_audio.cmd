@echo off
REM Sonda dedicada de audio.
REM
REM A matriz do RODAR.bat roda quase toda com WPJ2_AUDIO_FAST=1, que por
REM desenho pula ADPCM/RESAMPLE/ENVMIX. Isso torna aquela matriz inutil para
REM investigar a sintese: o caminho medido nao e o caminho que produz o som.
REM Esta sonda garante o oposto - audio ligado e fast mode desligado.
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

set WPJ2_AUDIO=1
set WPJ2_AUDIO_FAST=

REM O paliativo no_wet e o padrao no runtime, mas aqui ele PRECISA ficar
REM desligado: medir divergencia com o reverb cortado compara um caminho que
REM a ROM nao executa, e o numero deixa de significar o que se quer.
set WPJ2_AUDIO_NO_WET=0
set WPJ2_TIMEOUT=%1
if "%WPJ2_TIMEOUT%"=="" set WPJ2_TIMEOUT=25

REM Segundo argumento: tarefa de audio para a bissecao nativo x HLE.
REM   0 = varre todas as listas musicais e para na primeira divergencia.
set WPJ2_BISSECAO=%2

REM Terceiro argumento: faixa da DMEM comparada, em hex "ini:fim".
REM Padrao aqui = os buses wet, onde a medicao localizou 70%% do offset DC.
REM Os buses se movem entre listas (wet_l ja apareceu em 0xC80..0xD00 e
REM wet_r em 0xDC0..0xE40, com count=0x140), entao a faixa cobre todos.
set WPJ2_BISSECAO_FAIXA=%3
if "%WPJ2_BISSECAO_FAIXA%"=="" set WPJ2_BISSECAO_FAIXA=C00:F80

REM Grava exatamente o PCM entregue ao driver. Serve para separar defeito de
REM sintese de defeito do caminho de saida: se o WAV estiver limpo e o alto
REM falante chiar, o problema e do WinMM/driver, nao do RSP.
if not exist "%PROJ%\temp\projeto\audio_sonda" mkdir "%PROJ%\temp\projeto\audio_sonda"
set WPJ2_AUDIO_WAV=%PROJ%\temp\projeto\audio_sonda\saida.wav
set WPJ2_AUDIO_PLAY=1

echo === sonda de audio: %WPJ2_TIMEOUT% s, fast mode desligado, bissecao=%WPJ2_BISSECAO% ===
"%PROJ%\wpj2_probe.exe" "%ROM%" > "%PROJ%\temp\projeto\audio_sonda\audio_env.log" 2>&1
echo saida: temp\projeto\audio_sonda\audio_env.log
findstr /C:"bissecao]" "%PROJ%\temp\projeto\audio_sonda\audio_env.log"
findstr /C:"envmix-destinos" "%PROJ%\temp\projeto\audio_sonda\audio_env.log"
