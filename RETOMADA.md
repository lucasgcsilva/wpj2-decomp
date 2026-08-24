# Retomada — estado em 23/08/2026

Documento de handoff. A sessão de chat que produziu isto foi descartada de
propósito; **tudo o que importa está aqui e nos arquivos citados.** Comece por
este arquivo, não por histórico de conversa.

Registros por assunto:

| Assunto | Arquivo |
|---|---|
| Áudio | `ANALISE_AUDIO.md` |
| Entrada de controle | `ENTRADA_RETOMADA.md` |
| Layout do projeto e regra do `temp/` | `ESTRUTURA.md` |
| Gráficos / clipping | `analise/projeto/graficos_clipping_corredor.md` |

Ponto de entrada único para testar: `TESTAR.bat <perfil>`.
Perfis de entrada: `input`, `input_janela`, `input_tardio`, `menu`.

---

## O que está resolvido

- **Áudio** — audível e estável. Restam "pipocos" esporádicos.
- **Entrada** — botões e direcionais funcionando. A causa raiz da paralisia
  histórica está em `ENTRADA_RETOMADA.md` (o PIF só executava a fita de
  comandos na escrita; o jogo lia 30×/s uma resposta de dois segundos atrás).
- **Controller Pack** — implementado em `runtime/mempak.c`. Saves em `sav/`,
  fora do versionamento.
- Transições visuais parcialmente ajustadas.

## Corrigido nesta sessão

**Escrita dupla no Controller Pack.** A correção do PIF de 23/08 passou a
reexecutar a fita também na leitura de volta (`dir=0`). Como `pif_process()`
trata os comandos de MemPak `0x02`/`0x03`, **toda transação de save passou a
acontecer duas vezes**. O conteúdo continuava correto — mesmos 32 bytes, mesmo
endereço — então não aparecia como save corrompido, e sim como lentidão. É a
explicação mais provável para "o Controller Pack demora bastante".

`pif_process()` agora recebe `refresh`: em `dir=0` reamostra só o estado dos
controles e ignora armazenamento. O modelo por trás disso também foi
corrigido no comentário — no hardware os botões chegam novos porque o PIF
**varre os controles continuamente** em segundo plano, e não porque a fita
seja reexecutada; essa varredura nunca toca o Controller Pack.

*Falta validar com o jogo rodando: confirmar que salvar ficou mais rápido.*

## Aplicado na segunda passagem (23/08, tarde)

- **SI fora do portão de 60 Hz** (`runtime/hle.c`). Ver item 6 abaixo. Medido:
  leituras de controle em 40 s **1114 → 2252**, `si_r` **1134 → 3190**,
  pendências constantes em zero. É a correção mais importante do dia depois da
  escrita dupla.
- **Folga de texto medida** (`runtime/legendas.c`). Ver item 3. Ganho real
  pequeno (8740 → 8759); o restante é conteúdo, não código.
- **Filtro de dither pelo bit real e ampliação suavizada** (`runtime/video.c`).
  Ver item 2.

Nada disto foi validado com o jogo na mão — falta confirmar que salvar ficou
mais rápido e que as legendas resgatadas aparecem certas.

---

# Revisão dos projetos de referência — achados

Fontes: `tools/libreultra`, `tools/sdk-tools`, `tools/wonder-source`.

## 1. Blend e coverage não existem no rasterizador — prioridade máxima

Uma única lacuna explica **dois** problemas relatados, e é o melhor retorno
por esforço do projeto hoje.

Evidência, em `runtime/rsp.c:109`:

```c
/* SETOTHERMODE_L e o estado de blend/coverage do RDP. ...
   registramos os poucos modos emitidos antes de implementar blend. */
```

`SETOTHERMODE_L` é apenas registrado, nunca interpretado. Uma busca por
`coverage|cvg` em todo o `rsp.c` devolve só esse comentário.

**Consequência A — o quadrado vermelho piscante do menu.** É um retângulo
translúcido sobre a opção do cursor, ou seja, alpha blending. Sem blend ele
não tem como aparecer. Não é bug de lógica de menu: é a feature que falta.

**Consequência B — o serrilhado, inclusive na abertura.** O anti-alias do N64
é *baseado em coverage*, calculado pelo RDP durante a rasterização e guardado
junto do pixel. Se o rasterizador não produz coverage, o filtro do VI não tem
o que consumir — e **nenhum pós-processamento recupera informação de borda que
nunca foi calculada**. Por isso o `video_filtrar_vi_2d` já ativo na abertura
não resolve: ele foi posto no lugar errado da cadeia.

Confirmação de que o jogo espera AA ligado: o `VI_CONTROL` observado em
execução (`0xA4400000 = 77826`) tem os bits 8–9 (`aa_mode`) em zero, que é o
modo de AA mais completo.

**Encaminhamento sugerido, nesta ordem:**

1. Interpretar `SETOTHERMODE_L` e implementar o blender de dois ciclos. Além
   de trazer o realce do menu, é pré-requisito para transparência em geral —
   provavelmente também para parte das "transições estranhas".
2. Para o serrilhado, **não** perseguir coverage fiel de imediato. Rasterizar
   em 2× ou 4× e reduzir por média na apresentação resolve a borda em todo
   lugar, não depende de coverage, e é muito menor em esforço. Coverage fiel
   fica para a fase 4.

## 2. Features de VI: o jogo declara o que quer, e nós adivinhamos

`tools/wonder-source/src/main.c:65` mostra a intenção sem ambiguidade:

```c
D_8018168C = 74;                            /* 0x4A */
osViSetSpecialFeatures(OS_VI_DITHER_FILTER_ON | OS_VI_GAMMA_DITHER_OFF | OS_VI_GAMMA_OFF);
```

`0x4A` = `DITHER_FILTER_ON | GAMMA_DITHER_OFF | GAMMA_OFF`. Confere com a
chamada.

- **Gamma: já está correto.** `video_gamma_ativo()` condiciona ao bit 3 do
  `vi_status`; o jogo desliga o gamma, o bit fica zero, a curva não é
  aplicada. Verificado, sem ação necessária.
- **Filtro de dither: acionado pelo critério errado.** ✅ *corrigido.* Rodava
  sob `estado_jogo == 8`, uma heurística. Passa a seguir o bit que o jogo liga:
  `DITHER_FILTER_ENABLE`, **bit 16** (`0x10000`). Atenção — não é o bit 11; os
  bits 12–15 são `PIXEL_ADVANCE`, que em `0x13002` valem 3. Errei isto na
  primeira redação deste documento. Ataca granulação de dither, **não** o
  serrilhado (ver item 1).
- **Ampliação sem filtro.** ✅ *corrigido.* `g_present_smooth` estava desligado
  por padrão, então 320×240 era ampliado até o tamanho da janela repetindo
  pixel — o que cria escada mesmo numa imagem sem serrilhado na origem. Parte
  do serrilhado percebido nascia aqui, e não na rasterização.
  `WPJ2_PRESENT_SMOOTH=0` volta ao vizinho-mais-próximo, que continua sendo o
  modo certo para comparar pixel a pixel com o oráculo.

Decodificação completa do `VI_CONTROL` observado (`0x13002`), útil como
referência: type=2 (16 bits), gamma_dither=0, gamma=0, divot=0, serrate=0,
**aa_mode=0** (AA completo), pixel_advance=3, **dither_filter=1**. Bate campo a
campo com o `osViSetSpecialFeatures` do fonte.

## 3. Legendas faltando: é limite de espaço, não bug de código

**Resolvido o diagnóstico.** Os dois caminhos recusavam qualquer tradução mais
longa que o texto inglês:

```c
/* patch de cartucho */          if (translated_len > length) { skipped_long++; continue; }
/* interceptador dinâmico */     if (!translated_n || encoded_n > raw_n) { ... return 0; }
```

O comentário do patcher dizia que uma cadeia maior "será expandida no
interceptador dinâmico" — **não era verdade**, o interceptador aplicava a mesma
restrição. Nenhuma delas era traduzida em lugar nenhum. `textos/LEIA-ME.md`
já contabilizava 694 recusadas.

✅ *Corrigido em parte.* Os dois caminhos agora **medem a folga de enchimento**
em vez de tratar o comprimento do texto como capacidade do bloco, e só
escrevem sobre bytes lidos como zero. Também foi corrigido o laço de
terminação, que deixaria uma cadeia sem `NUL` ao usar a folga.

**Mas o ganho é pequeno: 8740 → 8759 cadeias.** Os recursos do cartucho são
densamente empacotados, quase sem enchimento. Isso é informação valiosa: o
inglês já foi espremido no espaço do japonês, e o português é tipicamente ~20%
mais longo.

**Encaminhamento — e não é código.** As ~2170 restantes precisam de traduções
mais curtas. `textos/apoio/revisao_runtime_limites.tsv` já lista exatamente
quais não cabem; alimentar essa lista de volta no pipeline de LM, pedindo
variantes dentro do limite de bytes de cada uma, é o caminho. A alternativa de
engenharia seria relocar as cadeias e corrigir os ponteiros, mas o patch atual
localiza texto por varredura de conteúdo, não por tabela de ponteiros — seria
um projeto à parte.

Diagnóstico contínuo: `src/scripts/resumo_legendas.py <legendas_rota.tsv>`
conta as rotas. `recurso_ptbr_folga` marca as resgatadas pela folga e
`recurso_ptbr_longo` as que ainda não cabem.

## 4. `tools/sdk-tools` — ainda não explorado

Contém `adpcm` e `tabledesign`, as ferramentas originais de codificação de
áudio. Com o áudio já aceitável, o valor caiu de prioridade, mas seguem sendo
a referência exata para os coeficientes de predição do ADPCM — úteis se os
"pipocos" residuais forem investigados.

## 5. Texturas mal mapeadas

Sem achado novo nesta revisão. Depende de comparar a emissão de
`SETTILE`/`SETTILESIZE` e o cálculo de coordenadas contra a referência. Fica
depois do item 1, porque implementar blend altera o caminho de textura e
tornaria qualquer medição feita agora obsoleta.

## 6. Controller Pack lento: o SI estava pautado na taxa de vídeo ✅ corrigido

Segunda causa, independente da escrita dupla, e maior que ela.

Em `hle_deliver_events` (`runtime/hle.c`) há um portão de 60 Hz. SP e DP já
eram entregues **antes** dele, com o comentário `"SP/DP nao esperam o proximo
retrace"`. A drenagem do SI, porém, ficava **depois** — pautando o barramento
serial na taxa de vídeo. No hardware a conclusão do SI sai em microssegundos;
o portão existe para manter a taxa de quadros honesta, não para atrasar
transferência.

A aritmética do custo:

- a fila de mensagens do SI deste jogo comporta **uma** mensagem — vista no
  despejo de threads como `fila=800F96D8 ... valid=0/1`
- logo o laço de oito entregava de fato **uma conclusão por retrace**, 60/s
- cada bloco de 32 bytes do Controller Pack consome duas (escrita da fita e
  leitura de volta) → 30 blocos/s
- uma imagem de 32 KB são 1024 blocos → **mais de trinta segundos só de espera
  de portão**

Correção: a entrega subiu para antes do portão, e o caminho de sono passa a
retornar cedo quando alguma thread foi acordada — sem isso o `Sleep` anularia
o ganho, porque a thread pronta só rodaria no fim dele.

Medido em 40 s, perfil `input_tardio`:

| | antes | depois |
|---|---|---|
| leituras de controle | 1114 | **2252** |
| `si_r` | 1134 | **3190** |
| `pendentes` | oscila 0–1 | **0 sempre** |

Evidência direta da rajada: na transição de cena entre 22 s e 24 s o `si_w`
salta de 206 para 952 — 746 escritas em ~2 s, cerca de 373/s, contra o teto
antigo de 60/s. Entrada preservada: `pad=1000` e estado final `11/24`, iguais.

**Lição de método, que vale para o resto do projeto:** o portão de 60 Hz é
correto para o *vídeo* e errado para todo o resto. Vale auditar se algum outro
evento ainda está sendo entregue depois dele sem precisar — AI já tem
tratamento próprio, mas PI não foi verificado.
