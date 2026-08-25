# Pendências de fidelidade visual

## Abertura 2D — junções na grade durante o zoom da ENIX

- **Impacto:** baixo. As linhas entre blocos aparecem durante a ampliação e
  desaparecem depois; a logo final continua legível e correta.
- **Localização:** estado `8/1`, mosaico de 30 `TEXRECT` (grade 5×6), blocos
  de `64×32`, textura CI8.
- **Referência:** Project64 não mostra as junções no vídeo de comparação.
- **Oráculo disponível:** `analise/oraculo/graficos/wpj2_8_1_texrect_oracle.txt`.
- **Tentativas já feitas:** BILERP forçado, filtro final tipo VI, borda extra
  de `TEXRECT` em COPY e permuta CI8 por DXT `0x100`. Nenhuma alterou o
  resultado observável; filtros de apresentação permanecem desligados no
  `TESTAR.bat`.
- **Hipótese a revisar depois:** composição RDP de CI8/TLUT e semântica exata
  de cobertura/ciclo COPY, validando contra um dump do framebuffer do
  Project64 no mesmo retrace. Não aplicar pós-processamento global, pois isso
  mascararia a causa e pode afetar texto/3D.

## Corredor 3D — fidelidade de materiais e amostragem

- **Impacto:** médio. A geometria estável do corredor, trono e diálogo é
  renderizada, mas há serrilhado e diferenças no padrão circular sob o trono,
  nas faixas azul/branco e no gradiente de fundo.
- **Estado:** `12/50`.
- **Oráculo disponível:** `analise/oraculo/graficos/wpj2_material_12_50_oracle.txt`.
  Ele confirma `LOADTLUT` em TMEM `0x800`, seguido por textura CI8 de `64×32`
  e o respectivo `LOADBLOCK`/tile do material.
- **Tentativas rejeitadas:** intercalação DXT para RGBA16 e CI no corredor,
  combinador RDP genérico e BILERP forçado em TRI1. As três pioraram ou não
  alteraram o resultado; as chaves no `TESTAR.bat` voltaram ao caminho estável.
- **Próxima revisão:** implementar o layout completo de TMEM/combiner por
  ciclo, validando cada material contra um framebuffer do Project64 no mesmo
  retrace. Não usar pós-processamento global como substituto.
# Corredor 3D — personagem central: entrada e persistência

- **Impacto:** médio. Na primeira conversa em `12/50`, o personagem deveria subir de baixo para cima e permanecer visível quando a caixa de diálogo termina. No recompilado ele aparece estático/tarde e, em outro ponto, some junto com a caixa.
- **Evidência consolidada:** a sonda da janela `gfx 1380..1540` mostrou que as 56–57 faixas de material da malha são submetidas em todos os lotes, mas a faixa projetada Y da parte central varia somente cerca de 7–8 pixels e se repete. O F5 em `gfx=1461` registrou o trono sem o personagem completo.
- **O que foi descartado:** desligar Z (`WPJ2_F3D_Z=0`) destrói a oclusão do trono e não recupera a animação; trocar para multiplicação convencional (`WPJ2_F3D_MATRIX_CONVENTIONAL=1`) deforma todo o corredor. A malha usa `SETCOMBINE FC1219FF/FFFFFE38` (TEXEL × SHADE), portanto não se deve aplicar o `PRIMITIVE alpha` da caixa como regra global ao personagem.
- **Próxima revisão:** instrumentar a pilha F3DEX por `MTX/PUSH/POPMTX` e comparar suas matrizes locais com uma captura equivalente do Project64. A correção deve preservar a ordem de matriz e o Z atuais; não resolver por retenção artificial de framebuffer ou alpha específico do personagem.

# Transições posteriores — 3D/2D e 2D/2D

- **Impacto:** baixo enquanto as cenas seguintes são mapeadas. A saída do corredor para a ilha (`estado 8/13`, `f5_001`) deixa dois losangos pretos no céu; a troca entre cenários 2D (`8/12` e `8/6`, `f5_002`/`f5_003`) vai para preto e muda de forma seca, quando a referência usa fade-out/fade-in.
- **Escopo:** não confundir com a primeira passagem 2D→3D, cujo intervalo preto curto já foi tratado separadamente. Estes casos usam outros lotes TEXRECT/alpha e devem ser comparados em uma revisão própria.
- **Próxima revisão:** capturar as listas RDP desses três estados no Project64 e validar `PRIMITIVE alpha`, `SETCOMBINE` e a alternância do VI antes de alterar o compositor global.

# Backend de GPU — substituição gradual do rasterizador RDP em CPU

- **Impacto:** estratégico/médio. A cadência de VI foi medida em 59,996 Hz,
  mas listas gráficas pesadas ainda podem produzir picos no rasterizador CPU;
  Project64 transfere essa parte para a GPU.
- **Estado atual:** `runtime/rsp.c` interpreta RSP/RDP e pinta triângulos,
  texturas, Z, fog e blend em software. `runtime/video.c` apenas apresenta o
  framebuffer final com GDI (`StretchDIBits`); não há Direct3D/OpenGL ativo.
- **Próxima revisão:** criar backend Direct3D 11 opcional: primeiro swap chain
  e apresentação de framebuffer, depois texturas/TMEM, triângulos, depth,
  combiner, fog e coverage via shaders. Validar material a material contra o
  Project64 antes de substituir o caminho CPU estável.
- **Cuidado:** não ativar um filtro global como atalho para anti-aliasing; o
  RDP depende de modos de ciclo e cobertura por material.

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
  RDRAM. Para estas duas a hipótese da imagem pré-rasterizada segue de pé, e é
  o que resta investigar neste item.
- **Tentativa rejeitada:** apagar e redesenhar texto no framebuffer produziu
  resíduos de sprites e conflito de ordenação com o cursor. Essa rota foi
  removida.
- **Próxima revisão:** interceptar a rotina CPU que grava a imagem CI8 ou
  substituir seu recurso-fonte antes da rasterização. Não desenhar uma camada
  por cima da janela nem editar o framebuffer depois da composição.
- **Separação concluída em 24/08:** o realce vermelho e a duplicação aparente
  do cursor não eram parte da tradução. Eram um quad F3DEX sem textura que o
  rasterizador tratava como texturizado; `G_TEXTURE_OFF` agora é respeitado em
  2D e 3D. O que resta neste item são somente `Day` e `Progress`.
