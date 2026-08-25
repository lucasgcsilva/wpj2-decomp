@echo off
REM ===========================================================================
REM  Wonder Project J2 - entrada unica de teste interativo
REM
REM  Uso:  TESTAR.bat [perfil] [argumento]
REM
REM  As legendas PT-BR estao LIGADAS POR PADRAO em todos os perfis.
REM
REM  Perfis:
REM    (nenhum)      padrao interativo: audio fiel, janela, teclado e PT-BR
REM    input         sonda de entrada: botao preso, telemetria da cadeia
REM    input_janela  o mesmo, mas com janela: mede o caminho do TECLADO
REM    audio         audio completo + WAV + metricas de DC/RMS
REM    sem_reverb    audio com o caminho wet cortado (menos chiado, ver 5h)
REM    sem_polef     POLEF em passagem direta
REM    voz N         isola a voz N do ENVMIXER (0..4)
REM    legendas      PT-BR + captura de RDRAM no F5, para auditoria
REM    sem_legendas  roda em ingles, para comparacao
REM    audio_rsp     microcodigo de audio recompilado no lugar do HLE C
REM    audio_fonte   rota mais fiel: microcodigo real + cadencia virtual do AI
REM    divergencia   varredura da bissecao HLE x microcodigo real
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
REM wpj2_probe.exe e o alvo de tools\build_probe.cmd e reune tudo: legendas
REM PT-BR, instrumentacao de entrada e as chaves de audio. Nao ha mais motivo
REM para trocar de executavel por perfil - era assim antes porque as
REM funcionalidades estavam em builds separadas.
set "EXE=wpj2_probe.exe"
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
REM Padrao validado auditiva e numericamente em 23/08: AList executada pelo
REM microcodigo real e AI na cadencia virtual. O HLE fica somente nos perfis
REM diagnosticos que o selecionam explicitamente abaixo.
set "WPJ2_NATIVE_AUDIO_RSP=1"
set "WPJ2_AI_VIRTUAL_CADENCE=1"
set "WPJ2_AI_TIMED=0"
REM O corredor atravessa o plano da camera. O F3DEX recorta as faces; descartar
REM o triangulo inteiro faz paredes sumirem por um quadro e voltarem no outro.
set "WPJ2_F3D_W_CLIP=1"
REM Traducoes maiores que o recurso ingles sao publicadas por ponteiro numa
REM arena fora do heap de 4 MB do jogo. O carregador conserva seus dois cursores
REM originais sincronizados; a troca acontece somente no consumidor da string.
set "WPJ2_REALOCAR=1"
set "WPJ2_REALOCAR_FMT=1"
set "WPJ2_BISSECAO="
set "WPJ2_BUTTONS="
set "WPJ2_WINDOW_TITLE=Wonder Project J2 - %PERFIL%"

REM --- legendas PT-BR: LIGADAS POR PADRAO ------------------------------------
REM Era o perfil padrao antes desta reescrita e voltou a ser. O catalogo fica
REM em textos\ (local, derivado da ROM, fora do Git). "TESTAR.bat sem_legendas"
REM roda em ingles para comparacao.
set "WPJ2_LEGENDAS=%~dp0textos\traducao_ptbr.tsv"
set "WPJ2_LEGENDAS_LOG=%SAIDA%\legendas_rota.tsv"
REM Temporariamente ativo tambem no perfil padrao: as falas com controles E1
REM e rolagem longa so podem ser confirmadas pelo recurso vivo do mesmo F5.
REM O custo ocorre apenas ao apertar F5 e os dumps continuam em temp\.
set "WPJ2_LEGENDAS_RDRAM_CAPTURE=1"
if /I "%PERFIL%"=="sem_legendas" set "WPJ2_LEGENDAS="
if /I "%PERFIL%"=="input"        set "WPJ2_LEGENDAS="
if /I "%PERFIL%"=="divergencia"  set "WPJ2_LEGENDAS="
if not exist "%WPJ2_LEGENDAS%" set "WPJ2_LEGENDAS="

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

REM START apertado DEPOIS do titulo, nao desde o boot.
REM
REM Medido: com o botao preso desde o quadro zero o jogo fica em 8/1 e nunca
REM chega ao titulo, enquanto sem entrada nenhuma ele avanca sozinho para 8/26
REM e depois 12/50. Ou seja, segurar START durante a abertura BLOQUEIA a
REM progressao - o botao tem efeito, so nao o esperado.
REM
REM Este perfil espera o titulo (aparece por volta dos 20 s) e da um toque
REM curto, que e o que se quer testar. Se o estado sair de 8/26, o START
REM funciona e a etapa titulo -> menu esta destravada.
if /I "%PERFIL%"=="input_tardio" (
  set "WPJ2_AUDIO=0"
  set "WPJ2_AUDIO_FAST=1"
  set "WPJ2_WINDOW=0"
  set "WPJ2_TIMEOUT=40"
  REM Toques de 1,5 s, nao de 200 ms. O jogo le o controle cerca de dez vezes
  REM por segundo, entao uma janela curta cabe inteira entre duas leituras e o
  REM botao nunca aparece em gContPad - foi o que aconteceu na primeira versao.
  set "WPJ2_INPUT=21000:1000;22500:0000;27000:1000;28500:0000"
  set "WPJ2_WINDOW_TITLE=Wonder Project J2 - START apos o titulo"
)

REM Sequencia completa: titulo -> START -> menu -> A -> selecao de save.
REM
REM So passou a ser testavel depois da correcao de 23/08 no PIF (a fita de
REM comandos precisa ser reexecutada tambem na leitura de volta; veja
REM ENTRADA_RETOMADA.md). Antes disto o botao so existia nos primeiros 2,3 s.
REM
REM Codigos: START=0x1000, A=0x8000. Toques de 1,5 s pelo mesmo motivo do
REM perfil acima. O A e repetido porque nao se sabe ainda quanto tempo a
REM transicao de cena leva - se o primeiro cair durante a troca, o segundo
REM pega o menu ja montado.
if /I "%PERFIL%"=="menu" (
  set "WPJ2_AUDIO=0"
  set "WPJ2_AUDIO_FAST=1"
  set "WPJ2_WINDOW=0"
  set "WPJ2_TIMEOUT=60"
  set "WPJ2_INPUT=21000:1000;22500:0000;28000:8000;29500:0000;36000:8000;37500:0000"
  set "WPJ2_WINDOW_TITLE=Wonder Project J2 - START e depois A"
)

REM A medicao que faltava. O perfil "input" prova que a cadeia funciona com
REM WPJ2_BUTTONS (variavel de ambiente, botao preso desde o boot), mas isso NAO
REM cobre o teclado: com a janela aberta ha a bomba de mensagens no meio, e ja
REM se observou que a configuracao com janela se comporta diferente.
REM
REM Aperte Enter depois que o titulo aparecer e confira no resumo se
REM [ctrl-raw] mostra pad=1000. Se mostrar, o transporte esta bom tambem no
REM modo interativo e o alvo passa a ser a maquina de estados.
if /I "%PERFIL%"=="input_janela" (
  set "WPJ2_AUDIO=0"
  set "WPJ2_AUDIO_FAST=1"
  set "WPJ2_WINDOW=1"
  set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teclado: aperte Enter no titulo"
)

REM Paliativo medido: o caminho wet responde por ~70%% do offset DC e e o que
REM mais reduz o chiado no teste auditivo. Nao e correcao (ver ANALISE_AUDIO.md).
if /I "%PERFIL%"=="sem_reverb" (
  set "WPJ2_NATIVE_AUDIO_RSP=0"
  set "WPJ2_AUDIO_NO_WET=1"
)
if /I "%PERFIL%"=="sem_polef" (
  set "WPJ2_NATIVE_AUDIO_RSP=0"
  set "WPJ2_AUDIO_BYPASS_POLEF=1"
)

if /I "%PERFIL%"=="voz" (
  set "WPJ2_NATIVE_AUDIO_RSP=0"
  set "WPJ2_AUDIO_VOICE=%ARG%"
  set "WPJ2_WINDOW_TITLE=Wonder Project J2 - voz %ARG%"
)

REM Auditoria da traducao. As legendas ja estao ligadas por padrao em todos os
REM perfis; este acrescenta a captura de RDRAM no F5, para localizar recursos
REM dinamicos que ainda nao aparecem no catalogo.
if /I "%PERFIL%"=="legendas" set "WPJ2_LEGENDAS_RDRAM_CAPTURE=1"

if /I "%PERFIL%"=="audio_rsp" (
  set "WPJ2_NATIVE_AUDIO_RSP=1"
  set "WPJ2_AI_VIRTUAL_CADENCE=1"
)

REM Perfil recomendado depois do cruzamento com wonder-source/libreultra.
REM Usa a AList original no RSP e a cadencia virtual ja validada. O executavel
REM unificado contem ainda a copia PI logica: amostras alinhadas em 2 bytes nao
REM podem ser transferidas por memcpy quando o cache RDRAM esta em outro
REM alinhamento modulo 4, ou os pares ADPCM chegam trocados e viram chiado.
if /I "%PERFIL%"=="audio_fonte" (
  set "WPJ2_NATIVE_AUDIO_RSP=1"
  set "WPJ2_AI_VIRTUAL_CADENCE=1"
  set "WPJ2_AI_TIMED=0"
  set "WPJ2_WINDOW_TITLE=Wonder Project J2 - audio fiel aos fontes"
)

REM Varredura da bissecao: compara o nosso HLE com o microcodigo real da ROM
REM sobre todas as listas musicais. IMPORTANTE: forca NO_WET=0, porque medir
REM divergencia com o reverb cortado compara um caminho que a ROM nao executa.
if /I "%PERFIL%"=="divergencia" (
  set "WPJ2_NATIVE_AUDIO_RSP=0"
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
REM Eco das variaveis que decidem o teste. Sem isto, um perfil que nao aplica
REM o que promete passa despercebido - foi o que aconteceu com WPJ2_INPUT.
echo   Env:   LEGENDAS=%WPJ2_LEGENDAS% BUTTONS=%WPJ2_BUTTONS% INPUT=%WPJ2_INPUT%
echo.

"%~dp0%EXE%" "%ROM%" > "%SAIDA%\execucao.log" 2>&1
set "SAIDA_CODE=%errorlevel%"

REM --- resumo ---------------------------------------------------------------
echo.
echo   --- resumo ---
if /I "%PERFIL%"=="input" goto :resumo_entrada
if /I "%PERFIL%"=="input_janela" goto :resumo_entrada
if /I "%PERFIL%"=="input_tardio" goto :resumo_entrada
if /I "%PERFIL%"=="menu" goto :resumo_entrada
goto :depois_entrada
:resumo_entrada
(
  echo   [cadeia de entrada]
  REM O despejo da estrutura de 40 bytes fica na linha seguinte ao cabecalho,
  REM entao os dois padroes precisam entrar no filtro.
  findstr /C:"ctrl-raw" /C:"raw[40]" "%SAIDA%\execucao.log"
  echo   [estado da maquina]
  findstr /C:"subestado principal" "%SAIDA%\execucao.log"
  echo   [teclas registradas pelo WndProc]
  findstr /C:"controle] botoes" "%SAIDA%\execucao.log"
)
:depois_entrada
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
