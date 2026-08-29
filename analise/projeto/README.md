# Resultados do projeto recompilado

Resultados produzidos pelo runtime deste projeto e já interpretados.

- `codigo/`: mapas estáticos usados para localizar chamadas, COP0 e MMIO.
- `inventario_execucao.md`: executáveis preservados e referência quebrada já
  existente no protótipo histórico.
- `audio_primeira_divergencia.md`: resultado consolidado da sonda de
  continuidade dos estados e do PCM entre RSP e AI.
- `audio_dma_pi_endian.md`: causa e correção da troca de bytes nas amostras
  ADPCM transferidas pelo PI para o cache de áudio.
- `graficos_clipping_corredor.md`: regressão causada pela perda da chave de
  recorte F3DEX e restauração do clipping homogêneo no corredor.
- `renderizacao_3d_fast3d.md`: textura, iluminação, cobertura/AA e VI no
  corredor 3D, comparados às referências Fast3D e GLideN64.
- `acentuacao_ptbr.md`: causa do `#` no lugar dos acentos, correção da cópia
  inicial da fonte Ryu e validação dos 14 glifos especiais.
- `integracao_legendas.md`: rota real do diálogo inglês descompactado e estado
  da substituição PT-BR.
- `referencia_wonder_llonsit.md`: avaliação da decompilação pública da ROM
  japonesa e pontos reaproveitáveis por endereço.
- `referencia_wpj2_recomp_vmarcelo.md`: auditoria da recompilação independente
  baseada em RT64, com comparação de cobertura e plano de adoção da GPU.
- `integracao_rt64.md`: arquitetura e validação do backend híbrido definitivo,
  com RT64 apenas no vídeo e os demais subsistemas preservados localmente.
- `checkpoints_estado.md`: comparação oot-dx/MegaManX4Recomp, causa da
  instabilidade do bookmark e requisitos para um save state completo.
- `savestate_runtime.md`: implementação e validação do snapshot versionado sem
  fibers, incluindo continuations, dispositivos, RT64 e cargas repetidas.
- `legendas/`: duas referências visuais usadas para localizar os buffers e o
  controle de cor da primeira caixa de diálogo.

Capturas WAV, PCM, PPM, logs de execução e métricas de uma nova rodada devem
ser gravadas em `../../temp/projeto`, nunca aqui.

## Estado consolidado mais recente — 20/08/2026

- A sonda `EVENTS_IDLE_ONLY` foi rejeitada: produzia tarefas gráficas, mas não
  apresentava quadros e deixava a janela sem processar mensagens do Windows.
  O perfil voltou ao fluxo normal e o bombeamento da janela foi desacoplado
  de `video_present`.
- A comparação com `tools/libreultra` confirmou que o ajuste de fronteira de
  8 KiB já está compilado dentro de `osAiSetNextBuffer` da ROM.
- A diferença concreta encontrada foi `__osAiDeviceBusy`: a ROM consultava
  `AI_STATUS_FIFO_FULL`, enquanto o runtime não espelhava o FIFO hospedado.
- O teste com dois DMAs, `DMA_BUSY/FIFO_FULL`, `AI_LEN` decrescente e evento
  na conclusão do DMA primário permaneceu responsivo e sustentou cerca de
  59,3 Hz. Porém, o teste auditivo não alterou o chiado. O modelo foi mantido
  apenas como sonda opt-in e o perfil normal voltou à cadência anterior.
- A sonda RSP → AI encontrou 367/367 buffers pareados byte a byte idênticos;
  corrupção posterior do PCM foi descartada. O ramo ativo agora é a primeira
  divergência dos históricos ADPCM/RESAMPLE antes da síntese.
- A rodada interativa obteve 370 ALists integralmente idênticas e alinhamento
  exato em `N+15`. A tarefa local 43 / Project64 58 entra com AList e estados
  iguais, mas produz os primeiros históricos ADPCM/RESAMPLE divergentes. A
  próxima sonda compara amostras e codebooks consumidos por essa tarefa.

As evidências válidas e a direção atual permanecem no documento canônico
`../../ANALISE_AUDIO.md`.
