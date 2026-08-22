# Guia Completo de Configurações, Arquivos e Metodologia para Análise (LLM Local)
## Projeto: Wonder Project J2 - Decompilação / Recompilação Estática (N64Recomp)

> [!NOTE]
> Este documento é o manual técnico de referência para um **LLM Local (ex: Qwen 3.5-4B)**. Ele descreve a arquitetura do projeto, a metodologia de testes com o **Oráculo Project64**, os locais exatos dos executáveis e fontes de referência, o guia completo das ferramentas em `tools/`, o resumo dos 11 defeitos de áudio e uma introdução detalhada ao segundo problema de **Entrada / Controle** (`ENTRADA_RETOMADA.md`).

---

### 1. Visão Geral do Projeto e Método de Trabalho

- **O que é este projeto?** É uma recompilação estática (tradução de instruções MIPS de Nintendo 64 para C nativo de alta velocidade via N64Recomp) do jogo **Wonder Project J2**. O código C resultante é compilado com MSVC/Clang e executado em conjunto com um *runtime nativo* (`runtime/*.c`).
- **Estado Atual:** O executável nativo compila, abre janela, renderiza gráficos 2D/3D e emite som.
- **Problema de Áudio (Principal):** O som reproduzido no executável nativo apresenta um **chiado intenso, ruído estático e estalos contínuos**.
- **Como analisamos:** o Project64 é uma referência prática, não uma verdade absoluta. Novas capturas ficam em `temp/oraculo`; a saída recompilada fica em `temp/projeto`. Depois da comparação, somente a conclusão e a evidência mínima são promovidas para `analise/`.

---

### 2. Localização da ROM e dos Executáveis

#### ROM do Jogo (Alvo Principal)
- **Caminho Físico da ROM:** `E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64`
- **Tamanho:** 8.912.896 bytes (8,50 MB)
- **Formato:** Big-Endian `.z64` (Magic `0x80371240`)
- **Ponto de Entrada MIPS (Entrypoint):** `0x80000400`
- **Checksums (CRC1 / CRC2):** `0x4F1E88F7` / `0x4A5A3F96`
- **Código Interno N64:** `NJ2J`

#### Pasta Raiz do Projeto
- `E:\projetos\project-wonder-j2-decomp`

#### Executáveis Produzidos no Nosso Projeto (Raiz)
- **`wpj2_visual.exe`** (`E:\projetos\project-wonder-j2-decomp\wpj2_visual.exe`): Executável nativo principal do jogo com suporte visual e reprodução de áudio ativados.
- **`wpj2_probe.exe`** (`E:\projetos\project-wonder-j2-decomp\wpj2_probe.exe`): Executável nativo compilado em modo de sondagem e telemetria profunda para diagnóstico.

#### Executável e Scripts do Oráculo (Project64)
- **Código-Fonte do Project64 Instrumentado:** `E:\projetos\project-wonder-j2-decomp\tools\Project64-source\`
- **Executável Binário Compilado do Project64:** `E:\projetos\project-wonder-j2-decomp\tools\Project64-source\Bin\Win32\Release\Project64.exe`
- **Diretório de Scripts do Project64:** `E:\projetos\project-wonder-j2-decomp\tools\Project64-source\Bin\Win32\Release\Scripts\`

---

### 3. O que é a Pasta `oraculo/` e Por que Usamos o Project64?

#### O Conceito da Pasta `oraculo/`
A pasta `oraculo/` (`E:\projetos\project-wonder-j2-decomp\oraculo\`) contém o **gabarito perfeito de áudio** gravado pelo Project64 durante a execução da ROM.
Quando o Project64 roda a ROM de Wonder Project J2 sob o script `SONDAR_AUDIO_PROJECT64.bat`, ele gera:
1. `oraculo/audio_capture.wav`: Arquivo WAV contínuo de áudio limpo.
2. `oraculo/wpj2_ai_manifest.csv`: Tabela com o índice de cada DMA AI enviado (`indice`, `endereco_rdram`, `tamanho_bytes`, `frames`, `pico_amplitude`, `amostras_nao_zero`).
3. `oraculo/ai_00001.pcm`, `oraculo/ai_00002.pcm`, ...: Cada bloco individual de áudio PCM de 16-bit enviado à placa de som.

#### Como nosso runtime é comparado com o oráculo
Quando rodamos o perfil atual, ele grava os arquivos em
`temp/projeto/testar/audio_rsp_exato/`:
- `audio_capture.wav`
- métricas e traços explicitamente habilitados para a rodada

Ao executar `COMPARAR_AUDIO.bat`, os scripts leem a saída temporária e a
referência consolidada em `analise/oraculo/audio/`.

#### Por que Modificamos o Código do Project64?
O código-fonte em `tools/Project64-source/Source/Project64-audio/AudioMain.cpp` foi alterado para adicionar a função `Wpj2OracleAppend()`. Essa função intercepta o registrador `AI_LEN` da AI do N64 exatamente quando o emulador transfere um buffer de áudio, salvando os bytes PCM crus na pasta `oraculo/` antes de passar para o driver DirectSound.

---

### 4. Mapeamento Comparativo de Arquivos do Projeto

Para analisar o código de áudio, consulte estes três grupos de arquivos:

#### A. Nosso Runtime Local (`E:\projetos\project-wonder-j2-decomp\runtime\`)
1. **`runtime/audio.c`**
   - **Responsabilidade:** Captura PCM da RDRAM, conversão para Little-Endian host, gerenciamento do relógio do registrador `AI_LEN`, enfileiramento WinMM e gravação em WAV.
   - **Trechos Críticos:**
     - Leitura com XOR `^ 2`: `*(const int16_t*)((uintptr_t)(rdram + p + i) ^ 2u)`.
     - Truncamento de tamanho: `bytes &= ~1u` (em vez de `bytes &= ~3u` para 4 bytes estéreo).
     - Fila WinMM `audio_play_buffer`: descarte silencioso de buffers quando `PLAY_SLOTS` está cheio.
     - Ganho master: `audio_apply_master_gain`.
2. **`runtime/rsp.c`**
   - **Responsabilidade:** Emulador da RSP (Reality Signal Processor) e distribuição de listas de tarefas gráficas (`type 1`) e de áudio `ACMD` (`type 2`).
   - **Trechos Críticos:** Ausência de sintetizador nativo HLE/C estático completo para microcódigo de áudio `ACMD` (ADPCM, osciladores, mixer).
3. **`runtime/hle.c`**
   - **Responsabilidade:** Substituição nativa HLE para chamadas `libultra` (PI DMA, VI, SI, AI, alocação de memória do jogo).
   - **Trechos Críticos:** Disparo da interrupção de áudio `OS_EVENT_AI` e atualização dos registradores `AI_STATUS` / `AI_CONTROL`.
4. **`runtime/sched.c`**
   - **Responsabilidade:** Agendador cooperativo de threads MIPS emuladas como Fibers no Windows.
   - **Trechos Críticos:** Sincronização entre retraces de VI (60 Hz) e finalização de buffers do AI.
5. **`runtime/runtime.c` & `runtime/runtime.h`**
   - **Responsabilidade:** Inicialização do espaço de endereçamento (8 MB de RDRAM mapeados em KSEG0/KSEG1) e contextos MIPS.

#### B. Oráculo Project64 (`E:\projetos\project-wonder-j2-decomp\tools\Project64-source\`)
1. **`Source/Project64-audio/Driver/SoundBase.cpp` & `SoundBase.h`**
   - **Trechos de Interesse:**
     - Linhas 163-164: Inversão de palavras e canais estéreo (`*(uint16_t*)(m_Buffer + loc) = *(uint16_t*)(m_AI_DMAPrimaryBuffer + 2)`).
     - Linha 109: Alinhamento estrito de 8 bytes na leitura do tamanho `m_AI_DMAPrimaryBytes & ~0x7`.
     - Linhas 48-54: Bloqueio suave `Sleep(1)` em vez de descartar amostras quando o buffer enche.
2. **`Source/Project64-audio/AudioMain.cpp`**
   - **Trechos de Interesse:** Cálculo da frequência real do DAC: `Frequency = (video_clock / (g_Dacrate + 1))` com `video_clock = 48681812` (NTSC).

#### C. Referência Zelda64Recomp (`E:\projetos\Zelda64Recomp\`)
1. **`ultramodern/audio.cpp`**
   - **Trechos de Interesse:** A função `get_remaining_audio_bytes()` aplica um adiantamento de **0.5 a 1.0 quadro VI no futuro** (`buffer_offset_frames`), evitando underflow no jogo.
2. **`src/recomp/ai.cpp`**
   - **Trechos de Interesse:** Wrappers HLE para chamadas `osAiSetFrequency_recomp`, `osAiSetNextBuffer_recomp`, `osAiGetLength_recomp` e `osAiGetStatus_recomp`.

---

### 5. Guia Completo das Ferramentas em `tools/`

A pasta `E:\projetos\project-wonder-j2-decomp\tools\` possui ferramentas divididas em 3 categorias:

#### Categoria 1: Sondagem, Diagnóstico e Comparação de Áudio
- **`tools/analisar_audio_deep.py`**: Analisa arquivos `.pcm` e `.wav`, calculando picos de amplitude, frequências, descontinuidades de fase e detectando ruído branco/estático.
- **`tools/comparar_audio_deep_estagios.py`**: compara a captura em `temp/projeto` contra o oráculo consolidado em `analise/oraculo`.
- **`tools/audio_oracle_test.cpp`**: Teste compilável em C++ que executa a comparação direta entre buffers gerados no Project64 e no nosso runtime.
- **`tools/audio_native_oracle_test.cpp`**: Teste de validação de emissão de PCM via WinMM no host.
- **`tools/extract_project64_oracle.py`**: Utilitário para extrair e organizar o catálogo de capturas PCM salvas pelo Project64.
- **`tools/reconstruir_audio_deep_task.py`**: Reconstitui o fluxo de comandos de áudio enviados à RSP para auditoria.

#### Categoria 2: Scripts de Compilação e Automação de Build
- **`tools/build_visual.cmd`**: Compila o executável principal `wpj2_visual.exe` via MSVC (`cl.exe /std:c17 /O2`).
- **`tools/build_probe.cmd`**: Compila o executável de testes e telemetria `wpj2_probe.exe`.
- **`tools/build_lib.cmd`**: compila os fontes de `src/RecompiledFuncs/*.c` gerando a biblioteca estática `build/wpj2_recompiled.lib`.
- **`tools/rebuild_all.cmd`**: Recompila todo o pipeline (Símbolos $\rightarrow$ N64Recomp $\rightarrow$ Pós-processamento $\rightarrow$ Compilação).
- **`tools/env.cmd`**: Configura o ambiente de variáveis do MSVC 2022 e ferramentas.

#### Categoria 3: Análise Estática de Código MIPS e Símbolos
- **`tools/gen_boot_symbols.py` & `tools/gen_table.py`**: Desmonta a ROM MIPS via biblioteca `rabbitizer` e gera a tabela de símbolos `wpj2.syms.toml`.
- **`tools/analyze_all.py` / `tools/scan_rom.py`**: Varre a ROM de Wonder Project J2 identificando funções, registradores de hardware e instruções.
- **`tools/find_overlays.py` / `tools/find_jal_refs.py`**: Localiza overlays e mapeia chamadas de funções.
- **`tools/translate_gemini_json.py`**: Script de tradução automática do catálogo de diálogos para Português (PT-BR).

---

### 6. Como Rodar os Scripts Batch de Teste (Raiz)

1. **`RODAR.bat`**: grava sua rodada em `temp/projeto/laboratorio/`.
2. **`SONDAR_AUDIO_PROJECT64.bat`**: grava uma nova referência em `temp/oraculo/audio_deep/`.
3. **`ORACULO_PROJECT64.bat`**: Define as variáveis de ambiente necessárias para gravar os manifestos e arquivos `.pcm` individuais.
4. **`COMPARAR_AUDIO.bat`**: grava comparação em `temp/projeto/comparar_audio/`.
5. **`TESTAR.bat`**: grava em `temp/projeto/testar/`.
6. **`JOGAR.bat`**: Inicia o executável nativo diretamente com suporte a teclado.

---

### 7. Resumo dos 11 Defeitos de Áudio Identificados (Referência Rápida)

1. **Falta de Sintetizador ABI da RSP (`runtime/rsp.c`):** As tarefas ACMD (tipo 2) não possuem emulação nativa C completa de ADPCM/osciladores.
2. **Inversão de Canais L/R e XOR 2 (`runtime/audio.c`):** Leitura de 16 bits trocando paridade sem respeitar a ordenação estéreo (`[31:16] Left`, `[15:0] Right`).
3. **Reporte de `AI_LEN` sem Máscara `& ~7` e sem Offset VI:** Ausência de compensação de VI no futuro presente no Zelda64Recomp.
4. **Frequência de DAC Fixa em 22047 Hz (`runtime/audio.c`):** Não utiliza a fórmula NTSC real `48681812 / (DACRATE + 1)`.
5. **Descarte Silencioso de Buffers no WinMM (`runtime/audio.c`):** Quando a fila enche, o PCM é descartado em vez de aplicar bloqueio suave `Sleep(1)`.
6. **Dessincronização de `AI_STATUS` e `MI_INTR_AI`:** A interrupção AI é sinalizada prematuramente, fazendo o jogo sobrescrever a RDRAM.
7. **Máscara de Tamanho Incorreta (`bytes &= ~1u` em vez de `~3u`):** O alinhamento a 2 bytes desfasa a onda estéreo a cada buffer ímpar.
8. **Race Condition de DMA na RDRAM:** Leitura síncrona imediata da RDRAM em `audio_queue_ai_buffer` causa tearing de áudio.
9. **Hard Clipping no Ganho (`audio_apply_master_gain`):** Truncamento rígido em `+32767 / -32768` sem atenuação suave (soft-clipping).
10. **Jitter de I/O WinMM na Thread Principal:** Chamadas síncronas `waveOutWrite` desestabilizam o timing entre quadros VI.
11. **Lixo Acumulado na DMEM/RDRAM:** Falta de zeramento de buffers de áudio/reverb entre tarefas.

---

### 8. SEGUNDO PROBLEMA: Entrada / Controle (`ENTRADA_RETOMADA.md`)

> [!IMPORTANT]
> **ATENÇÃO LLM LOCAL:** Este é um **SEGUNDO PROBLEMA INDEPENDENTE** que deve ser analisado **somente após a conclusão da etapa de áudio**. Ele está registrado no arquivo `ENTRADA_RETOMADA.md`.

#### Descrição do Problema de Controle
- **O Sintoma:** Ao abrir o jogo nativo (`JOGAR.bat`), a tela de título é exibida, mas pressionar a tecla **START (Enter)** não faz o jogo avançar para o menu de seleção de save.
- **Investigação Realizada:**
  1. O teclado **funciona** na borda do runtime (`WndProc` em `runtime/video.c` captura os pressionamentos de tecla e salva na variável `g_buttons`).
  2. O PIF emulado (`runtime/pif.c`) responde corretamente ao formato Joybus (`0x1000` para START).
  3. **O Defeito Raiz:** O jogo executa aproximadamente **11 leituras de controle** (`osContStartReadData` / PIF DMA) durante o bootstrap de inicialização e **para de solicitar novas leituras**. Como ele para de consultar o PIF após o boot, qualquer tecla pressionada no título nunca é lida.

#### Solução Proposta no Modelo `ultramodern` (Zelda64Recomp)
- No Zelda64Recomp (`tools/N64ModernRuntime-source/ultramodern/src/input.cpp`), as funções da `libultra` de controle são **substituídas nativamente por HLE**:
  ```cpp
  extern "C" s32 osContStartReadData(RDRAM_ARG PTR(OSMesgQueue) mq) {
      if (input_callbacks.poll_input != nullptr) input_callbacks.poll_input();
      ultramodern::send_si_message();
      return 0;
  }
  extern "C" void osContGetReadData(OSContPad *data) {
      // Preenche o buffer de botões direto da variável do host
  }
  ```
- **Próxima Ação para o Controle:** Criar wrappers HLE em `runtime/hle.c` para substituir `osContInit`, `osContStartReadData`, `osContGetReadData` e `__osSiRawStartDma`, entregando o estado de `g_buttons` diretamente ao jogo sem depender das consultas da fita joybus do PIF.
