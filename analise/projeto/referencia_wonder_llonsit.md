# Referência `LLONSIT-glitch/wonder`

Revisão realizada em 2026-08-22 sobre o commit
`4860e725438b8c7ef70cca46ec7a943f06e48b38` (2026-05-15). A cópia local fica
em `tools/wonder-source` e é ignorada pelo Git deste projeto.

## Conclusão

É a referência externa mais útil encontrada até agora porque decompila a ROM
japonesa do mesmo jogo e conserva os mesmos endereços virtuais do código que
o N64Recomp processou. A árvore possui 222 fontes C/assembly, cerca de 4.017
definições C, 623 trechos ainda não decompilados (`GLOBAL_ASM`) e 808 símbolos
nomeados; 133 símbolos nomeados ficam no segmento principal de funções.

Não é um port para PC e não substitui nosso runtime. Seu maior valor é dar
nomes, tipos, estruturas e intenção às funções já recompiladas automaticamente.

## Evidência imediata — legendas

`src/code/code_8F1A0.c` contém a implementação legível de `func_80096B38`.
Ela publica o bloco alocado por `*arg4` e retorna separadamente a fonte em
`sp2C`. `src/code/code_63240.c` usa as duas saídas na mesma chamada. Isso
confirma por fonte a causa da primeira tentativa de tradução: alterar somente
o ponteiro de saída não alcançava o caminho consumido pelo compositor.

## Prioridades de reaproveitamento

1. **Áudio — prioridade alta.** `src/audio/audio_driver.c` identifica bancos,
   sequências, vozes, volumes, osciladores e endereços globais. Embora um
   cabeçalho declare `OUTPUT_RATE 44100`, a inicialização efetiva
   `func_800B84F0` chama `osAiSetFrequency(0x5622)`, isto é, 22.050 Hz; os
   22.047 Hz medidos no runtime estão corretos. O jogo usa três buffers de
   saída, 24 buffers DMA e atualiza música/SFX a cada dois ticks. A árvore
   também traz a versão correspondente da
   libaudio (`env`, `resample`, `reverb`, `synthesizer`). Esses dados devem ser
   comparados ao runtime antes da próxima hipótese sobre o chiado. O gerenciador
   principal `audio_mgr.c`, porém, ainda é majoritariamente `GLOBAL_ASM`.
2. **Scheduler e cadência — prioridade alta.** `src/code/scheduler.c` descreve
   as filas separadas de áudio/gráficos, o yield do RSP e a reação aos eventos
   VI/SP/DP. É uma referência melhor que inferir o escalonamento apenas por
   logs e pode explicar a pequena diferença de velocidade.
3. **3D e matrizes — prioridade média/alta.** `mtx_util.c`, `sys_main.c` e os
   símbolos nomeiam rotações X/Y/Z, pilha de matrizes, conversões fixed/float,
   criação de tarefa gráfica e montagem de objetos. Servem para revisar ordem
   de multiplicação, câmera e clipping com semântica real.
4. **Texturas e sprites — prioridade média.** `spi.c`, `obj.h` e as tabelas de
   assets documentam SPI0/SPI1/SPIN, tamanhos de cabeçalho e estruturas de
   objetos. Podem substituir parte das hipóteses sobre layout e descompressão,
   embora o próprio README informe que várias paletas ainda estão indefinidas.
5. **Mapa de símbolos — ganho transversal.** `symbol_addrs.txt` associa nomes
   como `SysMem_HeapAlloc`, `SysMem_Copy8`, `AudioGeneral_PlayBGM`,
   `Scheduler_RspTaskYield` e `MtxUtil_Rotate*` aos `func_XXXXXXXX` locais.
   Deve alimentar relatórios e sondas; não é necessário renomear a saída gerada
   do N64Recomp para obter esse benefício.

## Limites e cuidados

- O alvo é a ROM japonesa, enquanto o executável atual usa a tradução inglesa
  de Ryu. Código e endereços coincidem em vários pontos já verificados, mas
  dados, tabelas e offsets de texto precisam ser validados antes de copiar.
- Há 623 rotinas ainda em assembly/não decompiladas; ausência de um nome ou C
  correspondente não significa que a função não exista.
- O repositório declara CC0 para o trabalho próprio, mas inclui fontes/header
  históricos da libultra com avisos de licença próprios. Usá-los como referência
  técnica é mais seguro do que copiá-los indiscriminadamente para o runtime.
- A cópia de referência não entra no build e não altera os protótipos.

## Correções aplicadas a partir do cruzamento

- A interceptação de `func_80090E58` foi retirada. `func_80096B38` é o único
  gancho de tradução e trata tanto `*arg4` quanto o retorno `v0`, conforme o C
  do projeto de referência.
- A pesquisa de legendas deixou de ocorrer em toda entrada de função e em todo
  retrace. Isso elimina pesquisas de sufixos durante a digitação e reduz custo
  no caminho quente sem alterar o texto do jogo.
- Quatro falas completas observadas depois da primeira sequência foram
  incorporadas ao catálogo com versões PT-BR que cabem no recurso atual.
- Áudio e matrizes não receberam mudanças especulativas: a taxa de áudio e a
  ordem de matrizes já coincidem com a fonte. As máscaras e a escala do
  controle também foram mantidas por já coincidirem com o original.
- A resposta Joybus de identidade foi corrigida de `0x02`
  (`CONT_CARD_PULL`) para `0x00`. O runtime agora representa corretamente um
  controle conectado sem Controller Pak, em vez de sinalizar remoção recente.
- O jogo solicita `OS_VI_DITHER_FILTER_ON`, com gamma e gamma-dither desligados.
  Isso foi documentado para a implementação futura do VI; não foi confundido
  com o filtro espacial aproximado que anteriormente danificou o 3D.
- O perfil `audio_wonder` executa o driver original já recompilado da ROM, o
  microcódigo de áudio via RSPRecomp, todos os buses/efeitos e a fila AI de dois
  DMAs temporizada. A tentativa revelou e corrigiu a ordem da interrupção AI:
  o slot concluído precisa sair do FIFO antes de a thread do jogo ser acordada.
