@echo off
REM Quatro gravacoes do mesmo trecho, isolando o caminho de reverb/eco.
REM
REM O relato e: musica correta por baixo, chiado intermitente com "pipoco",
REM suspeita de reverb. "Pipoco" e descontinuidade ou saturacao, nao ruido de
REM banda larga - e o caminho molhado (wet) somado ao seco e justamente onde
REM energia extra pode estourar o teto de 16 bits.
REM
REM Se o chiado sumir em no_wet  -> os envios de reverb do ENVMIXER.
REM Se sumir em sem_polef        -> o filtro recursivo POLEF.
REM Se sumir so em seco          -> a soma dos dois.
REM Se nao sumir em nenhum       -> nao e reverb; a suspeita cai.
call "E:\projetos\project-wonder-j2-decomp\tools\env.cmd"
cd /d "%PROJ%"

set DUR=%1
if "%DUR%"=="" set DUR=30

for %%V in (base no_wet sem_polef seco) do (
    set WPJ2_AUDIO_NO_WET=
    set WPJ2_AUDIO_BYPASS_POLEF=
    set WPJ2_AUDIO_DRY_ONLY=
    call :rodar %%V
)
echo.
echo Pronto. Compare em lab\ab_*.wav
dir "%PROJ%\lab\ab_*.wav"
goto :eof

:rodar
setlocal
if "%1"=="no_wet"    set WPJ2_AUDIO_NO_WET=1
if "%1"=="sem_polef" set WPJ2_AUDIO_BYPASS_POLEF=1
if "%1"=="seco"      set WPJ2_AUDIO_DRY_ONLY=1
set WPJ2_AUDIO=1
set WPJ2_AUDIO_PLAY=1
set WPJ2_AUDIO_FAST=
set WPJ2_TIMEOUT=%DUR%
set WPJ2_AUDIO_WAV=%PROJ%\lab\ab_%1.wav
echo === variante %1 (%DUR% s) ===
"%PROJ%\wpj2_probe.exe" "%ROM%" > "%PROJ%\lab\ab_%1.log" 2>&1
endlocal
goto :eof
