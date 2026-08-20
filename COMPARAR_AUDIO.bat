@echo off
REM ============================================================
REM  Captura pareada de audio e comparacao com o oraculo
REM
REM  Roda a mesma cena tres vezes, mudando so o tratamento da fila
REM  de saida, e compara cada captura com o WAV do Project64.
REM
REM    sem_espera  descarta na hora     (comportamento antigo)
REM    espera12    espera ate 12 ms     (novo padrao)
REM    espera40    espera ate 40 ms     (limite superior)
REM
REM  Se a correlacao com o oraculo parar de despencar depois dos
REM  primeiros segundos, a causa era o descarte.
REM
REM  Uso:  COMPARAR_AUDIO.bat [segundos]
REM ============================================================
setlocal
call "%~dp0tools\env.cmd"
cd /d "%~dp0"
if "%~1"=="" (set DUR=25) else (set DUR=%~1)
set "WPJ2_TEMP=%~dp0temp\projeto\comparar_audio"
if not exist "%WPJ2_TEMP%" mkdir "%WPJ2_TEMP%"

echo.
echo  Captura pareada - %DUR% s por corrida
echo  ------------------------------------
echo.
echo  Recompilando wpj2_visual.exe com a nova fila de audio...
call "%~dp0tools\build_visual.cmd" > "%WPJ2_TEMP%\build_audio.log" 2>&1
if errorlevel 1 (
   echo  A BUILD FALHOU. Veja temp\projeto\comparar_audio\build_audio.log
   pause
   exit /b 1
)
echo  Build ok.
echo.

python "%~dp0tools\comparar_lote.py" %DUR%

echo.
echo  Pronto. Resultados em temp\projeto\comparar_audio\.
echo.
pause
