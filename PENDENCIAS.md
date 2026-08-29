# Pendências de fidelidade visual

# Loja/computador — textos ingleses e congelamento completo

- **Impacto:** alto para continuidade da validação. A interface da loja ainda
  contém recursos em inglês e uma execução congelou imagem e áudio.
- **Estado em 27/08:** o watchdog finalmente capturou a falha completa. A
  trilha termina em `__osSpDeviceBusy`, durante `osSpTaskLoad`, com as filas de
  conclusão vazias e nenhuma DMA RSP realmente pendente. O runtime executa a
  DMA de SP de modo síncrono; portanto, esperar bits ocupados no espelho MMIO
  era um estado impossível. `__osSpSetPc` e `__osSpDeviceBusy` agora consultam
  o estado nativo do RSP. A correção está compilada e aguarda validação longa.
- **Textos da loja:** rótulos e descrições agora são substituídos nas cadeias
  NUL do próprio recurso recém-carregado. Não há desenho adicional sobre o
  framebuffer. `Buy`, `Back`, `Money`, `Shop` e as descrições dos óleos estão
  cobertos; a tela precisa ser validada interativamente.
- **Próximo passo:** navegar por vários itens por tempo suficiente para exceder
  o ponto da trava anterior. Se o watchdog disparar novamente, a nova trilha
  será comparada com esta antes de alterar mais lógica do escalonador.
- **Stress acelerado em 28/08:** 240 segundos a 480 Hz, 112.583 chamadas do
  heap, 160.787 tarefas RSP e cerca de 6,49 GB carregados na TMEM sem trava,
  descarte RSP ou DMA recusada. O congelamento acumulativo fica considerado
  corrigido nas rotas exercitadas; permanece necessária cobertura narrativa
  além da loja, não outra repetição do mesmo menu.

# Legendas PT-BR — expansão de recursos maiores que o inglês

- **Impacto:** alto para cobertura da tradução. O catálogo textual está
  traduzido, mas 694 dos 2.137 recursos completos conhecidos excedem o espaço
  binário da cadeia inglesa.
- **Estado em 25/08:** limite removido na rota já identificada. Cadeias maiores
  são copiadas para uma arena acima dos 4 MB usados pelo heap e o ponteiro é
  trocado somente no consumidor individual (`func_800319B0`) ou no formatador
  (`func_80090E58`). Os dois cursores estruturais de `func_80096B38` permanecem
  sincronizados e nunca são reapontados.
- **Evidência local:** `textos/apoio/revisao_runtime_limites.tsv` registra fonte, tradução e
  relação `bytes_pt>bytes_disponíveis` para cada caso.
- **Validação automática:** uma tradução sintética de 69 bytes para um recurso
  de 21 bytes percorreu `recurso_ptbr_arena`/`arena_cache`, manteve o subestado
  `24` e 3.657 tarefas gráficas. O A/B forçando todas as cadeias terminou no
  mesmo estado e carga do modo sem arena; o protótipo anterior desviava para
  `50` e entrava em laço.
- **Ainda pendente:** validação interativa de textos longos reais em cenas mais
  avançadas. A rota que constrói tabelas por `func_80096D40` já reaponta cada
  elemento depois de preservar o bloco original. Não encurtar automaticamente
  as traduções como substituto.
- **Revisão conjunta de 25/08:** o usuário confirmou que este item continua
  pendente. A arena removeu o limite binário conhecido, mas ainda existem
  falas longas cuja paginação, posicionamento ou scroll precisam ser validados
  e corrigidos em execução real.

# Menus — `Day` e `Progress` no recurso gráfico

- **Impacto atual:** baixo. A seleção de diário ainda contém `Day` e
  `Progress`; os rótulos textuais da tela seguinte foram resolvidos.
- **Causa revista em 25/08 — a hipótese anterior estava errada.** Dizia-se que
  essas palavras já chegavam rasterizadas numa imagem dinâmica. Sete das nove
  são **texto simples e patcheável**; o que as mantinha em inglês era o
  orçamento de bytes, não a rasterização.

  O despejo de strings da RDRAM na tela de velocidade mostra o texto vivo,
  ao lado de uma tradução nossa já aplicada na mesma região:

  ```
  0x0F8C9A   Message Speed
  0x0F8E8F   O Imp*rio Siliconiano     <- nossa, aplicada
  ```

  Orçamentos medidos na ROM (bloco 0x68B9xx/0x68BExx, separado por `\n`, sem
  enchimento — o limite é o tamanho exato do original):

  | rótulo | ROM | limite | situação |
  |---|---|---|---|
  | `Message Speed` | 0x68B924 | slot 48 | resolvido: `Velocidade da Mensagem` |
  | `Bird's Speed`  | 0x68B968 | slot 48 | resolvido: `Velocidade do Bird` |
  | `Back`          | 0x68BED8 |  4 | resolvido: `Sair` |
  | `End`           | 0x68BED0 |  3 | resolvido: `Fim` |
  | `Normal`        | 0x68B934 |  6 | já correto |
  | `Fast`          | 0x68B93C | slot 48 | resolvido: `Rápido` |
  | `Slow`          | 0x68B944 | slot 48 | resolvido: `Lento` |

  O limite curto era do patch no cartucho, não do recurso vivo. Cada endereço
  acima é copiado por `func_800BD218` para um slot privado de 48 bytes. A
  tradução agora ocorre depois da cópia e usa essa capacidade real.

- **Continuam sem origem textual:** `Progress` e `Day`. Não existem na ROM em
  texto plano (`Day` só aparece dentro de "Days") nem apareceram no despejo da
  RDRAM. Um ring ampliado preservou todos os 144.184 objetos de texto desde o
  início e confirmou que nenhuma das duas sequências passa por
  `func_80094230`. O recurso inglês adicional foi isolado em ROM `0x0068E100`
  (0x282E0 bytes depois de `Spi_DecompressAsset`); a ROM japonesa não o carrega.
  O que resta é localizar os tiles dos dois rótulos dentro desse recurso e
  substituí-los antes da composição.
- **Tentativa rejeitada:** apagar e redesenhar texto no framebuffer produziu
  resíduos de sprites e conflito de ordenação com o cursor. Essa rota foi
  removida.
- **Próxima revisão:** interceptar a rotina CPU que grava a imagem CI8 ou
  substituir seu recurso-fonte antes da rasterização. Não desenhar uma camada
  por cima da janela nem editar o framebuffer depois da composição.
- **Revisão conjunta de 25/08:** o usuário confirmou que `Day` e `Progress`
  continuam aparecendo em inglês; o item permanece aberto.
- **Separação concluída em 24/08:** o realce vermelho e a duplicação aparente
  do cursor não eram parte da tradução. Eram um quad F3DEX sem textura que o
  rasterizador tratava como texturizado; `G_TEXTURE_OFF` agora é respeitado em
  2D e 3D. O que resta neste item são somente `Day` e `Progress`.
