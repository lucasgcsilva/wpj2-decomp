@echo off
REM ===========================================================================
REM  Wonder Project J2 - entrada unica de teste interativo
REM
REM  Uso:  TESTAR.bat [perfil] [argumento]
REM
REM  Perfis:
REM    (nenhum)     padrao interativo: janela, teclado, audio completo
REM    input        sonda de entrada: botao preso, telemetria da cadeia
REM    audio        audio completo + WAV + metricas de DC/RMS
REM    sem_reverb   audio com o caminho wet cortado (menos chiado, ver 5h)
REM    sem_polef    POLEF em passagem direta
REM    voz N        isola a voz N do ENVMIXER (0..4)
REM    legendas     integracao PT-BR
REM    audio_rsp    microcodigo de audio recompilado no lugar do HLE C
REM    divergencia  varredura da bissecao HLE x microcodigo real
REM
REM  Toda saida vai para temp\projeto\<perfil>\ conforme ESTRUTURA.md.
REM  Fechar a janela encerra o teste. Nao ha limite de tempo.
REM ===========================================================================
setlocal EnableDelayedExpansion
call "%~dp0tools\env.cmd"
cd /d "%~dp0"

set "PERFIL=%~1"
if "%PERFIL%"=="" set "PERFIL=padrao"
set "ARG=%~2"

REM --- executavel -----------------------------------------------------------
REM wpj2_probe.exe e o alvo de tools\build_probe.cmd e o unico que carrega a
REM instrumentacao atual. Os demais existem para comparacao historica.
set "EXE=wpj2_probe.exe"
if /I "%PERFIL%"=="legendas"   set "EXE=build\wpj2_legendas_check.exe"
if /I "%PERFIL%"=="audio_rsp"  set "EXE=build\wpj2_audio_rsp_exato.exe"
if not exist "%~dp0%EXE%" (
  echo ERRO: %EXE% nao encontrado.
  echo Compile com: tools\build_probe.cmd
  pause
  exit /b 1
)

REM --- saida ----------------------------------------------------------------
set "SAIDA=%~dp0temp\projeto\%PERFIL%"
if /I "%PERFIL%"=="voz" set "SAIDA=%~dp0temp\projeto\voz_%ARG%"
if not exist "%SAIDA%" mkdir "%SAIDA%"
del /q "%SAIDA%\*.bmp" >nul 2>nul
del /q "%SAIDA%\*.log" >nul 2>nul
set "WPJ2_CAPTURE_DIR=%SAIDA%"
set "WPJ2_OUT=%SAIDA%\"
set "WPJ2_STATUS_FILE=%SAIDA%\status.txt"

REM --- base comum -----------------------------------------------------------
set "WPJ2_WINDOW=1"
set "WPJ2_TIMEOUT=0"
set "WPJ2_RETRACE=60"
set "WPJ2_AUDIO=1"
set "WPJ2_AUDIO_PLAY=1"
set "WPJ2_AUDIO_FAST=0"
set "WPJ2_AUDIO_VOICE=-1"
set "WPJ2_AUDIO_NO_WET=0"
set "WPJ2_AUDIO_BYPASS_POLEF=0"
set "WPJ2_AUDIO_DRY_ONLY=0"
set "WPJ2_AUDIO_MUSIC_ONLY=0"
set "WPJ2_NATIVE_AUDIO_RSP=0"
set "WPJ2_BISSECAO="
set "WPJ2_BUTTONS="
set "WPJ2_LEGENDAS="
set "WPJ2_WINDOW_TITLE=Wonder Project J2 - %PERFIL%"

REM --- perfis ---------------------------------------------------------------

REM Sonda de entrada. A cadeia PIF -> gContPad -> gControllerRaw ja foi
REM verificada como correta (ENTRADA_RETOMADA.md, 22/08); este perfil existe
REM para reconfirmar apos qualquer mexida e para observar a maquina de estado,
REM que e onde o defeito restante deve estar.
if /I "%PERFIL%"=="input" (
  set "WPJ2_AUDIO=0"
  set "WPJ2_AUDIO_FAST=1"
  set "WPJ2_BUTTONS=%ARG%"
  if "%ARG%"=="" set "WPJ2_BUTTONS=0x1000"
  set "WPJ2_TIMEOUT=25"
  set "WPJ2_WINDOW=0"
  set "WPJ2_WINDOW_TITLE=Wonder Project J2 - sonda de entrada"
)

REM Paliativo medido: o caminho wet responde por ~70%% do offset DC e e o que
REM mais reduz o chiado no teste auditivo. Nao e correcao (ver ANALISE_AUDIO.md).
if /I "%PERFIL%"=="sem_reverb" set "WPJ2_AUDIO_NO_WET=1"
if /I "%PERFIL%"=="sem_polef"  set "WPJ2_AUDIO_BYPASS_POLEF=1"

if /I "%PERFIL%"=="voz" (
  set "WPJ2_AUDIO_VOICE=%ARG%"
  set "WPJ2_WINDOW_TITLE=Wonder Project J2 - voz %ARG%"
)

if /I "%PERFIL%"=="legendas" (
  set "WPJ2_LEGENDAS=%~dp0textos\traducao_ptbr.tsv"
  set "WPJ2_LEGENDAS_LOG=%SAIDA%\legendas_rota.tsv"
)

if /I "%PERFIL%"=="audio_rsp" set "WPJ2_NATIVE_AUDIO_RSP=1"

REM Varredura da bissecao: compara o nosso HLE com o microcodigo real da ROM
REM sobre todas as listas musicais. IMPORTANTE: forca NO_WET=0, porque medir
REM divergencia com o reverb cortado compara um caminho que a ROM nao executa.
if /I "%PERFIL%"=="divergencia" (
  set "WPJ2_AUDIO_NO_WET=0"
  set "WPJ2_AUDIO_FAST=0"
  set "WPJ2_BISSECAO=-2"
  set "WPJ2_BISSECAO_FAIXA=5C0:F80"
  set "WPJ2_TIMEOUT=30"
  set "WPJ2_WINDOW=0"
)

REM WAV para analise posterior, exceto na sonda de entrada.
if /I not "%PERFIL%"=="input" set "WPJ2_AUDIO_WAV=%SAIDA%\audio.wav"

REM --- execucao -------------------------------------------------------------
echo.
echo   Wonder Project J2 - perfil: %PERFIL%
echo   ------------------------------------------------
if /I not "%PERFIL%"=="input" if /I not "%PERFIL%"=="divergencia" (
  echo    Enter=START   X/Espaco=A   Z=B   C=Z   A/S=L/R   Setas=direcional
  echo    F5 captura   F6 historico   F2/F4 checkpoint   F11 voz do audio
  echo.
  echo    Feche a janela para encerrar.
)
echo   Saida: %SAIDA%
echo.

"%~dp0%EXE%" "%ROM%" > "%SAIDA%\execucao.log" 2>&1
set "SAIDA_CODE=%errorlevel%"

REM --- resumo ---------------------------------------------------------------
echo.
echo   --- resumo ---
if /I "%PERFIL%"=="input" (
  echo   [cadeia de entrada]
  REM O despejo da estrutura de 40 bytes fica na linha seguinte ao cabecalho,
  REM entao os dois padroes precisam entrar no filtro.
  findstr /C:"ctrl-raw" /C:"raw[40]" "%SAIDA%\execucao.log"
  echo   [estado da maquina]
  findstr /C:"subestado principal" "%SAIDA%\execucao.log"
)
if /I "%PERFIL%"=="divergencia" (
  findstr /C:"populacao" "%SAIDA%\execucao.log"
)
if exist "%SAIDA%\audio.wav" (
  "%WPJ2_PYTHON_DIR%\python.exe" "%~dp0src\scripts\resumo_audio.py" "%SAIDA%\audio.wav" 2>nul
)
echo.
echo   Log completo: %SAIDA%\execucao.log
echo.
echo   Depois de analisar, esvazie temp\ conforme ESTRUTURA.md.
exit /b %SAIDA_CODE%
