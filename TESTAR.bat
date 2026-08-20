@echo off
setlocal
REM Teste unificado da rota natural: abertura 2D -> transicao -> cena 3D/dialogo.
REM Uso: TESTAR.bat [musica_limpa|musica_pura|audio_rsp_exato|audio_zelda_queue|legendas|antes_audio|audio_nativo|audio_nativo_clocked|audio_counter|audio_counter_vi|diagnostico_audio], TESTAR.bat voz N, sem_polef, sem_reverb ou ganho_baixo.
REM Nao ha limite de tempo;
REM feche a janela para encerrar. "voz N" isola a voz N do ENVMIXER (0..4).
REM
REM Perfis historicos incorporados aqui (nao manter .bat separados):
REM   APRESENTACAO_ABERTURA, PROTOTIPO_CENA3D, PROTOTIPO_VISUAL,
REM   SONDAR_TRANSICAO_CADENCIA, SONDAR_TRANSICAO_RETRACE_300 e
REM   VALIDAR_TRANSICAO_60HZ.
REM Para uma nova sondagem, altere somente as variaveis deste arquivo e
REM registre o objetivo no bloco de comentarios acima.

REM "antes_audio" seleciona o executavel compilado exatamente com o fonte de
REM audio anterior a compatibilidade AI. Serve somente a comparacao de
REM desempenho; o perfil padrao abaixo continua sendo o atual com audio.
REM O duplo clique usa a sonda atual; um argumento ainda permite selecionar
REM um perfil histórico sem criar outro .bat (por exemplo: TESTAR.bat legendas).
set "WPJ2_PROFILE=audio_rsp_exato"
if not "%~1"=="" set "WPJ2_PROFILE=%~1"
set "WPJ2_EXE=wpj2_visual.exe"
set "WPJ2_PROFILE_TAG=audio"
if /I "%WPJ2_PROFILE%"=="legendas" (
  set "WPJ2_EXE=build\wpj2_legendas_check.exe"
  set "WPJ2_PROFILE_TAG=legendas_ptbr"
)
if /I "%WPJ2_PROFILE%"=="diagnostico_audio" set "WPJ2_EXE=build\wpj2_audio_diagnostico.exe"
if /I "%WPJ2_PROFILE%"=="musica_pura" set "WPJ2_EXE=build\wpj2_musica_pura.exe"
if /I "%WPJ2_PROFILE%"=="musica_limpa" set "WPJ2_EXE=build\wpj2_musica_limpa.exe"
if /I "%WPJ2_PROFILE%"=="audio_zelda_queue" set "WPJ2_EXE=build\wpj2_audio_zelda_queue.exe"
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" set "WPJ2_EXE=build\wpj2_audio_rsp_exato.exe"
if /I "%WPJ2_PROFILE%"=="antes_audio" (
  set "WPJ2_EXE=wpj2_visual_antes_audio_compat.exe"
  set "WPJ2_PROFILE_TAG=antes_audio_compat"
)
if /I "%WPJ2_PROFILE%"=="audio_nativo" set "WPJ2_PROFILE_TAG=audio_nativo_rsp"
if /I "%WPJ2_PROFILE%"=="audio_nativo_clocked" set "WPJ2_PROFILE_TAG=audio_nativo_clocked"
if /I "%WPJ2_PROFILE%"=="audio_counter" set "WPJ2_PROFILE_TAG=audio_counter"
if /I "%WPJ2_PROFILE%"=="audio_counter_vi" set "WPJ2_PROFILE_TAG=audio_counter_vi"
if /I "%WPJ2_PROFILE%"=="audio_nativo_seco" set "WPJ2_PROFILE_TAG=audio_nativo_seco"
if /I "%WPJ2_PROFILE%"=="sem_polef" set "WPJ2_PROFILE_TAG=sem_polef"
if /I "%WPJ2_PROFILE%"=="sem_reverb" set "WPJ2_PROFILE_TAG=sem_reverb"
if /I "%WPJ2_PROFILE%"=="estado_audio" set "WPJ2_PROFILE_TAG=estado_audio"
if /I "%WPJ2_PROFILE%"=="audio550" set "WPJ2_PROFILE_TAG=audio550"
if /I "%WPJ2_PROFILE%"=="ganho_baixo" set "WPJ2_PROFILE_TAG=ganho_baixo"
if /I "%WPJ2_PROFILE%"=="diagnostico_audio" set "WPJ2_PROFILE_TAG=diagnostico_audio"
if /I "%WPJ2_PROFILE%"=="musica_pura" set "WPJ2_PROFILE_TAG=musica_pura"
if /I "%WPJ2_PROFILE%"=="musica_limpa" set "WPJ2_PROFILE_TAG=musica_limpa"
if /I "%WPJ2_PROFILE%"=="audio_zelda_queue" set "WPJ2_PROFILE_TAG=audio_zelda_queue"
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" set "WPJ2_PROFILE_TAG=audio_rsp_exato"
set "WPJ2_AUDIO_VOICE=-1"
if /I "%WPJ2_PROFILE%"=="voz" (
  set "WPJ2_AUDIO_VOICE=%~2"
  set "WPJ2_PROFILE_TAG=voz_%~2"
)

REM Rodada isolada de capturas F5: arquivos da execucao anterior nao entram na
REM proxima analise. O executavel cria BMP+TXT em temp\projeto\testar quando
REM F5 e pressionado.
set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar"
if /I "%WPJ2_PROFILE%"=="antes_audio" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\antes_audio_compat"
if /I "%WPJ2_PROFILE%"=="audio_nativo" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\audio_nativo_rsp"
if /I "%WPJ2_PROFILE%"=="audio_nativo_clocked" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\audio_nativo_clocked"
if /I "%WPJ2_PROFILE%"=="audio_counter" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\audio_counter"
if /I "%WPJ2_PROFILE%"=="audio_counter_vi" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\audio_counter_vi"
if /I "%WPJ2_PROFILE%"=="audio_nativo_seco" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\audio_nativo_seco"
if /I "%WPJ2_PROFILE%"=="voz" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\voz_%~2"
if /I "%WPJ2_PROFILE%"=="sem_polef" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\sem_polef"
if /I "%WPJ2_PROFILE%"=="sem_reverb" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\sem_reverb"
if /I "%WPJ2_PROFILE%"=="estado_audio" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\estado_audio"
if /I "%WPJ2_PROFILE%"=="audio550" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\audio550"
if /I "%WPJ2_PROFILE%"=="ganho_baixo" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\ganho_baixo"
if /I "%WPJ2_PROFILE%"=="diagnostico_audio" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\diagnostico_audio"
if /I "%WPJ2_PROFILE%"=="musica_pura" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\musica_pura"
if /I "%WPJ2_PROFILE%"=="musica_limpa" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\musica_limpa"
if /I "%WPJ2_PROFILE%"=="audio_zelda_queue" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\audio_zelda_queue"
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" set "WPJ2_CAPTURE_DIR=%~dp0temp\projeto\testar\audio_rsp_exato"
if not exist "%WPJ2_CAPTURE_DIR%" mkdir "%WPJ2_CAPTURE_DIR%"
set "WPJ2_RUN_METRICS=%WPJ2_CAPTURE_DIR%\run_metrics.txt"
del /q "%WPJ2_CAPTURE_DIR%\f5_*.bmp" >nul 2>nul
del /q "%WPJ2_CAPTURE_DIR%\f5_*.txt" >nul 2>nul
del /q "%WPJ2_CAPTURE_DIR%\historico_*.bmp" >nul 2>nul
del /q "%WPJ2_CAPTURE_DIR%\historico.txt" >nul 2>nul
if /I "%WPJ2_PROFILE%"=="legendas" del /q "%WPJ2_CAPTURE_DIR%\legendas_rota.tsv" >nul 2>nul
if /I "%WPJ2_PROFILE%"=="estado_audio" rmdir /s /q "%WPJ2_CAPTURE_DIR%\state_oracle" >nul 2>nul
if /I "%WPJ2_PROFILE%"=="audio550" rmdir /s /q "%WPJ2_CAPTURE_DIR%\audio_task" >nul 2>nul
if /I "%WPJ2_PROFILE%"=="diagnostico_audio" rmdir /s /q "%WPJ2_CAPTURE_DIR%\audio_post" >nul 2>nul
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" del /q "%WPJ2_CAPTURE_DIR%\audio_deep_local.csv" >nul 2>nul
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" del /q "%WPJ2_CAPTURE_DIR%\audio_deep_compare.md" >nul 2>nul
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" rmdir /s /q "%WPJ2_CAPTURE_DIR%\first_divergence" >nul 2>nul

REM Perfil atual, validado para reproduzir a transicao sem acelerar a ROM.
set "WPJ2_WINDOW=1"
set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste visual RDP (audio)"
if /I "%WPJ2_PROFILE%"=="legendas" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste de legendas PT-BR"
if /I "%WPJ2_PROFILE%"=="antes_audio" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste visual RDP (antes da compatibilidade AI)"
if /I "%WPJ2_PROFILE%"=="audio_nativo" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio RSP nativo"
if /I "%WPJ2_PROFILE%"=="audio_nativo_clocked" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio RSP nativo, AI cadenciado"
if /I "%WPJ2_PROFILE%"=="audio_counter" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio, temporizador COP0"
if /I "%WPJ2_PROFILE%"=="audio_counter_vi" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio, temporizador COP0 por VI"
if /I "%WPJ2_PROFILE%"=="audio_nativo_seco" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio RSP nativo sem reverb"
if /I "%WPJ2_PROFILE%"=="voz" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio, voz %~2"
if /I "%WPJ2_PROFILE%"=="sem_polef" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio, POLEF bypass"
if /I "%WPJ2_PROFILE%"=="sem_reverb" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio, sem reverb"
if /I "%WPJ2_PROFILE%"=="estado_audio" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - captura de estado de audio"
if /I "%WPJ2_PROFILE%"=="audio550" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - captura audio tarefa 550"
if /I "%WPJ2_PROFILE%"=="ganho_baixo" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - teste audio, ganho mestre 20 por cento"
if /I "%WPJ2_PROFILE%"=="diagnostico_audio" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - diagnostico de estado de audio"
if /I "%WPJ2_PROFILE%"=="musica_pura" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - musica pura (sem vozes e efeitos)"
if /I "%WPJ2_PROFILE%"=="musica_limpa" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - musica limpa (sem vozes, eco e filtro)"
if /I "%WPJ2_PROFILE%"=="audio_zelda_queue" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - fila de audio Zelda64Recomp"
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" set "WPJ2_WINDOW_TITLE=Wonder Project J2 - audio RSP nativo"
set "WPJ2_WINDOW_HOLD_LAST=0"
set "WPJ2_RETRACE=60"
set "WPJ2_PREEMPT_EVERY_POLL=0"
set "WPJ2_TIMEOUT=0"
REM A camada de legenda só é ligada no perfil correspondente. Ela lê o TSV
REM externo e mantém o cursor da digitação da ROM; o jogo normal fica em inglês.
set "WPJ2_LEGENDAS="
if /I "%WPJ2_PROFILE%"=="legendas" set "WPJ2_LEGENDAS=%~dp0textos\traducao_ptbr.tsv"
set "WPJ2_LEGENDAS_LOG="
if /I "%WPJ2_PROFILE%"=="legendas" set "WPJ2_LEGENDAS_LOG=%WPJ2_CAPTURE_DIR%\legendas_rota.tsv"
set "WPJ2_LEGENDAS_RDRAM_CAPTURE=0"
if /I "%WPJ2_PROFILE%"=="legendas" set "WPJ2_LEGENDAS_RDRAM_CAPTURE=1"
REM Audio completo: ADPCM, RESAMPLE e ENVMIXER sao executados. A saida toca
REM pelo Windows e uma copia WAV e salva para analise apos fechar a janela.
set "WPJ2_AUDIO_FAST=0"
set "WPJ2_AUDIO=1"
set "WPJ2_AUDIO_PLAY=1"
REM Rota em validacao: microcodigo real recompilado e cadencia AI medida no
REM Project64. O perfil padrao continua no HLE C ate validar a musica toda.
set "WPJ2_NATIVE_AUDIO_RSP=0"
set "WPJ2_AI_VIRTUAL_CADENCE=0"
set "WPJ2_AI_ZELDA_QUEUE=0"
if /I "%WPJ2_PROFILE%"=="audio_zelda_queue" set "WPJ2_AI_ZELDA_QUEUE=1"
REM Teste causal: muda somente o sintetizador. Cadencia virtual, contador e
REM fila Zelda permanecem desligados, eliminando a mistura de variaveis do
REM antigo perfil audio_nativo.
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" set "WPJ2_NATIVE_AUDIO_RSP=1"
set "WPJ2_AI_TIMED=0"
REM A fila de dois DMAs baseada no libreultra permanece disponivel por
REM WPJ2_AI_TIMED=1, mas o teste auditivo nao alterou o chiado. O perfil normal
REM volta a cadencia virtual anterior enquanto a origem do PCM e investigada.
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" set "WPJ2_AI_VIRTUAL_CADENCE=1"
set "WPJ2_AI_VI_CADENCE=0"
set "WPJ2_EVENTS_IDLE_ONLY=0"
if /I "%WPJ2_PROFILE%"=="audio_nativo" set "WPJ2_NATIVE_AUDIO_RSP=1"
if /I "%WPJ2_PROFILE%"=="audio_nativo" set "WPJ2_AI_VIRTUAL_CADENCE=1"
if /I "%WPJ2_PROFILE%"=="audio_nativo_clocked" set "WPJ2_NATIVE_AUDIO_RSP=1"
if /I "%WPJ2_PROFILE%"=="audio_nativo_clocked" set "WPJ2_AI_VIRTUAL_CADENCE=1"
if /I "%WPJ2_PROFILE%"=="audio_nativo_clocked" set "WPJ2_AI_COMPAT_CLOCKED=1"
if /I "%WPJ2_PROFILE%"=="audio_counter" set "WPJ2_NATIVE_AUDIO_RSP=1"
if /I "%WPJ2_PROFILE%"=="audio_counter" set "WPJ2_AI_VIRTUAL_CADENCE=1"
if /I "%WPJ2_PROFILE%"=="audio_counter" set "WPJ2_COUNTER_COMPARE=1"
if /I "%WPJ2_PROFILE%"=="audio_counter_vi" set "WPJ2_NATIVE_AUDIO_RSP=1"
if /I "%WPJ2_PROFILE%"=="audio_counter_vi" set "WPJ2_AI_VIRTUAL_CADENCE=1"
if /I "%WPJ2_PROFILE%"=="audio_counter_vi" set "WPJ2_COUNTER_COMPARE=1"
if /I "%WPJ2_PROFILE%"=="audio_counter_vi" set "WPJ2_COUNT_VI_CLOCK=1"
if /I "%WPJ2_PROFILE%"=="audio_nativo_seco" set "WPJ2_NATIVE_AUDIO_RSP=1"
if /I "%WPJ2_PROFILE%"=="audio_nativo_seco" set "WPJ2_AI_VIRTUAL_CADENCE=1"
set "WPJ2_AUDIO_GAIN_PERCENT=100"
if /I "%WPJ2_PROFILE%"=="ganho_baixo" set "WPJ2_AUDIO_GAIN_PERCENT=20"
set "WPJ2_AUDIO_WAV=%WPJ2_CAPTURE_DIR%\audio_capture.wav"
set "WPJ2_NATIVE_AUDIO_LIST_TRACE="
REM A rodada profunda anterior cumpriu seu papel. O teste auditivo atual nao
REM grava uma linha por AList para nao deslocar o instante dos eventos.
set "WPJ2_NATIVE_AUDIO_DEEP_TRACE="
set "WPJ2_NATIVE_AUDIO_DEEP_CAPTURE_COUNT="
REM Sonda compacta da fronteira RSP/memoria/AI. Compara os historicos antes e
REM depois de cada tarefa e grava bruto apenas as oito primeiras alteracoes
REM ocorridas entre tarefas. Uma rodada testa ordem, persistencia e vida do PCM.
set "WPJ2_AUDIO_FIRST_DIVERGENCE_DIR="
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" set "WPJ2_AUDIO_FIRST_DIVERGENCE_DIR=%WPJ2_CAPTURE_DIR%\first_divergence"
if defined WPJ2_AUDIO_FIRST_DIVERGENCE_DIR if not exist "%WPJ2_AUDIO_FIRST_DIVERGENCE_DIR%" mkdir "%WPJ2_AUDIO_FIRST_DIVERGENCE_DIR%"
REM PCM individual para confrontar diretamente com os buffers da sonda PJ64.
REM Abrir/fechar ~30 arquivos por segundo tornava a preempcao dependente do
REM disco. So perfis explicitamente diagnosticos habilitam esse despejo.
set "WPJ2_AUDIO_BUFFER_DIR="
if /I "%WPJ2_PROFILE%"=="diagnostico_audio" set "WPJ2_AUDIO_BUFFER_DIR=%WPJ2_CAPTURE_DIR%\audio_buffers"
if /I "%WPJ2_PROFILE%"=="estado_audio" set "WPJ2_AUDIO_BUFFER_DIR=%WPJ2_CAPTURE_DIR%\audio_buffers"
if defined WPJ2_AUDIO_BUFFER_DIR if not exist "%WPJ2_AUDIO_BUFFER_DIR%" mkdir "%WPJ2_AUDIO_BUFFER_DIR%"
REM AList antes do processamento. No perfil estado_audio tambem mantem uma
REM janela inicial; em qualquer perfil, F5 arma uma coleta ancorada no ponto
REM audivel e a grava em audio_f5, sem mexer na imagem ou temporizacao.
set "WPJ2_AUDIO_STATE_CAPTURE="
if /I "%WPJ2_PROFILE%"=="estado_audio" set "WPJ2_AUDIO_STATE_CAPTURE=%WPJ2_CAPTURE_DIR%\state_oracle"
if /I "%WPJ2_PROFILE%"=="estado_audio" if not exist "%WPJ2_AUDIO_STATE_CAPTURE%" mkdir "%WPJ2_AUDIO_STATE_CAPTURE%"
if /I "%WPJ2_PROFILE%"=="audio550" set "WPJ2_AUDIO_STATE_CAPTURE=%WPJ2_CAPTURE_DIR%\audio_task"
if /I "%WPJ2_PROFILE%"=="audio550" if not exist "%WPJ2_AUDIO_STATE_CAPTURE%" mkdir "%WPJ2_AUDIO_STATE_CAPTURE%"
if /I not "%WPJ2_PROFILE%"=="estado_audio" set "WPJ2_AUDIO_STATE_CAPTURE=%WPJ2_CAPTURE_DIR%\audio_f5"
if /I not "%WPJ2_PROFILE%"=="estado_audio" rmdir /s /q "%WPJ2_AUDIO_STATE_CAPTURE%" >nul 2>nul
if /I not "%WPJ2_PROFILE%"=="estado_audio" if not exist "%WPJ2_AUDIO_STATE_CAPTURE%" mkdir "%WPJ2_AUDIO_STATE_CAPTURE%"
if /I "%WPJ2_PROFILE%"=="audio550" set "WPJ2_AUDIO_STATE_CAPTURE=%WPJ2_CAPTURE_DIR%\audio_task"
set "WPJ2_AUDIO_STATE_TASK="
if /I "%WPJ2_PROFILE%"=="audio550" set "WPJ2_AUDIO_STATE_TASK=550"
REM Estado posterior: somente o perfil de diagnostico salva as memorias que
REM cada AList deixou para a proxima. Isso separa chiado por estado acumulado
REM de chiado que venha do dispositivo de saida.
set "WPJ2_AUDIO_STATE_POST_CAPTURE="
if /I "%WPJ2_PROFILE%"=="diagnostico_audio" set "WPJ2_AUDIO_STATE_POST_CAPTURE=%WPJ2_CAPTURE_DIR%\audio_post"
if /I "%WPJ2_PROFILE%"=="diagnostico_audio" if not exist "%WPJ2_AUDIO_STATE_POST_CAPTURE%" mkdir "%WPJ2_AUDIO_STATE_POST_CAPTURE%"
REM -1 = mistura normal; no modo "voz N", somente a voz logica N chega aos
REM buses de saida. ADPCM/RESAMPLE das demais continuam rodando para preservar
REM seus historicos entre buffers.
set "WPJ2_AUDIO_VOICE=%WPJ2_AUDIO_VOICE%"
set "WPJ2_AUDIO_BYPASS_POLEF=0"
if /I "%WPJ2_PROFILE%"=="sem_polef" set "WPJ2_AUDIO_BYPASS_POLEF=1"
REM Mantem a musica seca e desliga somente o bus auxiliar (wet) do ENVMIXER.
set "WPJ2_AUDIO_NO_WET=0"
if /I "%WPJ2_PROFILE%"=="sem_reverb" set "WPJ2_AUDIO_NO_WET=1"
REM Musica pura: somente os cinco canais persistentes do BGM, sem falas/SFX,
REM sem bus wet e sem a realimentacao de atraso que simula eco/reverb.
set "WPJ2_AUDIO_DRY_ONLY=0"
set "WPJ2_AUDIO_MUSIC_ONLY=0"
if /I "%WPJ2_PROFILE%"=="musica_pura" set "WPJ2_AUDIO_NO_WET=1"
if /I "%WPJ2_PROFILE%"=="musica_pura" set "WPJ2_AUDIO_DRY_ONLY=1"
if /I "%WPJ2_PROFILE%"=="musica_pura" set "WPJ2_AUDIO_MUSIC_ONLY=1"
if /I "%WPJ2_PROFILE%"=="musica_limpa" set "WPJ2_AUDIO_NO_WET=1"
if /I "%WPJ2_PROFILE%"=="musica_limpa" set "WPJ2_AUDIO_DRY_ONLY=1"
if /I "%WPJ2_PROFILE%"=="musica_limpa" set "WPJ2_AUDIO_MUSIC_ONLY=1"
if /I "%WPJ2_PROFILE%"=="musica_limpa" set "WPJ2_AUDIO_BYPASS_POLEF=1"
REM A fila AI temporizada ainda depende do espelho dos registradores MMIO;
REM mantemos o modo compativel para a musica tocar sem interromper a ROM.
set "WPJ2_AI_TIMED=0"
if /I "%WPJ2_PROFILE%"=="antes_audio" (
  set "WPJ2_AI_TIMED=1"
)
set "WPJ2_TMEM_INTERLEAVE=1"
REM Sonda RGBA16 rejeitada: esta carga especifica nao usa a permuta aplicada
REM pela implementacao local. Mantemos 0 como referencia estavel.
set "WPJ2_TMEM_INTERLEAVE_RGBA16=0"
REM O oraculo mostrou CI16 carregado e CI8 lido no tile do trono. Testa a
REM alternancia DXT somente para CI dentro do corredor 12/50.
REM Sonda rejeitada: este material usa leitura linear; a permuta danifica
REM textos CI que chegam no final da mesma cena.
set "WPJ2_TMEM_INTERLEAVE_CI=0"
REM O combinador generico nao trouxe ganho visual e elevou o custo do quadro.
set "WPJ2_RDP_COMBINER=0"
REM Sem forcar filtro: preserva exatamente o modo pedido pela lista RDP.
set "WPJ2_TEX_FILTER_3D="
REM O culling nao era a causa: volta ao comportamento normal da ROM.
set "WPJ2_F3D_CULL=1"
REM Sonda reversivel: recorta TRI1 que cruza o plano W no corredor 12/50.
set "WPJ2_F3D_W_CLIP=1"
REM A multiplicacao convencional deformou o corredor e foi rejeitada. Mantem
REM a convencao de vetores-linha usada pelo F3DEX nesta ROM.
set "WPJ2_F3D_MATRIX_CONVENTIONAL=0"
REM Z e necessario para preservar a oclusao e a composicao do trono. A sonda
REM com Z=0 nao recuperou a animacao do personagem e foi rejeitada.
set "WPJ2_F3D_Z=1"
REM A cobertura 2x2 do RDP fica desligada: sem o Z/coverage buffer completo
REM ela produziu linhas pretas entre triangulos. O 2D sera tratado primeiro
REM pelo filtro de apresentacao da janela, sem mexer na RDRAM.
set "WPJ2_RDP_AA=0"
REM Reamostragem suave ao ampliar a janela; F5/F6 mantem 320x240 sem filtro.
set "WPJ2_PRESENT_SMOOTH=1"
REM Os filtros finais nao alteraram a grade: desativados ate corrigir a fonte.
set "WPJ2_VI_FILTER_2D=0"
REM A forcagem BILERP nao alterou a imagem; volta ao estado pedido pela ROM.
set "WPJ2_TEX_FILTER="
REM Sonda passiva do Gepetto em 8/3. Registra a origem da textura, tile e
REM retangulos no intervalo da captura, sem alterar a imagem.
set "WPJ2_TEXRECT_TRACE=%WPJ2_CAPTURE_DIR%\texrect_sprite.csv"
set "WPJ2_TEXRECT_TRACE_STATE=8/3"
set "WPJ2_TEXRECT_TRACE_GFX_MIN=4400"
set "WPJ2_TEXRECT_TRACE_GFX_MAX=4460"
REM O oraculo confirmou DXT=0x100 no CI8 da grade 8/1: alterna bancos TMEM.
REM Esta chave afeta somente esse mosaico e pode voltar a 0 para comparacao.
set "WPJ2_CI8_DXT_8_1=1"
set "WPJ2_AUTOCUTSCENE=0"
set "WPJ2_HOLD_STATE_8_1=0"
REM Nao reter 12/50 nesta rodada: precisamos observar a aproximacao do trono
REM e a entrada do dialogo, onde foi percebida queda de FPS.
set "WPJ2_HOLD_STATE_12_50=0"

REM Um unico estado compacto para analise posterior; sem PPM, trace ou log
REM por display list. Remova esta linha apenas se a proxima sondagem nao
REM precisar de telemetria.
set "WPJ2_STATUS_FILE=%WPJ2_CAPTURE_DIR%\testar_status.txt"
set "WPJ2_STATUS_TIMELINE=%WPJ2_CAPTURE_DIR%\testar_timeline.csv"
REM CSV pequeno de cada TEXRECT que usa PRIMITIVE alpha; sem PPM nem log de DL.
set "WPJ2_ALPHA_TRACE=%WPJ2_CAPTURE_DIR%\alpha_fade.csv"
REM TRI1 na janela de entrada do personagem. A sonda nao altera o desenho:
REM registra materiais e a faixa projetada para verificar se a subida vertical
REM se perde na transformacao F3DEX.
set "WPJ2_TRI_ALPHA_TRACE=%WPJ2_CAPTURE_DIR%\tri_alpha.csv"
set "WPJ2_TRI_ALPHA_GFX_MIN=1380"
set "WPJ2_TRI_ALPHA_GFX_MAX=1540"
set "WPJ2_TRI_ALPHA_ALL=1"
REM Sonda transitória: confirma o clear preto real entre 2D e 3D (somente
REM FILLRECTs de 12/50; algumas linhas por execucao, sem impacto perceptivel).
set "WPJ2_TRANSITION_TRACE=%WPJ2_CAPTURE_DIR%\transicao_rdp.csv"
set "WPJ2_DEBUG=0"

REM O teste auditivo deterministico nao grava telemetria visual continua.
REM Essas sondas faziam I/O durante a emulacao e mudavam o ponto exato em que
REM VI/AI interrompiam a CPU recompilada. F5 continua disponivel sob demanda.
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" (
  set "WPJ2_TEXRECT_TRACE="
  set "WPJ2_STATUS_TIMELINE="
  set "WPJ2_ALPHA_TRACE="
  set "WPJ2_TRI_ALPHA_TRACE="
  set "WPJ2_TRANSITION_TRACE="
)

if not exist "%~dp0%WPJ2_EXE%" (
  echo ERRO: %WPJ2_EXE% nao encontrado. Solicite uma compilacao diagnostica.
  pause
  exit /b 1
)

for /f %%I in ('powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()"') do set "WPJ2_RUN_START_MS=%%I"
"%~dp0%WPJ2_EXE%" "E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64"
set "WPJ2_EXIT=%errorlevel%"
for /f %%I in ('powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()"') do set "WPJ2_RUN_END_MS=%%I"
set "WPJ2_STATUS_METRICS=%WPJ2_CAPTURE_DIR%\testar_status.txt"
powershell -NoProfile -Command "$s=[int64]$env:WPJ2_RUN_START_MS; $e=[int64]$env:WPJ2_RUN_END_MS; $wall=($e-$s)/1000.0; $wav=$env:WPJ2_AUDIO_WAV; $rate=0; $frames=0; $wavSeconds=0.0; if(Test-Path -LiteralPath $wav){$b=[IO.File]::ReadAllBytes($wav); if($b.Length -ge 44){$rate=[BitConverter]::ToUInt32($b,24); $data=[BitConverter]::ToUInt32($b,40); if($rate){$frames=[int]($data/4); $wavSeconds=$frames/[double]$rate}}}; $status=$env:WPJ2_STATUS_METRICS; $retrace='?'; $gfx='?'; $audioTasks='?'; if(Test-Path -LiteralPath $status){$t=Get-Content -LiteralPath $status -Raw; if($t -match '(?m)^retrace=(\d+)'){$retrace=$matches[1]}; if($t -match '(?m)^graficas=(\d+)'){$gfx=$matches[1]}; if($t -match '(?m)^audio=(\d+)'){$audioTasks=$matches[1]}}; $ratio=if($wall -gt 0){$wavSeconds/$wall}else{0}; $out=@('perfil='+$env:WPJ2_PROFILE_TAG,'inicio_utc_ms='+$s,'fim_utc_ms='+$e,('parede_s={0:N3}' -f $wall),('wav_s={0:N3}' -f $wavSeconds),('wav_rate_hz='+$rate),('wav_frames='+$frames),('audio_parede_ratio={0:N3}' -f $ratio),('retrace='+$retrace),('tarefas_graficas='+$gfx),('tarefas_audio='+$audioTasks),('exit_code='+$env:WPJ2_EXIT)); [IO.File]::WriteAllLines($env:WPJ2_RUN_METRICS,$out); Write-Host ('Metricas: '+$env:WPJ2_RUN_METRICS)"
REM A análise ocorre somente depois de fechar o jogo, portanto não interfere
REM na cadência observada. Se a execução não alinhar com o oráculo, o relatório
REM registra "nenhum" em vez de comparar tarefas apenas pelo número.
set "WPJ2_ANALYZER_PY=C:\Users\lucas\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if /I "%WPJ2_PROFILE%"=="audio_rsp_exato" powershell -NoProfile -Command "$py=$env:WPJ2_ANALYZER_PY; $probe=$env:WPJ2_AUDIO_FIRST_DIVERGENCE_DIR; $root='%~dp0'; if((Test-Path -LiteralPath $py) -and (Test-Path -LiteralPath ($probe+'\state_continuity.csv'))){& $py ($root+'src\scripts\analisar_primeira_divergencia.py') --probe $probe --out ($probe+'\RELATORIO.md'); $suspect=Get-ChildItem -LiteralPath $probe -Directory -Filter 'suspect_task_*' -ErrorAction SilentlyContinue | Select-Object -First 1; if($suspect){& $py ($root+'src\scripts\comparar_entrada_audio_suspeita.py') --probe $probe --out ($suspect.FullName+'\COMPARACAO_ENTRADAS.md')}}"
echo Teste encerrado. Resultados temporarios: temp\projeto\testar
exit /b %WPJ2_EXIT%
