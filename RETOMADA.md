# Retomada — estado em 23/08/2026

## 29/08/2026 — desempenho do corredor e reversão segura

- O slot 1 isolou o engasgo também sem saída de áudio; o som não é a causa.
- `RT64::processDisplayLists` custa vários milissegundos por tarefa e ainda
  ocupa o caminho cooperativo principal.
- A tentativa inicial de fila gráfica assíncrona congelou o jogo após duas
  tarefas porque o contrato SP/DP não foi reproduzido por completo.
- A tentativa foi retirada. O RT64 voltou ao processamento síncrono validado:
  590 tarefas gráficas, 345 de áudio e 689 apresentações a 60,05 Hz numa rodada
  pelo slot 1, sem congelamento.
- O slot 1 do usuário e o slot 9 reservado para testes não foram sobrescritos.
- Próximo passo de desempenho: portar a fila gráfica completa do
  N64ModernRuntime, incluindo a ordem real de SP/DP e o despertar do
  gerenciador, em vez de apenas tornar a chamada RT64 assíncrona.

### Continuação — Vulkan como backend padrão

- A causa dominante dos picos era a API D3D12 escolhida por `Automatic` no
  Windows: listas de somente 984 bytes bloqueavam 60–100 ms.
- No mesmo slot 1, Vulkan reduziu as tarefas `>=33 ms` de 61 para 10 e colocou
  999/1.074 tarefas abaixo de 2 ms, mantendo a mesma renderização RT64 2x.
- Vulkan agora é o padrão; `WPJ2_RT64_API=d3d12` conserva o comparativo.
- A preparação dos pipelines foi movida para a inicialização e a primeira
  display list resincroniza o relógio para não transformar aquecimento do host
  em VIs atrasados.
- Validação final com áudio: 699 tarefas gráficas, 438 de áudio, 876
  apresentações a 60,05 Hz, sem trava; apenas quatro tarefas gráficas acima de
  33 ms.

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

## 7. Acentuação em português — o que está provado e onde parou

Trabalho de 24/08. **Não commitado.**

### Provado

**O texto chega certo ao motor.** Despejo dos bytes que o formatador consome
para "Ok, vamos lá!":

```
4F 6B 2C 20 76 61 6D 6F 73 20 6C 23 21
 O  k  ,  _  v  a  m  o  s  _  l  #  !
```

`fold_utf8`, substituição e escolha de código funcionam de ponta a ponta.

**A faixa de códigos que o motor aceita**, medida por sondagem em duas rodadas
(`WPJ2_SONDA_CODIGOS` em `runtime/hle.c` troca a cadeia por uma fileira de
candidatos e mostra na tela quais aparecem):

| faixa | resultado |
|---|---|
| `0x20–0x5A`, `0x61–0x7A` | desenha |
| `0x5B–0x60`, `0x7B–0x7E` | **espaço** — caem no vão entre `Z` e `a` |
| `≥ 0xE0` | **controle** — consome o caractere seguinte |

A última linha explica o primeiro sintoma: com o acento em `0xF8`, `lá!` virou
`l` + marca solta e o `!` sumiu.

**Os 14 destinos finais**, confirmados um a um na tela:
`# $ % & * + / ; < = > @ ( )`. Consequência: o TSV **não pode conter** esses
símbolos.

**A fonte 8×8 e a composição.** Formato vindo de `func_80091A04`
(`tools/wonder-source/src/code/code_8F1A0.c`): 1bpp, 8 bytes por glifo,
indexado por `código × 8`. Os acentos existem soltos — agudo `0x27`,
circunflexo **`0x5E`**, til **`0x7E`** (coluna 15, não 14; o `¥` japonês na
coluna 13 desloca a contagem visual). A composição funciona e está verificada
na arte do log: `á` e `ç` saem corretos.

### O bloqueio

**O texto exibido não vem dessas tabelas.** Provado por vandalismo
(`WPJ2_VANDALIZAR_FONTE`): encher a letra `a` de tinta nas tabelas apontadas
por `D_8015F810/F868/F870/F878` **não alterou uma única letra na tela**.

Esse mesmo teste, feito antes com a guarda fraca, corrompeu um gráfico e
revelou um "Progress" escondido — porque os ponteiros nem sempre apontam para
uma fonte, e a guarda antiga só exigia "alguma letra tem tinta". Agora a
guarda é `parece_fonte`, com assinatura completa, e não há mais corrupção.

### Próximo passo

Localizar a fonte que a caixa de diálogo realmente usa. O caminho mais direto
é a varredura de RDRAM com `parece_fonte` seguida de vandalismo em cada tabela
encontrada — a que alterar a tela é a certa. A varredura já existe em
`legendas_despejar_fonte` sob `WPJ2_DESPEJO_FONTE`, com o instante ajustável
por `WPJ2_DESPEJO_FONTE_RETRACE`; falta acoplar o vandalismo a ela e cuidar
para que a cena esteja na tela no momento da captura.

Cuidado registrado: o detector não pode exigir `0x00–0x1F` vazios. A tabela
real **tem** blocos gráficos nessa faixa, e essa exigência fez a varredura
devolver zero tabelas.

### Duas armadilhas caras, registradas para não se repetirem

**1. Heurística que aponta não autoriza escrita.** Tentei alimentar a
composição com o resultado da varredura de RDRAM — compor em toda tabela cuja
assinatura batesse, sem precisar saber qual é de quem. A tela ficou **preta**.
Por mais critérioso que `parece_fonte` seja, ele ainda casa com dado de
recurso: as reincidências em `0x802B62xx` ficam na vizinhança da própria
cadeia de diálogo, em `0x2B1F10`. A varredura voltou a ser só diagnóstico
(`WPJ2_VARRER_FONTES`), e só se escreve onde um ponteiro do jogo mandou.

**2. Cachear endereço de fonte corrompe.** Guardar as bases num catálogo e
recompor nelas todo quadro parecia inofensivo. Corrompeu o menu com blocos
magenta e verde: o jogo **reaproveita aquela memória** e nós continuávamos
escrevendo lá. Revalidar com `parece_fonte` não protege — dado reaproveitado
às vezes passa. A composição agora resolve o ponteiro a cada quadro e escreve
no que ele apontar naquele instante.

**Nota de método.** Perdi ciclos comparando quadros capturados no fim da
execução, cujo instante varia entre execuções — cheguei a ler cena legítima
como corrupção. Existe agora `WPJ2_ACENTOS=0` para desligar a composição e
comparar as duas telas na mesma condição. Comparação sem controle não vale.

## 8. Caça à fonte do diálogo — candidatos eliminados

Trabalho de 24/08, segunda metade. **Não commitado.**

O objetivo é achar de onde o texto exibido tira os pixels. Dois candidatos
foram eliminados por medição, e a eliminação é o resultado — economiza o
próximo ciclo.

### Eliminado: fonte 8×8 em `D_8015F810/F868/F870/F878`

Composição verificada correta na tabela (arte no log). Encher a letra `a` de
tinta (`WPJ2_VANDALIZAR_FONTE`) **não alterou uma única letra na tela**.

### Eliminado: `func_80094230` e a fonte 8×12 de `D_8015F880`

Este parecia certo. `wonder-source/src/code/code_8F1A0.c` dá a assinatura
exata:

```c
if (arg0 < 0xFF) {
    for (i = 0; i < 12; i++) {
        sp23 = *((arg0 * 6 * 2) + i + D_8015F880);
```

Ou seja fonte 8×12, 12 bytes por glifo, indexada por **código de caractere**.
Implementei a composição nela — encaixe adaptativo do acento conforme o espaço
livre de cada letra, cedilha embaixo — e o glifo saiu correto no log.

A tela não mudou. O contador então respondeu por quê:

```
[glifo] total com a0<0xFF: 0 de 108001 chamadas
```

**Nenhuma chamada desenha caractere.** `func_80094230` é só o caminho de
objeto/sprite nesta cena. Todos os `obj` observados (`0x140`, `0x224`,
`0x22A`…) caem fora do `if`.

Isso também explica um mistério anterior: a tabela exportada saía zerada nos
índices registrados porque aqueles índices nunca foram glifos.

### Onde continuar

Sobra `func_80090E58`, o formatador — que já envolvemos e que é chamado **uma
vez por caractere**, com o ponteiro da cadeia avançando um byte a cada
chamada. É ele que decide como desenhar. No wonder-source está como
`GLOBAL_ASM`, então a referência não ajuda; o caminho é instrumentar quais
funções executam entre a entrada e a saída dele e seguir a que lê pixels.

Nota de valor: a nota antiga do nosso código dizia que `func_80094230` "busca
os pixels na tabela 0x8015F880". A parte do formato estava certa e a parte do
uso estava errada — mais um caso de dedução nossa que a referência confirma
pela metade. Conferir sempre as duas metades.

### Continuação da caça — mais um candidato fora, e um achado colateral

**`func_80090784` escreve no framebuffer, não no atlas.** O contador antigo só
media escritas em `0x802CEF20` e devolvia zero; isso nunca significou que o
plotador estivesse parado. Sem o filtro ele aparece com milhões de chamadas.

**Atribuição de chamador não é confiável.** O histograma por
`trace_last_func()` apontou `func_80095F9C` com 3,5 milhões de chamadas — mas
ao ler essa função no wonder-source ela é um **mapeador de código**, não um
desenhista:

```c
sp4 = (arg0 & 0xFF00) >> 8;
sp0 = arg0 & 0xFF;
if (sp0 >= 0x80 && sp0 < 0xFF) arg0 -= 1;
arg0 = (arg0 - sp4 * 0x44) + 0x2244;
```

Um mapeador não chama o plotador milhões de vezes. Com fibers,
`trace_last_func()` devolve a última função *traçada*, não o chamador real —
a mesma armadilha que já invalidou a identificação por `ra`. **Não usar esse
histograma para nomear quem desenha.**

**Achado colateral, útil para a tradução:** o jogo trabalha com um espaço de
caractere de **dois bytes** e converte para índice de objeto pela fórmula
acima. Isso pode explicar por que um byte solto `0x23` não chega ao caminho de
glifo como esperávamos, e merece ser levado em conta antes da próxima
tentativa de acentuação.

**Estado dos candidatos:**

| candidato | veredito |
|---|---|
| fonte 8×8 (`D_8015F810` e cia.) | eliminado — vandalismo sem efeito |
| `func_80094230` / 8×12 (`D_8015F880`) | eliminado — 0 de 108 mil chamadas com código de caractere |
| `func_80091A04` | congela em 2500 chamadas; não acompanha o texto |
| `func_80095F9C` | é mapeador de código, não desenhista |

**Próximo passo sugerido, livre de atribuição.** Recortar do framebuffer o
bitmap de uma letra que está na tela (por exemplo o `M` de "Menu") e procurar
esse padrão de bits na RDRAM. A fonte precisa conter o desenho literalmente, e
essa busca não depende de saber quem chama quem — que é justamente o elo em
que as ferramentas deste projeto são fracas.

## 9. A fonte do diálogo, localizada

**`0x800E5250` contém o `M` de "Menu", com passo de 2 bytes entre linhas.**

Achado por busca livre de atribuição: transcreveu-se o desenho da letra direto
do framebuffer (`src/scripts/recortar_glifo.py`) e procurou-se esse padrão na
memória. O bitmap exibido tem de existir literalmente em algum lugar — e essa
busca não depende de saber quem chama quem, que é o elo fraco aqui.

```
#.....#   0x82        encontrado em 0x800E5250
##...##   0xC6        passo 2 bytes  ->  linha de 16 pixels
##...##   0xC6        celula de glifo: 16 bytes
#.#.#.#   0xAA
#.#.#.#   0xAA
#.#.#.#   0xAA
#..#..#   0x92
#..#..#   0x92
```

O passo 2 explica por que todas as buscas anteriores falharam: as linhas do
glifo **não são contíguas**. A fonte é uma imagem de 16 pixels de largura e a
letra ocupa a metade esquerda de cada linha.

**A tabela não é indexada por ASCII.** Assumindo base = `M` − 0x4D×16, o índice
0x41 devolve um `G`, o 0x42 vem vazio e o 0x30 quase vazio. Combina com o
mapeador de dois bytes de `func_80095F9C`
(`arg0 = arg0 − sp4*0x44 + 0x2244`): o índice de glifo é derivado, não é o
código do caractere.

**Próximo passo.** Renderizar a região inteira em torno de `0x800E4D80` como
imagem de 16 pixels de largura e ler a ordem real dos glifos. Com a ordem em
mãos, a acentuação já está pronta — toda a lógica de composição existe e foi
validada; falta só apontá-la para esta tabela e para o índice certo.

---

# Revisão gráfica dos projetos de referência

## O que já está mapeado e não precisa de nova investigação

- **Blend e coverage existem** em `runtime/rsp.c` (`rdp_blender_source_over`,
  `g_aa_cobertura[320*240]`). O AA é *opt-in* por `WPJ2_RDP_AA` porque falta
  cobertura por amostra também no Z-buffer.
- **O jogo pede AA**: `VI_CONTROL` observado `0x13002` tem `aa_mode = 0`, o
  modo mais completo.
- **Gamma correto**, filtro de dither agora ligado pelo bit real (item 2).

## Onde os três projetos ainda têm o que dar

**`libreultra`** — é a referência para o que o VI espera receber. Vale
comparar nossa sequência de `osViSetMode`/`osViSwapBuffer` com a dele antes de
mexer em qualquer coisa de apresentação; divergência ali explica transição
estranha melhor do que qualquer ajuste no rasterizador.

**`wonder-source`** — é onde está o realce vermelho do menu que falta. O
desenho do cursor é código do jogo, não do RDP, e o projeto está decompilado.
Procurar a rotina do menu de save e ver que primitiva ela emite responde se é
alpha blending, retângulo de preenchimento ou paleta piscante. É investigação
barata e de retorno visível.

**`sdk-tools`** — pouco a oferecer em gráficos; o valor dele é áudio
(`adpcm`, `tabledesign`).

## Recomendação de ordem

1. Realce do menu, via `wonder-source` — sintoma concreto, código disponível.
2. Serrilhado por supersampling na apresentação, não por coverage fiel. Já
   medimos que a ampliação sem filtro criava escada sozinha; resolver a borda
   por amostragem é muito mais barato que completar o coverage no Z-buffer.
3. Texturas mal mapeadas depois do blend, porque implementar blend altera o
   caminho de textura e invalidaria qualquer medição feita antes.

## 10. Fonte: estrutura totalmente decifrada, origem ainda não é a exibida

### Decifrado (é o resultado desta rodada)

Busca livre de atribuição: transcrever o `M` de "Menu" do framebuffer e
procurar o desenho na memória. Achado em `0x800E5250`, com **passo de 2 bytes
entre linhas** — motivo pelo qual toda busca por bytes contíguos falhava. O
hexdump em volta deu a estrutura inteira:

```
800E5230  80 06  80 00 ... F8 00      'L', largura 6
800E5250  82 08  C6 00 ... 92 00      'M', largura 8
800E5270  84 07  C4 00 ... 84 00      'N', largura 7
```

| campo | valor |
|---|---|
| base | `0x800E48B0` |
| célula | **32 bytes** por glifo |
| índice | **ASCII** (L, M, N consecutivos) |
| linhas | até 16, uma por `u16`, byte alto = bitmap |
| offset 1 | **largura** — a fonte é proporcional |

Confirmado com `A` (`10 28 28 28 44 7C 82 82`) e com `g`, que usa dez linhas
por causa da descendente.

A composição foi implementada nessa estrutura (`compor_fonte32`), com encaixe
adaptativo do acento, cedilha abaixo e cópia da largura. O `á` sai correto no
log. A localização usa **cache com revalidação** — confere o `A` antes de
escrever, para não repetir a corrupção causada por cache cego.

### O que ainda bloqueia

Vandalizar a letra `a` nessa fonte **não altera a tela**, mesmo ela sendo
comprovadamente a origem do `M` exibido. Portanto `0x800E48B0` é uma **cópia
de origem**: o jogo transfere o glifo para outro lugar antes de desenhar, e é
essa cópia que aparece.

### Para encerrar a parte da fonte

Falta um passo, e ele é bem definido: **descobrir para onde o glifo é copiado**
e aplicar a composição lá, ou antes da cópia.

Duas rotas, ambas viáveis:

1. **Antes da cópia.** Se a cópia vem de um DMA ou `memcpy` de recurso,
   compor na origem logo antes dela resolve. O ponto de entrada provável é o
   carregamento de recurso que já envolvemos (`func_800BD218`).
2. **Achar o destino.** Repetir a busca pelo `M` mais tarde na execução, com a
   caixa de diálogo já na tela — se aparecer em dois endereços, o segundo é a
   cópia viva, e é nele que se escreve.

A rota 2 é a mais barata e usa ferramenta que já existe
(`WPJ2_PROCURAR_GLIFO` com `WPJ2_DESPEJO_FONTE_RETRACE` ajustado para depois
do menu aparecer). Hoje a busca para na primeira ocorrência; basta deixá-la
listar todas.

### Medição que fecha o diagnóstico da fonte

Gravamos uma marca reconhecível na célula do `á` e conferimos no quadro
seguinte:

```
[sobrescrita] marca sobreviveu em 0 de 52800 quadros
```

**Zero.** E a leitura de volta *dentro da mesma chamada* funciona — o log
`[fonte32]` mostra o `á` composto corretamente. Logo a escrita é válida e o
jogo **re-transfere a fonte continuamente**, desfazendo a acentuação antes de
desenhar. Não é problema de endereço nem de formato: é de momento.

Compor logo após `func_800BD218` (cópia de recurso ROM→RDRAM) **não resolveu**,
o que elimina esse caminho como origem da re-transferência.

**Próximo candidato, já visível nos traços:** `func_800BD1FC`. Ele aparece
sempre imediatamente antes de `func_800BD218` e devolve um endereço
(`[b23c4] ->BD1FC-a ... ret=001044E4`), com comportamento de descompressor. Se
a fonte é descomprimida a cada quadro, é ali que ela nasce — e é ali que a
composição precisa entrar.

Ferramenta pronta para a próxima sessão: `WPJ2_MEDIR_SOBRESCRITA=1` responde em
uma execução se a marca passou a sobreviver. Enquanto ela disser zero, o ponto
de escrita ainda está errado.

## 11. Correção de um erro meu, e o diagnóstico real

### A medição "0 de 52800" estava errada — o defeito era da sonda

Eu havia concluído que o jogo re-transferia a fonte a cada quadro. **Não
re-transfere.** A sonda gravava a marca no *início* de `compor_fonte32` e a
própria composição a sobrescrevia logo abaixo, na mesma chamada. Eu media a
minha escrita apagando a si mesma.

Com a marca armada no *fim* da função, ela sobrevive:

```
[cerco-cru] retrace-antes base=0x800E48B0 base_ok=1 marca=1 viva=1 byte=0x5A
```

Toda conclusão que dependia daquele "0 de 52800" fica retirada — inclusive a
suspeita sobre `func_800BD1FC`.

### O diálogo e o menu usam a MESMA fonte

Transcrevi o `O` de "Ok," dentro da caixa (limiar invertido, porque ali o texto
é claro sobre escuro) e procurei o desenho:

```
'O' exibido  ->  0x800E5290  =  0x800E48B0 + 'O'*0x20
```

Slot `'O'` exato, mesma base. Não são duas fontes: é uma só, indexada por
ASCII, células de 32 bytes.

### O que realmente bloqueia

Encher o slot `'O'` de tinta **não altera o `O` na tela**. Mesma coisa com o
slot `0x23`. Combinado com o fato de que nossas escritas persistem, só resta
uma explicação:

**O texto é rasterizado uma vez e não é redesenhado a partir da fonte.** A
caixa de diálogo vira pixels no framebuffer e fica lá; mudar a fonte depois
não tem efeito nenhum.

### Consequência para o conserto

Não adianta recompor melhor, nem mais vezes, nem em outro lugar. A composição
tem de estar aplicada **antes da primeira rasterização daquele texto** — ou o
texto precisa ser forçado a redesenhar.

O log mostra `[fonte32] acentuacao aplicada` muito cedo, bem antes de o menu
aparecer, o que aparentemente satisfaz essa condição e contradiz o resultado.
É aí que está o fio a puxar.

**Lacuna de medição a fechar primeiro:** o cerco limitou-se a 12 amostras e
todas caíram no mesmo ponto (`retrace-antes`), então ainda não sabemos se a
marca sobrevive *no instante do plotador*. Basta contar por ponto de
observação em vez de um teto global — é a diferença entre saber e supor, e foi
exatamente o que me fez errar antes.

## 12. Fecho do ciclo: o que está provado e o furo identificado

### Provado por medição

| fato | evidência |
|---|---|
| A composição persiste | marca viva em **100%** de 2,8 M observações, inclusive no plotador |
| Tabela em RDRAM `0x800E48B0` | `O` e `M` exibidos localizados nos slots ASCII exatos |
| Tabela na ROM `0x000E54B0` | mesma assinatura, passo 2 |
| Patch de ROM funciona | a tradução PT-BR aparece na tela por esse mesmo mecanismo |
| **Nenhuma das duas é a fonte desenhada** | encher **toda** a faixa `0x20–0x7F` de tinta na ROM não altera um pixel do texto |

A última linha é decisiva: se o texto viesse de qualquer uma dessas tabelas,
apagá-las inteiras teria destruído a tela. Não destruiu.

### O furo, e ele é meu

`f32_localizar` guarda a primeira base encontrada e só a revalida pelo `'A'`.
Como o `'A'` continua íntegro numa cópia adormecida para sempre, a função
**nunca volta a procurar** — e nós ficamos escrevendo numa cópia que ninguém
lê. Existem pelo menos duas cópias (RDRAM e ROM); provavelmente há uma
terceira, viva, produzida na carga da cena.

Isto também explica por que todo teste de vandalismo deu negativo: não é que a
fonte não seja usada, é que estávamos na cópia errada o tempo todo.

### Próximo passo, concreto

Trocar "localizar uma base" por **enumerar todas** e compor em cada uma. A
verificação por assinatura (`parece_fonte`/`f32_confere`) já é forte o
bastante para autorizar a escrita — foi ela que eliminou a corrupção quando o
cache cego causou problema.

Segundo passo, se ainda assim não aparecer: rodar a busca do bitmap com o menu
na tela **e no mesmo instante** em que o quadro é capturado. Hoje a busca roda
no retrace 2600 e o quadro sai no fim da execução; se a cena recarregar entre
os dois, comparo coisas diferentes.

### Nota de método

Três conclusões minhas foram retiradas neste ciclo — "o jogo recarrega a fonte
a cada quadro", "a caixa usa outra fonte" e "`func_800BD1FC` é o descompressor
culpado". Todas vinham de medições cujo defeito estava na sonda, não no jogo.
O martelo (apagar a fonte inteira) deveria ter vindo antes de qualquer
hipótese: é o teste que separa "a fonte não é usada" de "mexi na fonte errada",
e custa uma execução.

## 13. Varredura da referência — o que ela entregou, e o impasse

### Ganhos reais da varredura (não eram palpite)

**`func_8009230C`** — a rotina de desenho que faltava, e casa campo a campo com
o formato medido da tela:

```c
sp27 = *(arg0 * 0x20 + i * 2 + D_8015F874 - 0x400);
```

32 bytes por glifo, passo 2 entre linhas, doze linhas, e o viés de `0x400` que
eu não conhecia (a tabela começa no código 0x20).

**`sys_main.c`** — explica as múltiplas cópias e por que patchear a ROM não
adiantou:

```c
D_8015F880 = ...(gSeg_639B20_ROM_START, ...);
D_8015F874 = SysMem_HeapAllocMark(...);
Spi_DecompressAsset(D_8015F880, sp1DC, D_8015F874);
```

A fonte está **comprimida** na ROM (segmento `0x639B20`) e é descomprimida
para o heap. O que eu achava por assinatura em `0x000E54B0` era outra coisa.

**Família de nove ponteiros** (`F800`…`F880`) e pelo menos quatro rotinas de
desenho, com alturas 8, 10 e 12. Passei a compor em todos os que passam na
assinatura, em vez de escolher um por execução.

### O controle que valida a metodologia

Rodei a mesma execução **sem** a tradução: a tela mostra "Start without
saving" e "Ok, let's go!". Portanto o quadro capturado **reflete** o que este
runtime faz, e os testes de vandalismo não eram nulos por construção.

### O impasse, declarado com precisão

- a família inteira de ponteiros produz **uma** tabela válida: `0x800E48B0`
- ela contém os glifos exibidos nos slots ASCII exatos (`M`, `O` localizados a
  partir dos pixels da tela)
- nossas escritas ali persistem (marca viva em 100% de 2,8 M observações)
- apagar essa tabela **inteira** não altera um pixel do texto
- e o quadro comprovadamente responde a outras alterações nossas

Essas cinco coisas não podem ser todas verdadeiras se o texto fosse desenhado
lendo essa tabela no momento do desenho. Alguma premissa entre elas é falsa, e
não é mais produtivo descobrir qual por tentativa.

### Próximo passo: usar o oráculo, não mais palpite

Duas ações, nesta ordem:

1. **Project64 como oráculo.** Rodar a mesma ROM e pôr um *breakpoint de
   leitura* na região da fonte enquanto a caixa de diálogo está na tela. O
   endereço que o hardware emulado realmente lê encerra a questão em uma
   sessão — é exatamente o que nenhuma das nossas sondas consegue, porque
   escrevemos código que executa, não que observa acessos.

2. **Busca pelo glifo original.** Procurar na RDRAM todas as cópias do `#`
   original (formato ×0x20, passo 2) com a nossa tabela já patcheada. Qualquer
   cópia que ainda tenha o `#` intacto é candidata a ser a viva.

A ação 2 é barata e roda aqui; a 1 é a que dá certeza. Convém fazer as duas.

## 14. A descoberta que reenquadra tudo: a referência é de OUTRA ROM

### Fatos novos, medidos

**`tools/wonder-source/symbol_addrs.txt` existe** — um mapa símbolo→endereço
completo, com 841 linhas. Recurso que eu não estava usando e que dá endereços
prontos:

```
Spi_DecompressAsset                  = 0x800BF0B4
Spi_GetHeader                        = 0x800BED70
SysMem_DmaCopy                       = 0x800BD218
SysMem_GetPhysicalAddressFromVirtual = 0x800BD1FC
```

De passagem, isso corrige outro palpite meu: o `BD1FC` que aparecia nos traços
e que eu suspeitei ser o descompressor é apenas um tradutor de endereço.

**`Spi_DecompressAsset` (0x800BF0B4) NUNCA é chamado** na nossa execução.
Envolvi a função e o traço não emitiu uma linha sequer.

**A tabela em `0x800E48B0` é dormente.** Apagar as 256 células não altera um
pixel de texto — e o controle (rodar sem tradução, e ver "Ok, let's go!" em
inglês) prova que o quadro responde às nossas alterações.

### A conclusão

`wonder-source` decompila a **ROM japonesa original**. Nós rodamos a **tradução
inglesa do Ryu**, e esse patch trocou a fonte japonesa por uma ASCII. Em
`sys_main.c` a fonte é um asset **comprimido** descomprimido no heap; na nossa
ROM esse caminho **não é exercido**.

Ou seja: a referência e a nossa ROM divergem exatamente no subsistema que eu
estava investigando. Seguir o fluxo do wonder-source levava, com toda a
coerência interna, a cópias que o nosso binário não usa.

Isso explica de uma vez:

- por que `Spi_DecompressAsset` não roda
- por que as tabelas encontradas por assinatura são dormentes
- por que patchear a ROM em `0x000E54B0` não teve efeito
- por que `func_80094230` nunca recebe código de caractere

### Como atacar daqui

O caminho da fonte **na ROM traduzida** precisa ser levantado no nosso próprio
binário, não na referência. Duas frentes:

1. **Project64 como oráculo** — breakpoint de *leitura* na região da fonte com
   a caixa de diálogo na tela. É a única ferramenta aqui que observa acessos;
   as nossas sondas só executam código.
2. **`tools/wpj2e_v1`** — o patch do Ryu está no projeto (`.ips`, ignorado pelo
   git). Comparar a ROM original com a traduzida revela exatamente quais
   regiões ele alterou, e a fonte estará entre elas. Isso dá o endereço por
   diferença, sem depender de observar acessos.

A frente 2 é barata, roda aqui e não estava sendo usada — é provavelmente o
próximo passo certo.

## 15. Acentuação — falso positivo retirado e rota visual corrigida

A captura humana posterior mostrou `Qual di#rio voc+ deseja usar?`; portanto,
a alegação anterior de sucesso pelo conteúdo intermediário da memória estava
errada. A partir daqui acentuação só é validada pelo framebuffer final.

O caminho ativo da ROM T-En foi localizado no ramo `a0 >= 0xFF` de
`func_80094230`. O formatador do Ryu converte ASCII em Shift-JIS e entrega
índices truncados como `0x193` (`#`), `0x17B` (`+`), `0x23C` (`a`) e `0x240`
(`e`). O compositor lê células de 24 bytes através de `D_8015F880`, com viés
`-0x1200`; as tabelas ASCII de 12/32 bytes estudadas antes são dormentes para
esse diálogo.

O wrapper agora troca o bitmap do objeto reservado no instante imediatamente
anterior ao desenho. A reprodução automática até o menu registrou os objetos
`0x0193 <- 0x023C` e `0x017B <- 0x0240`, e o framebuffer final mostra
`Qual diário você...` com os dois acentos, na fonte do jogo e sem sobreposição.
Os compositores legados ficaram disponíveis apenas com
`WPJ2_ACENTOS_LEGADO=1`.

Também foi corrigida a métrica das marcas. O agudo copiado do apóstrofo
japonês ficava no extremo direito da célula e aparecia sobre o caractere
seguinte; agora agudo, grave, circunflexo e til têm desenhos de duas linhas
centrados. `í` substitui o ponto por uma pequena diagonal, e `º`/`ª` passaram
a ser gravados junto das demais compostas.

Relatório consolidado: `analise/projeto/acentuacao_ptbr.md`.

---

# 15. Sessão de 24/08 (tarde) — acentuação, desempenho e o mapa da tradução

**Nada commitado a partir daqui** (trabalho em co-work com o Codex; commit sob
pedido).

## Acentuação PT-BR — resolvida e publicada (`0200360`)

A correção veio do Codex a partir da análise acumulada aqui, e foi verificada
de forma independente: build limpo, `test_legendas_recursos` OK, e a captura
do menu mostra **"Ok, vamos lá!"** contra o controle em inglês.

Dois erros meus que ela corrigiu, e vale registrar:

- os `obj=0x22A`, `0x140` que eu **descartei** eram os índices de glifo reais.
  Descartei-os por causa do `if (arg0 < 0xFF)` do wonder-source — que é o ramo
  da ROM **japonesa**. O patch Ryu converte ASCII → EUC-JP → Shift-JIS →
  índice truncado em dez bits;
- a célula é de **24 bytes**, não 32. Eu inferi `0x20` do espaçamento `L`/`M`/`N`
  numa tabela dormente.

O caminho vivo é `D_8015F880 + indice*24 + linha*2 - 0x1200`, recomposto
imediatamente antes de `func_80094230` consumir o objeto.

## Desempenho — regressão desfeita, e o jogo ficou mais rápido que antes

A lentidão de cursor **não vinha da acentuação**: vinha da instrumentação que
eu deixei para trás na caça à fonte.

`func_80090784` desenha **um pixel** e roda milhões de vezes — 2,8 M em 40 s
pelo próprio histograma. Havia ali um cerco de marca e um histograma de
chamadores, ambos chamando `getenv()` por chamada. Removidos.

Também: chaves de ambiente em caminho quente passaram a `static` lido uma vez
(`func_80090E58` fazia dois `getenv` por letra), e a rede de segurança que
varria **8 MB de RDRAM por quadro** durante troca de cena foi espaçada.

**Medido: 2252 → 3345 leituras em 40 s, +48%.** Acima do que era antes de todo
o trabalho de acentuação.

## O mapa da tradução — o achado estrutural desta sessão

Parei de tratar frase por frase e medi o catálogo inteiro contra o orçamento
real da ROM (`src/scripts/auditar_orcamento.py`, mesmo critério do runtime).

| classe | quantidade | natureza |
|---|---|---|
| cabem e são aplicadas | **1977** | funcionando |
| na ROM, tradução longa demais | **1687** | conteúdo — encurtar |
| **não estão na ROM em texto plano** | **2063** | comprimidas — outro caminho |

**46% de recusa** entre as localizadas. É isto que explica "palavras em inglês
espalhadas pela interface", e é por isso que caçar frase a frase não terminaria
nunca.

A lista completa das 1687, com limite e tamanho atual de cada uma, ordenada por
quanto falta, está em `textos/apoio/revisao_runtime_limites.tsv` — consumível
direto pelo pipeline de LM. **Quase todas as piores faltam por 1 byte.**

### Por que o orçamento é apertado

As opções de menu vivem num bloco contíguo separado por `\n`, **sem enchimento
de zeros**. O limite é o tamanho exato do original:

```
0x68B8E8  "Start\n"  "Delete Diary\n"  "Copy Diary\n"
0x68B90C  "Start without saving\n"
```

Corrigidas para caber: `Jogar` (5), `Apagar` (6), `Copiar` (6). A escolha de
palavra é revisável; o limite não.

### Guarda de terminador no patcher

O patcher trocava a cadeia em qualquer posição onde os bytes batessem, mesmo
sendo **prefixo** de outra. Agora só troca quando o byte seguinte não é ASCII
imprimível.

Medido o efeito: o total caiu de 8759 para 8390 aplicadas, mas **apenas 20
entradas** perdem todas as ocorrências — e são justamente as que corrompiam:

```
'Yes.'  ->  "Yes... forever"
'Yup!'  ->  "Yup! I too will join"
'Day'   ->  "Days..."
```

Os 369 patches a menos eram escritas erradas evitadas.

A regra é por exclusão (rejeita só ASCII imprimível) em vez de lista de
terminadores aceitos, porque o texto embute controles `E0/E1/E2` e uma fala
pode terminar num deles.

## Classe 3 — o que falta investigar

`Progress`, `Day` e os rótulos da tela de saves **não existem em texto plano na
ROM**. Estão comprimidos, como a fonte.

O caminho de runtime (`legendas_substituir_recurso`) age depois da
descompressão e seria a rota natural — mas o log mostra que ele mal dispara:
**duas substituições na execução inteira**. Ou essas telas não passam por ele,
ou passam por um caminho que não interceptamos.

Próximo passo: instrumentar quem carrega os textos da tela de saves e ver se
há um ponto equivalente ao `Spi_DecompressAsset` da fonte, onde a cadeia exista
descomprimida antes de ser consumida.

## Ferramentas novas

- `src/scripts/auditar_orcamento.py` — audita o catálogo inteiro contra a ROM
- `src/scripts/orcamento_texto.py` — orçamento de uma cadeia específica
- `src/scripts/recortar_glifo.py` — transcreve um glifo do framebuffer
- `src/scripts/comparar_quadros.py` — dois quadros lado a lado

## Pendente

Item das transições com falhas após o start — não atacado.

## Atualização Codex — 24/08/2026: causa comum do realce e das faixas

O trace completo do menu `11/24`, cruzado com `wonder-source` e os macros RDP
de `libreultra`, encontrou o quad que faltava. O realce não é `FILLRECT` nem
`TEXRECT`: a ROM emite `VTX` + dois `TRI1`, desliga `G_TEXTURE`, usa
`G_CC_SHADE`, render mode `0x00504A50` e alfa nos vértices.

O rasterizador respeitava `G_TEXTURE_OFF` somente no 3D `12/50`; no 2D ele
amostrava o tile anterior. A correção global em `runtime/rsp.c` recuperou, em
replay determinístico:

- retângulo vermelho piscante sobre a opção do menu;
- retângulo cobrindo a ficha rosa inteira na seleção de diário;
- cursor sem disputar/duplicar os pixels do realce;
- transição 2D em sequência limpa: escurecimento, preto, menu escuro, menu.

A tentativa anterior de traduzir `Day`/`Progress` editando o framebuffer foi
removida. Com PT-BR ligado, `Jogar`/`Apagar`/`Copiar` aparecem pelo caminho
nativo; `Day`/`Progress` continuam ingleses porque já chegam rasterizados na
imagem CI8 dinâmica (`timg` por volta de `0x80358F50`). Esse é o próximo alvo
de tradução, no escritor/recurso da imagem, não numa camada sobreposta.

## Atualização Codex — 25/08/2026: estado gráfico entre tarefas

O relato de que a tela congelava enquanto a música continuava separou o defeito
de um deadlock. `G_TEXTURE_OFF`, necessário para o realce vermelho, estava
vazando para a `OSTask` gráfica seguinte. Lotes 2D que não repetiam
`gSPTexture(G_ON)` deixavam de atualizar a imagem, embora jogo, VI e áudio
seguissem executando.

`runtime/rsp.c` agora reinicia apenas o marcador de definição no começo de cada
tarefa: depois de um comando `TEXTURE`, seu ON/OFF é respeitado dentro daquela
tarefa; antes dele, o 2D conserva o estado inicial texturizado. O replay completo
mantém menu, realce e transições. A afirmação anterior de que isto também
resolvera o congelamento após `End` foi retirada: a reprodução usada terminava
antes do ponto real, que ocorre 5–6 segundos depois da confirmação.

Em paralelo, o CRC dos 32 bytes do Controller Pak foi alinhado ao
`__osContDataCrc` de `libreultra`, `wonder-source` e Project64, incluindo os oito
bits zero finais. É uma correção válida do PFS, mas não era a causa deste
congelamento visual.

## Atualização Codex — 25/08/2026: congelamento tardio após `End`

O teste longo confirmou congelamento da thread principal, não só da imagem:
gráficos e PIF paravam enquanto o áudio continuava, e a execução girava em
`func_8008ED4C`/`vsprintf`. Desligar PT-BR eliminava o defeito; manter o catálogo
e desligar apenas o patch estático do cartucho também.

A bisseção identificou exatamente `It's morning...` → `É de manhã...`, em
`0x008006DC`. O glifo interno de `ã` é o byte `%`; antes de o texto chegar ao
renderer ele ainda passa por formatação, portanto o byte era consumido como
especificador e corrompia a cadeia de argumentos. `runtime/legendas.c` agora
recusa no patch estático traduções com essa colisão e as deixa para a tradução
do recurso vivo, posterior ao `sprintf`.

No replay completo com PT-BR, a contagem voltou a 4.702 tarefas gráficas em 18
segundos e a thread principal terminou aguardando em `0x800CC98C`, em vez de
presa no formatador.

---

# 16. Sessão de 25/08 — rótulos de menu: a hipótese da rasterização caiu

**Não commitado.**

## O que se acreditava

O `PENDENCIAS.md` afirmava que `Day`, `Progress`, `Message Speed`,
`Bird's Speed`, `Fast` e `Slow` "já chegam rasterizados numa imagem CI8
intermediária", e que seria preciso interceptar quem gera essa textura.

## O que a medição mostrou

Despejo de strings da RDRAM na tela de velocidade (`WPJ2_LEGENDAS_RDRAM_CAPTURE`
com o replay `menu_velocidade`):

```
0x0F8C9A   Message Speed
0x0F8E8F   O Imp*rio Siliconiano     <- tradução nossa, já aplicada ali perto
```

O texto **existe vivo em memória**, e o nosso patcher **alcança aquela região**.
Não era rasterização: era o orçamento de bytes de novo.

Sete dos nove rótulos estão no bloco `0x68B9xx`/`0x68BExx` da ROM, separados
por `\n` e sem enchimento — o limite é o tamanho exato do original.

| rótulo | limite | antes | agora |
|---|---|---|---|
| `Message Speed` | 13 | `Velocidade da Mensagem` (22) | `Vel. Mensagem` |
| `Bird's Speed` | 12 | `Velocidade do Bird` (18) | `Vel. do Bird` |
| `Back` | 4 | `Voltar` (6) | `Sair` |
| `End` | 3 | — | `Fim` |
| `Normal` | 6 | `Normal` | já correto |
| `Fast` | 4 | `Rápido` (6) | **pendente** |
| `Slow` | 4 | `Lento` (5) | **pendente** |

Validado na tela: a tela de velocidade exibe "Vel. Mensagem", "Vel. do Bird",
"Fim" e "Sair", com o realce azul do cursor visível.

## O que resta

`Fast` e `Slow` cabem em quatro bytes — é decisão de redação, não de código.

`Progress` e `Day` continuam sem origem textual: não estão na ROM em texto
plano (`Day` só aparece dentro de "Days") nem apareceram no despejo da RDRAM.

## Atualização Codex — 25/08/2026: falas parcialmente em inglês

As cinco capturas da abertura não eram cinco traduções ausentes. Elas expõem
um mesmo formato da ROM T-En: uma fala é formada por vários fragmentos do
catálogo e controles que inserem nomes. O patch in-place traduzia apenas os
fragmentos curtos, gerando frases EN/PT e, na fala longa, composição visual
corrompida durante a rolagem.

`legendas_realocar_recurso_composto` agora atua somente na entrada de
`func_80090E58`, depois que `vsprintf` resolveu os controles. A rotina casa o
maior fragmento em cada posição, conserva texto variável e espaços de
fronteira e publica uma cadeia única na arena. Não ativar essa recomposição nas
rotas anteriores ao formatador: `ã` usa o byte `%` e voltaria a travar a thread
principal. Os cinco exemplos foram adicionados ao teste unitário, que passou;
dois replays de regressão também permaneceram ativos.

`Day` e `Progress` não pertencem a essa solução. Permanecem pré-rasterizados na
imagem CI8 da seleção de diário e devem ser corrigidos no escritor/recurso
nativo, nunca por sobreposição tardia no framebuffer.

## Atualização Codex — 25/08/2026: controles E1 e trava após confirmar A

As instruções do tutorial usam pares `E1 <cor>` e `E1 FF` ao redor das ações e
botões. Eles já eram preservados, mas vários fragmentos (`"Yes"`, `"Good"`,
`R-Button`, `Pad`, conectivos) estavam ausentes de `traducao_ptbr.tsv`. O
catálogo foi completado e o teste agora cobre a cadeia com controles de cor.

As falas longas ainda podiam corromper a rolagem quando todos os fragmentos já
chegavam em português: a recomposição exigia ao menos um trecho inglês. Foi
adicionado um índice pela forma PT-BR codificada; ele reconhece esses trechos,
preserva os bytes e consolida a mensagem numa única cadeia estável.

O congelamento depois de responder com A não ocorreu em `printf`. O log entra
em `func_800CB840` (`osDestroyThread`) ao remover a thread 15 e nunca retorna;
PIF e tarefas gráficas param, enquanto o áudio hospedado continua. O percurso
recompilado da lista global de threads não tinha limite e encontrou uma lista
cíclica. `sched_destroy_thread` agora remove o alvo de forma limitada da fila
de escalonamento e da lista ativa, sincroniza seu fiber e permite recriar o
mesmo objeto OSThread caso o pool do jogo o reutilize.
Só para essas duas a hipótese da imagem pré-rasterizada continua de pé.

## Lição de método

A hipótese anterior era plausível e estava documentada como "causa
confirmada". O que a derrubou foi uma medição barata que ninguém tinha feito:
procurar a palavra como texto na memória. Vale desconfiar de "confirmado" que
não aponta para a medida que o confirmou.

---

# 17. Encurtar as traduções ou mexer no limite? — evidência

**Não commitado.** A pergunta é estratégica, e a melhor evidência é o que a
própria tradução inglesa fez, já que ela enfrentou o mesmo problema vindo do
japonês.

## O que o patch do Ryu fez (lido do `.ips`)

`src/scripts/analisar_ips.py` resume `tools/wpj2e_v1/Wpj2e_v1_z64.ips`:

```
registros            : 410
bytes alterados      : 812 KiB
maior offset tocado  : 0x880000        <- a ROM original terminava em 0x800000

faixas contiguas, as maiores:
  0x7FFFFE..0x880000   524290 bytes    <- regiao NOVA, meio megabyte
  0x6A69E0..0x6C6FD8   132600 bytes
  0x68B8E0..0x68BE08     1320 bytes    <- nosso bloco de menu, in-place
```

**Ele expandiu a ROM em 512 KiB.** A região nova contém 5099 cadeias de
diálogo em inglês e **60% dela é zero** — cerca de 316 KB livres.

Ou seja: mexer no limite não é especulação neste jogo. Já foi feito, e a
infraestrutura está na ROM.

## Onde as nossas recusas estão

```
recusas ABAIXO de 0x800000 (blocos in-place)  :  169
recusas ACIMA  de 0x800000 (area realocada)   : 1506
```

Contra a intuição: o grosso está dentro da área realocada. Os 316 KB de zeros
existem, mas **não colados a cada cadeia** — as strings estão empacotadas e a
folga está concentrada noutro lugar. Por isso a folga não nos serve sem
reapontar.

## Leitura

Encurtar e realocar não são alternativas excludentes; atacam populações
diferentes:

| | alcance | custo | risco |
|---|---|---|---|
| encurtar | as 169 apertadas + as que faltam por 1 byte | baixo, em lote pelo pipeline | nenhum |
| realocar | as 1506 de uma vez, e remove o teto de vez | alto | precisa achar o ponteiro |

A experiência deste projeto com caça a ponteiro (a fonte custou vários ciclos)
recomenda não começar por aí sem antes confirmar, numa sessão focada, se a
tabela existe. Uma tentativa de localizar ponteiros para a região não foi
conclusiva: os acertos em `0x1011CB/DF/F3` têm passo de 20 bytes, sugerindo
tabela de registros, mas os valores procurados são pequenos e podem coincidir.

**Caminho mais barato para realocar, já identificado no `PENDENCIAS.md`:** não
mexer na ROM, e sim no runtime — "redirecionar os dois ponteiros publicados por
`func_80096B38` para slots expansíveis". Ali nós controlamos a memória, não
precisamos de espaco contiguo na ROM, e o interceptador de recurso ja existe.

## Recomendacao

1. Encurtar agora o que a auditoria lista, em lote. Desbloqueia sem risco e a
   lista com limite exato de cada entrada ja esta pronta.
2. Em paralelo, uma sessao focada em `func_80096B38` para decidir se a
   realocacao em runtime e viavel. Esse unico fato decide se o teto cai ou se
   conviveremos com ele.

---

# 18. Garantia "tudo em memória, ROM intacta" — auditada e reforçada

**Pergunta:** a ROM está sendo carregada inteira em memória? Alguma coisa
escreve no arquivo original?

**Resposta medida, não presumida.** `load_rom()` em `runtime/runtime.c` já
atendia ao princípio:

- `fopen(path, "rb")` — somente leitura;
- `ftell` + `malloc(g_rom_size)` + um único `fread` da imagem **inteira**
  (8.912.896 bytes, nada de streaming ou leitura parcial);
- `fclose(f)` imediatamente depois — o arquivo nunca é reaberto;
- o alvo dos patches é `cart = g_rdram + CART_OFFSET`, uma **segunda cópia**
  produzida por `copy_swapped()`. `legendas_aplicar_cartucho()` opera só nela.

Verificação empírica: MD5 do arquivo antes e depois de uma execução completa —
`3c02f56dd7b1a06be83a7a288755612f` nos dois casos, tamanho idêntico.

## O furo que existia: identidade por CRC1

A checagem de identidade usava o **CRC1 lido em `0x10`**. Esse é um campo
*dentro* do arquivo, não um hash do conteúdo: uma imagem adulterada carrega o
mesmo CRC1 sem esforço. Ou seja, a garantia que queremos — *"qualquer ROM que
bata com o nosso MD5 funciona no projeto"* — não estava sendo verificada de
fato, e um usuário com uma ROM diferente veria sintomas confusos (texto e
recursos em deslocamentos errados) sem nenhum aviso.

**Correção.** MD5 sobre o buffer inteiro já carregado, calculado logo após o
`fread`, com o valor de referência fixado em `WPJ2_ROM_MD5`. Implementação
local em `runtime/runtime.c` (sem dependência nova, sem novo arquivo nos
scripts de build, que listam fontes explicitamente).

A implementação foi validada **antes** de entrar no runtime, contra três
vetores conhecidos e contra o Python (`temp/testmd5.c`):

| entrada | resultado |
|---|---|
| `""` | `d41d8cd98f00b204e9800998ecf8427e` |
| `"abc"` | `900150983cd24fb0d6963f7d28e17f72` |
| `"The quick brown fox…"` | `9e107d9d372bb6826bd81d3542a419d6` |
| ROM Ryu v1.0 | `3c02f56dd7b1a06be83a7a288755612f` |

Saída atual no boot:

```
ROM  : ...[T-En by Ryu v1.0].z64
       8.50 MB, nome 'WONDER PROJECT J2   ', CRC1 4F1E88F7
       MD5 3c02f56dd7b1a06be83a7a288755612f
```

Com MD5 divergente, o aviso passa a dizer o que realmente importa: que o
projeto só é validado contra aquela imagem e que deslocamentos podem não bater.
O CRC1 continua sendo impresso, mas rebaixado a informação de cabeçalho — se
ele destoar de um MD5 correto, isso vira um aviso separado.

## ⚠ Este MD5 é provisório e sai depois

A amarração a **um** MD5 é andaime de desenvolvimento, não objetivo do projeto.
O alvo é rodar **outras imagens do mesmo jogo** — a japonesa original e outras
traduções. Nesse mundo um MD5 fixo vira estorvo.

O que precisa existir antes de removê-lo: os deslocamentos de texto e recurso
hoje são constantes casadas com a build Ryu v1.0. Trocar o MD5 por identificação
dinâmica (CRC1 + tamanho) só faz sentido junto com **perfis de ROM** — cada
imagem conhecida trazendo seus próprios deslocamentos. Enquanto isso não existe,
o aviso evita o pior cenário, que é diagnosticar horas de sintoma estranho
causado por ROM diferente.

Marcado no código com `>>> PROVISORIO -- A REMOVER <<<` acima de
`WPJ2_ROM_MD5`. Sai o bloco inteiro: `md5_hex`, a constante e o aviso em
`load_rom`.

**Nota de método (para não repetir):** o build visual é `WPJ2_RELEASE` e faz
`freopen("NUL", "w", stdout)` quando `WPJ2_DEBUG` não está ligado. Qualquer
tentativa de inspecionar saída de console precisa de `WPJ2_DEBUG=1`, senão o
log sai vazio e parece que o programa não rodou.

---

# 19. Limite de textos — arena validada no consumidor correto

A primeira implementação experimental reapontava as duas saídas de
`func_80096B38` individualmente. O `wonder-source` mostra que elas são cursores
sincronizados do mesmo bloco; `func_80096C6C` avança ambos conforme o layout
original. Reapontar um cursor para uma string maior desviava o jogo do
subestado `24` para `50` e provocava uma explosão de chamadas.

A realocação foi movida para `func_800319B0`, que recebe uma cadeia individual
e somente publica seu ponteiro em `D_801879D0`. O carregador agora mantém seus
dois cursores intactos. O formatador `func_80090E58(char**)` continua podendo
receber diretamente um slot expansível.

O A/B forçando todas as traduções para a arena terminou no mesmo subestado e
com carga equivalente ao modo sem arena. Um catálogo sintético confirmou uma
tradução de 69 bytes sobre origem de 21: `recurso_ptbr_arena` seguido por
`recurso_ptbr_arena_cache`, 3.657 tarefas gráficas e subestado final `24`.

O replay tardio após `End` também permaneceu estável, com 5.042 tarefas
gráficas. `TESTAR.bat` habilita o modo seguro `WPJ2_REALOCAR=1`; o modo 2 fica
apenas para diagnóstico. A análise consolidada está em
`analise/projeto/realocacao_textos.md`.

A rota paralela `func_80096D40`, que publica uma tabela sem chamar `319B0`, foi
coberta separadamente: o corpo original constrói a tabela e o wrapper reaponta
somente seus elementos depois. O replay final com essa rota habilitada manteve
4.440 tarefas gráficas em 18 segundos e a thread principal aguardando
normalmente em `0x800CC98C`.

---

# 20. Rótulos completos da tela de velocidade

Alterar `Vel. Mensagem` para `Velocidade da Mensagem` fazia o menu voltar ao
inglês porque esse rótulo não percorre as arenas de diálogo. O trace de ROM
localizou a rota exata:

```
Message Speed  0x0068B924 -> 0x00363D50  48 bytes
Fast           0x0068B93C -> 0x00363DD0  48 bytes
Slow           0x0068B944 -> 0x00363E10  48 bytes
Bird's Speed   0x0068B968 -> 0x00363F10  48 bytes
```

O limite de 13/12/4 bytes existia somente porque o patch estático escrevia por
cima da cadeia na ROM. `func_800BD218` já cria uma cópia privada de 48 bytes
para cada rótulo. O runtime agora substitui o texto logo depois dessa cópia,
passando a capacidade explícita do slot ao interceptador.

O replay exibiu `Velocidade da Mensagem`, `Velocidade do Bird`, `Rápido`,
`Lento`, `Fim` e `Sair`, preservando fonte, realce e composição originais. A
execução permaneceu estável no estado `11/24`, com 3.747 tarefas gráficas.
`Day` e `Progress` continuam sendo o único subcaso de menu sem origem textual.

## Regressão detectada e retirada no mesmo hook

A primeira versão chamava o interceptador também para **todas** as demais
cópias de `func_800BD218`. Isso traduziu repetidamente o identificador técnico
`SPI1` e voltou a inserir glifos reservados antes de `printf`; 5–6 segundos
depois de `Fim`, a thread principal parava novamente em `vsprintf`.

O hook foi restringido ao intervalo comprovado do bloco de menus
`0x0068B8E0..0x0068BF48`. Validação após a restrição:

- replay tardio: 6.511 tarefas gráficas, `main_func=0x800CC98C`, sem trava;
- ocorrências indevidas de `SPI1`: zero;
- replay do menu: os seis rótulos continuam PT-BR, 4.636 tarefas gráficas e
  estado `11/24`.

---

# 21. Cobertura total do banco Ryu e rolagem das falas longas

`blue` e `green` não eram falhas da extração: estavam no legado nos offsets
`0x00800AAD` e `0x00800AB9`, mas o pipeline não os promoveu porque eram
fragmentos curtos. Essa exclusão era incorreta. A auditoria agora considera
toda fonte do banco textual oficial `0x00800000..0x0081FFFF`, inclusive nomes,
aspas e espaços nas bordas. Foram encontradas 98 chaves ausentes e todas foram
adicionadas ao mapa ativo; o veredito atual é 5.886 chaves e zero lacunas nesse
banco. Falsos positivos fora dele continuam fora do catálogo.

Nas falas longas, a arena já continha português correto. A hipótese desta
rodada foi uma dupla quebra e a implementação chegou a remover delimitadores
tipográficos e recalcular linhas. **Esse resultado foi rejeitado depois em
execução real**: causava sobreposição e deslocava controles. A seção 32 contém
a substituição segura, que preserva toda quebra explícita e só troca um espaço
por `\n` quando a quebra automática cairia dentro de uma palavra.

---

# 22. Congelamento tardio da abertura — proteção geral por fase

O F5 no congelamento isolou `main_func=0x8008EDA4`, com gráficos presos em
7.882 tarefas e áudio ainda ativo. O dump da RDRAM mostrou a fala de Geppetto
já traduzida antes do formatador; o `ã` de `atenção` usa `0x25`, interpretado
ali como comando `%`.

Todas as rotas precoces de texto agora usam a API
`legendas_*_antes_formatador`: se a tradução colidir com o alfabeto de
formatos, ela é adiada sem modificar o recurso. A rota posterior a
`func_8008EDA4` aplica a tradução normalmente. Isso cobre genericamente
`func_80096B38`, `func_800319B0` e `func_80096D40`, em vez de criar exceção
para uma fala.

Validação automática de 175 segundos: 10.127 tarefas gráficas, função ativa
fora de `0x8008EDA4`, filas em espera normal e registros pareados de
`recurso_ptbr_adiado_formato`/substituição tardia. O teste unitário também
reproduz a colisão e passou. O encerramento com código 1 foi o timeout
deliberado do perfil sem janela, não uma falha do runtime.

---

# 23. RT64 integrado ao runtime principal

O teste isolado de `Vmarcelo49/wpj2-recomp` provou que o RT64 reproduzia o
corredor com qualidade próxima ao Project64, mas carregava a ROM japonesa e
seu áudio SDL2 ficava dessincronizado. A integração definitiva foi feita na
fronteira da `OSTask`, sem trocar o restante do projeto.

`runtime/rsp.c` agora encaminha somente `M_GFXTASK` para uma interface de
backend. Tarefas de áudio continuam no RSP recompilado local. A ponte recebe
RDRAM, DMEM, IMEM, registradores VI, display list e microcódigo reais;
`runtime/video.c` pede ao RT64 a apresentação na mesma janela Win32. Tradução
PT-BR, acentuação, input, Controller Pak, saves, replay, áudio e cadência
permanecem implementados pelo nosso runtime.

Arquivos próprios adicionados:

```text
runtime/rt64_backend.c
runtime/rt64_backend.h
runtime/rt64_backend_api.h
src/rt64_bridge/CMakeLists.txt
src/rt64_bridge/wpj2_rt64_bridge.cpp
```

A ponte é uma DLL carregada dinamicamente. Sua ausência ou falha de
inicialização mantém o backend CPU; uma display list rejeitada desativa o RT64
para o restante da execução e retorna imediatamente à renderização CPU. Os
artefatos pesados não ficam na raiz:

```text
build/rt64_runtime/wpj2_rt64_bridge.dll
build/rt64_runtime/SDL2.dll
build/rt64_runtime/dxcompiler.dll
build/rt64_runtime/dxil.dll
```

Comandos reproduzíveis:

```bat
tools\build_probe.cmd rt64
TESTAR.bat
```

O launcher também foi normalizado para CRLF; em LF o `cmd.exe` cortava o
início das linhas e ignorava o perfil. O build nativo usa MSVC e Ninja. Como o
Windows SDK instalado não expõe GPU Upload Heaps e o caminho validado usa
Vulkan, o CMake compatibiliza apenas esse tipo do backend D3D12 opcional.

Validação final:

- o log registrou `[rt64] backend grafico nativo ativo`;
- o processo permaneceu responsivo, sem display lists rejeitadas;
- abertura, corredor, personagem e caixa de diálogo foram exibidos em PT-BR;
- o usuário avaliou o resultado como **“funcionando perfeito”** e confirmou a
  qualidade visual esperada;
- o áudio ouvido foi o pipeline local já corrigido, não o áudio
  dessincronizado da referência externa.

A análise detalhada está em `analise/projeto/integracao_rt64.md`. O que restava
nesse ponto era percorrer cenas tardias, adaptar F5 à saída GPU e expor
resolução/upscale como opções. A seção seguinte registra a conclusão do F5; o
backend CPU segue disponível para A/B e fallback.

---

# 24. RT64 padrão, F5 da saída GPU e avanço momentâneo

RT64 passou a ser o backend padrão tanto no loader quanto no `TESTAR.bat`.
Nenhum argumento é necessário; `TESTAR.bat cpu` força o rasterizador antigo e
a falha de carga da ponte continua acionando o fallback automaticamente.

O caminho RT64 retornava de `video_present` antes de atualizar os metadados do
último VI, portanto F5 não tinha sequer um ponto válido para capturar. Agora a
rota registra RDRAM, origem, dimensões e formato antes de apresentar. Como os
pixels finais estão no swapchain Vulkan e não em `g_pixels`, F5 captura a área
cliente já composta pelo DWM, sem bordas/título, em 640×480. A captura validada
registrou:

```text
captura_visual=rt64_dwm 640x480
video=320x237 formato=2
```

O BMP continha a imagem RT64 correta da abertura; os arquivos auxiliares de
estado, RDRAM textual e AList continuam sendo produzidos como antes.

F11 deixou de alternar vozes de diagnóstico. Enquanto pressionado, ele muda o
retrace emulado para quatro vezes a taxa normal e retira a fila WinMM do
caminho crítico. Ao entrar, a fila de áudio antigo é descartada; ao soltar, o
próximo DMA volta a tocar no ponto corrente. O prazo QPC é reiniciado nas duas
transições para não suspender fibers no prazo da escala anterior.

Medição automática em uma execução RT64 real:

```text
normal, 2 s:       120 retraces
F11, 2 s:          446 retraces
após soltar, 2 s:  retorno à cadência normal
razão observada:   3,72x (alvo nominal 4x, limitado pela carga real)
```

O projeto `Vmarcelo49/wpj2-recomp` não forneceu save-state nem checkpoint. Seu
README lista save-states entre os recursos ainda inexistentes; ele possui
somente Controller Pak/Memory Pak, descrito pelo próprio projeto como não
testado. Nosso F2/F4 por reinício e replay determinístico permanece sendo uma
implementação própria e mais segura que restaurar apenas a RDRAM sobre fibers
antigas.

---

# 25. F11 em 8× e janela RT64 redimensionável

O multiplicador nominal do avanço momentâneo foi elevado de 4× para 8×.
Áudio continua fora do caminho crítico enquanto F11 permanece pressionado e
volta à cadência corrente assim que a tecla é solta. Em execução RT64 real:

```text
normal, 2 s:  120 retraces
F11, 2 s:     881 retraces
razão:        7,34x (alvo nominal 8x, limitado pela carga real)
```

A janela deixou de ter estilo fixo e agora pode ser redimensionada ou
maximizada. Durante o arraste, `WM_SIZING` conserva a área cliente em 4:3 e
`WM_GETMINMAXINFO` impõe mínimo de 320×240. O teste tentou impor 1000×500 e o
runtime corrigiu para cliente 984×738, razão 4:3. F5 também foi revalidado após
o redimensionamento:

```text
captura_visual=rt64_dwm 800x600
```

A pendência das junções da grade da logo ENIX foi encerrada: o backend RT64
não reproduz as costuras que existiam no rasterizador CPU. O histórico do
diagnóstico permanece nas análises; o item saiu de `PENDENCIAS.md`.

Na revisão conjunta seguinte, o usuário também confirmou como resolvida pelo
RT64 a pendência de fidelidade de materiais e amostragem do corredor 3D. O
diagnóstico permanece em `analise/projeto/renderizacao_3d_fast3d.md`, mas o
item deixou a lista de trabalho ativo.

O usuário confirmou ainda que o RT64 corrigiu a entrada vertical e a
persistência do personagem central do corredor durante os diálogos. Essa
pendência também foi encerrada; suas sondas antigas continuam preservadas
somente como histórico de diagnóstico do rasterizador CPU.

A integração do backend de GPU RT64 também foi aprovada como encerrada. RT64
é o caminho padrão, o CPU permanece como fallback/A-B e F5 acompanha a saída
GPU. Resolução interna, upscale e filtros configuráveis ficam para uma fase
posterior de melhorias de PC e não mantêm esta implementação em aberto.

A revisão das pendências manteve aberto o item de legendas PT-BR maiores que
os recursos ingleses. A arena dinâmica já resolve a capacidade de memória,
mas textos longos reais ainda apresentam casos de paginação, posicionamento
ou scroll que precisam de validação interativa.

`Day` e `Progress` no menu de diários também foram confirmados como ainda
pendentes. Encerrada a revisão conjunta, `PENDENCIAS.md` ficou reduzido a dois
itens ativos: textos PT-BR longos e esses dois rótulos gráficos do menu.

---

# 26. README recalibrado após a integração RT64

O README foi refeito para descrever o runtime atual, e não mais o estágio do
rasterizador CPU. As quatro fases agora são estimativas ponderadas e pontuadas:

```text
Fase 1 — protótipo funcional:     99%
Fase 2 — fidelidade de execução:  85%
Fase 3 — extração total:          15%
Fase 4 — modernização para PC:    45%
```

Cada fase lista entregas e critérios explícitos para chegar a 100%. Foram
documentados RT64 padrão/fallback CPU, áudio nativo, controles completos,
Controller Pak, PT-BR, bookmarks, janela 4:3 e F11 em 8×. Referências novas
incluem RT64, RecompFrontend, o1heap, `wpj2-recomp`, `wonder`, `josette`, a
tradução inglesa, libreultra e sdk-tools.

As três imagens antigas foram substituídas por capturas F5 da execução RT64
atual: ENIX, título e corredor 3D com diálogo PT-BR. Os BMPs, dumps e metadados
usados na seleção foram tratados como temporários e removidos depois da
consolidação em `docs/`.

---

# 27. Primeira rodada de correção sistemática das falas restantes

Cinco capturas F5 foram correlacionadas com o catálogo, o banco vivo, a fila
de mensagens e o buffer apresentado. Três causas reais foram separadas:

- falas com `ã` eram adiadas antes do formatador e não podiam mais ser
  reconhecidas depois que variáveis como `Josette` eram expandidas;
- o comando animado `E2 06 80 10` era lido como se tivesse somente dois bytes
  e fazia o parser rejeitar a mensagem;
- `Doctor...` permanecia inglês no próprio catálogo ativo.

O runtime agora codifica `ã` como `0x7F` somente durante a fase anterior a
`func_8008EDA4` e o restaura como glifo `0x25` em `func_80090E58`. Isso permite
traduzir a mensagem inteira cedo, preservando variáveis e sem expor `%` ao
formatador. A forma PT-BR já codificada também é reconhecida e protegida em
reentradas do mesmo bloco. Runtime e extrator preservam controles E2 de quatro bytes;
`Don't be so sel...fish.` foi catalogado como `Não seja tão ego...ísta.` e
`Doctor...` como `Doutor...`.

O suposto reaproveitamento indevido no quinto F5 não apareceu nos dados: a
fila aponta para uma arena distinta com a tradução correta, enquanto o buffer
visual ainda contém a frase anterior e o estado é `0x2D`. Ele fica para
revalidação interativa, sem correção paliativa.

`test_legendas_recursos.exe` passou incluindo a colisão de fase e o controle
animado. `tools/build_probe.cmd rt64` recompilou com sucesso o executável usado
por `TESTAR.bat`.

---

# 28. F11 com frame skip e diagnóstico da falsa repetição

O novo F5 mostrou `Ali, uma pessoa...` na janela, mas a fila de diálogo já
continha `Eh? Mas por que!?` em outro slot. A execução tinha 33.727 retraces e
somente 7.295 tarefas gráficas: a simulação acelerada estava muito à frente da
apresentação RT64. Não houve repetição de chave nem de tradução.

F11 agora mantém toda a lógica do jogo, inclusive filas, textos e transições,
mas descarta a rasterização de sete em cada oito tarefas gráficas e suspende a
apresentação da janela enquanto está pressionado. Isso remove RT64 do caminho
crítico do 8x e evita mostrar uma fala obsoleta como se fosse a atual. Ao
soltar, a apresentação recomeça na próxima tarefa completa. A leitura física
de F11 corrige também a perda de `WM_KEYUP` observável durante reconstruções
da swapchain na transição 3D/2D. O relatório final passa a informar quantos
quadros foram descartados pelo turbo.

---

# 29. Replay F4 em 8x e causa real da repetição de falas

O replay acionado por F4 agora compartilha o turbo de navegação do F11. Durante
a reconstrução até o poll gravado pelo F2, o runtime descarta sete de cada oito
tarefas gráficas, não apresenta RT64 e não envia áudio ao host. A lógica,
transições e entradas continuam integrais. Um replay existente alcançou
exatamente a leitura 4.995 e retornou automaticamente à velocidade normal.

A repetição de `Ali, uma pessoa...` não vinha do catálogo. A tradução seguinte
`Eh? Mas por que!?` existia corretamente em `0x80410028`, porém
`SysMem_DmaCopy` recusava origens acima de 4 MB e não a copiava ao buffer vivo.
A fila seguia adiante enquanto a imagem mantinha a frase anterior. O wrapper de
`func_800BD218` passou a copiar logicamente a faixa reservada da arena PT-BR;
todo o restante continua na implementação original do jogo.

---

# 30. F4 máximo, controles E0 multilinha e fala animada ausente

O retorno F4 agora omite 100% da rasterização até alcançar a leitura de
controle gravada pelo F2. A simulação guest permanece completa; F11 continua
em 8x com uma lista gráfica a cada oito. Um bookmark existente atingiu
exatamente a leitura 7.995 e restaurou a cadência normal. A troca de processo
continua intencional: pilhas de fibers, continuações C e RT64 não formam um
estado que possa ser reinicializado com segurança dentro da mesma janela.

Os textos `não se preocuJosettepe` e `por mJosetteim` foram explicados pelo
controle `E0 01`, que insere o nome da protagonista. Ele pertence à coluna zero
da segunda linha, mas era reposicionado pela proporção do tamanho total. O
mapeamento agora conserva linha e coluna; o teste unitário exige `\n E0 01`.

`Dr. Geppetto: Live your life to the fullest.` não constava do catálogo por
estar numa região dinâmica e conter três controles animados `E2 06`. Foi
adicionada como `Dr. Geppetto: Viva sua vida plenamente.`, mantendo os
controles. A primeira passada conservadora de gênero corrigiu formas
inequivocamente dirigidas à Josette nos catálogos, sem alterar automaticamente
falas cujo interlocutor é ambíguo.

Validação: `test_legendas_recursos.exe` passou; `tools/build_probe.cmd rt64`
recompilou o runtime; replay headless alcançou o alvo 7.995 dentro da janela de
seis segundos.

---

# 31. Refluxo seguro de diálogos e cerco de `Day`/`Progress`

O buffer do F5 continha `Tenho certeza que você será gentilmente vigiada...`
sem quebra; `vig`/`iada` era produzido pelo limite automático do formatador.
O runtime agora aplica word-wrap apenas ao banco narrativo confirmado, volta
ao último espaço quando a linha excede 38 glifos e preserva todas as quebras
explícitas. Menus e outros tipos de caixa permanecem fora desse tratamento.

Para `Day` e `Progress`, a referência em `wonder-source` reenquadrou o caso:
`func_80094230` compõe glifos/objetos no atlas CI8. `TESTAR.bat` ativa um ring
diagnóstico e cada F5 grava `glifos_f5_NNN.tsv` com sequência, objeto,
coordenadas, avanço, estado e chamador. Isso permitirá localizar e substituir
o recurso nativo das palavras, sem repetir a tentativa rejeitada de pintar no
framebuffer.

---

# 32. Correção do refluxo e origem gráfica de `Day`/`Progress`

A validação interativa rejeitou o refluxo descrito na seção anterior. Ele
removia quebras existentes e recalculava a mensagem inteira; isso produziu
texto sobreposto e deslocou o controle `E0 01`, fazendo `Josette` aparecer no
meio de palavras. A implementação foi substituída por uma operação que mantém
o comprimento: com `\n`/`\r` explícito não toca em nada; sem quebra explícita,
troca apenas o espaço anterior ao ponto em que o limite nativo de 38 glifos
cortaria uma palavra. Os testes verificam preservação byte a byte da quebra
original e o ajuste isolado da quebra automática.

Para os rótulos do diário, um ring de 524.288 posições preservou os 144.184
objetos desde o início do replay e ainda não encontrou `Day` nem `Progress`.
Logo eles não passam pelo compositor normal. `Spi_DecompressAsset` revelou o
recurso exclusivo do patch inglês em ROM `0x0068E100`, com 0x282E0 bytes
descomprimidos; a ROM japonesa usa outro banco e não carrega esse endereço.
Essa é agora a origem concreta a dissecar, substituindo o recurso antes da
rasterização e não desenhando texto sobre o framebuffer.

Validação local: `test_legendas_recursos.exe` passou e
`tools/build_probe.cmd rt64` gerou `wpj2_probe.exe` com a ponte RT64 ativa.

---

# 33. Quebra proporcional confirmada pelos quatro F5

Os F5 `001..004` provaram que a sobreposição restante era uma dupla rolagem,
não texto ausente. A sequência de objetos mostra, por exemplo, `Bird` chegando
a x=228; a vírgula cai sozinha em y=14 e o `\n` do recurso imediatamente
reinicia a linha seguinte em y=0. O mesmo padrão ocorreu em `então`, em espaços
de preenchimento depois de `hora de` e depois de `determinada`.

O limite anterior de 38 caracteres foi descartado. A fonte é proporcional e
os avanços medidos em `func_80094230` variam de 2 a 8 pixels; o corte nativo
fica em aproximadamente 232 px. O compositor PT-BR agora calcula pixels. Em
mensagens compostas, espaços imediatamente anteriores a `\n` são removidos e,
se necessário, a própria quebra explícita é reposicionada para o espaço antes
da palavra que estouraria. A posição antiga vira espaço: conserva-se uma única
quebra, em vez de somar uma quebra nossa à automática do jogo.

`test_legendas_recursos.exe` passou, incluindo a divisão real antes de `Bird`,
e `tools/build_probe.cmd rt64` recompilou `wpj2_probe.exe`.

---

# 34. Linha terminal preservada e diagnóstico da trava completa da loja

O F5 da instrução de três linhas mostrou que a terceira não pertencia ao
recurso: `botão!` era desenhado novamente em `y=0` porque o refluxo havia
movido o `0A` terminal para dentro da fala. Esse delimitador encerra a exibição
e não pode ser usado como uma quebra de layout. O runtime agora o preserva e
condensa a tradução nas duas linhas nativas. Os fragmentos mistos também foram
corrigidos, removendo `it` e `press`. O teste cobre a mensagem completa e seus
controles animados.

O travamento posterior na loja foi classificado como congelamento completo:
imagem e áudio param juntos. O log disponível foi obtido antes da ocorrência
e continuava avançando normalmente, portanto não sustenta uma correção por
palpite. `TESTAR.bat` agora ativa `WPJ2_STALL_WATCHDOG`; se retraces, tarefas
de áudio e polls ficarem simultaneamente imóveis por dez segundos, o runtime
grava automaticamente estado, trilha, RDRAM e glifos em
`temp/projeto/padrao`, sem exigir F5. Essa captura será a base da correção geral
da loja e dos seus textos ainda em inglês.

Validação: `test_legendas_recursos.exe` passou e `tools/build_probe.cmd rt64`
gerou novamente `wpj2_probe.exe` com RT64.

---

# 35. Fala do botão, recursos da loja e causa concreta do congelamento

O `f5_001` mostrou que a instrução de carregar objetos continuava sendo
composta pelos fragmentos ingleses `yellow ` e ` button!`, separados pelos
controles coloridos E1. Traduzir o fragmento inicial como `Botão Amarelo`
mudava o papel dos controles e provocava a sobreposição. A composição agora é
`amarelo [controle] botão!`, preservando a ordem e a paginação nativas.

O `f5_002` confirmou uma segunda rota de texto na loja. Nomes de itens já
passavam pelo catálogo, mas `Buy`, `Back`, a ajuda e as descrições permaneciam
em pequenas cadeias NUL de tabelas carregadas por SPI/PI. Foi acrescentada uma
substituição por bloco que só aceita chaves exatas do catálogo e grava dentro
do slot original e de sua folga NUL. Não existe camada desenhada sobre o
framebuffer. O catálogo cobre os rótulos da loja e as descrições dos dois
óleos; o teste unitário comprova dois slots consecutivos.

O watchdog automático capturou a trava completa. Não era apenas uma impressão
de acúmulo: depois de 94.105 retraces, imagem, áudio e CPU pararam juntos. A
trilha terminou em `__osSpDeviceBusy`, embora a fila RSP estivesse vazia, sem
conclusões perdidas e sem DMA pendente. Como a ponte executa as DMAs de SP de
modo síncrono, o estado ocupado vinha do espelho MMIO obsoleto. `__osSpSetPc`
e `__osSpDeviceBusy` agora usam o estado nativo do RSP e foram incluídas nas
substituições do recompilado.

Validação local: build RT64 concluído; `test_legendas_recursos.exe` passou com
a fala controlada e os slots estáticos da loja. Falta validar interativamente
a sequência longa de compras/navegação para confirmar que não há uma segunda
causa de congelamento.

---

# 36. Stress reproduzível e log automático de inglês sem catálogo

Foi criado o perfil `TESTAR.bat stress N`. Ele reproduz o bookmark F2 em turbo
até o poll exato e, depois de uma margem de 120 leituras, alterna A/B,
direcionais, C-Buttons, Z, L/R e analógico. A lógica, o áudio e as tarefas RSP
continuam ativos; somente janela e saída sonora são omitidas. O watchdog de
congelamento permanece ligado.

Três rodadas totalizaram 345 segundos. A principal chegou à loja e recarregou
seus itens continuamente: 85.361 retraces, 77.832 tarefas RSP, 38.107 chamadas
do heap, fila RSP com pico 1/256 e zero descarte. As três encerraram pelo tempo
configurado, sem watchdog, exceção ou falha antecipada. Isso aumenta a
confiança na correção de `__osSpDeviceBusy`, mas não equivale a jogar até os
créditos, pois a entrada sintética não garante avanço narrativo.

Toda execução de `TESTAR.bat` também cria `traducao_ausentes.tsv`. A sonda
fica na entrada do formatador, registra apenas frases consumidas, remove
controles, evita sufixos por glifo, deduplica e filtra português dinâmico já
traduzido. A última calibração não encontrou inglês sem chave nas cenas
percorridas. Texto incorporado a imagem continua fora dessa sonda e deve ser
mapeado como asset.

Validação: `test_legendas_recursos.exe` OK, build RT64 OK e `git diff --check`
sem erros de conteúdo.

---

# 37. Stress acelerado a 480 Hz

O perfil de stress passou a aceitar uma seed no terceiro argumento e usa
480 Hz depois de reconstruir o bookmark: `TESTAR.bat stress 240 7`. A rodada
alcançou aproximadamente 180 leituras de controle por segundo e terminou pelo
limite de 240 segundos, sem watchdog ou exceção.

Carga acumulada: 176.737 retraces, 160.787 tarefas RSP, 321.576 transferências
de SP, 33.730 DMAs de cartucho, 112.583 chamadas do heap, 3.813.463 LOADBLOCKs
e cerca de 6,49 GB enviados à TMEM. A fila RSP atingiu apenas 1/256, com zero
descarte, e nenhuma DMA PI foi recusada. O antigo congelamento da loja não se
reproduziu mesmo sob carga muito superior à execução interativa.

A sonda `traducao_ausentes.tsv` ficou vazia além do cabeçalho. A sequência
variou recursos e o portão interno, mas permaneceu principalmente no estado
da loja (`1/1`); stress sintético testa estabilidade, não resolve escolhas de
gameplay. Para avançar a cobertura, o melhor próximo insumo é um novo bookmark
gravado manualmente depois da loja.

O texto histórico do encerramento por timeout dizia que o boot estava preso
esperando hardware, mesmo em testes deliberadamente temporizados. A mensagem
agora é neutra: `tempo limite configurado atingido`.

---

# 38. Stress dirigido da loja, validação texto→saída e coordenadas de Bird

Foi criado `TESTAR.bat stress_loja N`. Diferente do stress aleatório, ele usa
as próprias frases consumidas pelo formatador como marcos: reconhece o fim do
tutorial e a menção à Loja de Computadores antes de emitir entradas. As
tentativas provaram que o bookmark atual termina ainda no tutorial, e não no
computador. Também expuseram duas reproduções intermitentes que congelaram em
`11/24`, tela preta e polls parados em ~18,6 mil, enquanto áudio/retraces
continuaram. Portanto a estabilidade do replay F4 ainda não está encerrada.

O formatador agora gera `traducao_validacao.tsv`, comparando o snapshot que
identificou com o ponteiro final realmente consumido. Na rodada completa houve
15 pares exatos e uma divergência: a tradução esperada ganhou `\n` antes de
`apontar!`; trata-se da quebra automática de layout, não de uma frase trocada.
O log conserva identificado, esperado e consumido para distinguir os casos.

Para eliminar a calibração visual de Bird, F5 passou a gravar no próprio
`f5_NNN.txt` os três bancos `SpriteObj` apontados por `801A8C18/24/30`, quatro
slots por banco, incluindo flags, recurso, `x/y/z` e rotação. A próxima ação é
posicionar Bird sobre o computador e apertar F5; essa coordenada alimentará o
roteiro dirigido, que então poderá confirmar Comprar e navegar à direita por
mais de dez segundos sem depender de aproximações.

---

# 39. Bookmark v2 por retrace e requisitos do save state real

Foram estudados `tools/oot-dx` e o `psxrecomp-v4` trazido pelo
MegaManX4Recomp. O primeiro não possui snapshot arbitrário: oferece avanço de
quadro, map select, noclip e edição de estado no nível do jogo. O segundo tem
um serializador completo, versionado e aplicado em block leader seguro, mas
recusa expressamente o carregamento no modo de host fibers.

Essa restrição coincide com nosso runtime: cada `OSThread` possui
`recomp_context`, porém sua continuação real vive na pilha C da fiber. Um save
state verdadeiro exige primeiro um escalonador sem fibers, com `resume_pc` e
continuações reentrantes; restaurar apenas RDRAM voltaria a criar as travas já
observadas.

A causa concreta da variação do F4 atual era anterior a essa reforma maior: o
bookmark v1 associava entradas e alvo à quantidade de leituras do PIF, que
varia conforme o escalonamento. Novos F2 geram `WPJ2_BOOKMARK_2`, com mudanças
de controle e alvo vinculados ao retrace emulado. O leitor continua aceitando
v1. Build completo passou; um smoke test carregou três transições e desligou
o turbo exatamente no retrace 4. A análise e a sequência completa do futuro
save state estão em `analise/projeto/checkpoints_estado.md`.

Para validar, grave um novo F2 numa cena distante e use F4 três vezes. O
`quick.replay` antigo só passa a v2 depois desse novo F2.

---

# 40. Correção do F4 que apenas reiniciava

O log da execução interativa mostrou que o F4 carregava corretamente o
`quick.replay` v2 e anunciava o alvo 8164, mas chegava a ele antes da primeira
consulta ao PIF. A aceleração ilimitada do VI mantinha a thread de retrace
sempre pronta e impedia as threads de menor prioridade de inicializar e
consumir o roteiro. Assim, o contador era correto e o estado do jogo era o de
um boot comum.

O retorno por retrace passou a usar 8x cadenciado (480 Hz), incluindo o áudio
em modo acelerado. O portão temporal continua existindo, de modo que controle,
lógica e tarefas RSP avançam junto com o retrace. Ao alcançar o alvo, vídeo e
áudio voltam automaticamente à velocidade normal.

Validação headless com o bookmark real:

- alvo: retrace 8164, 161 transições;
- as leituras do controle e as transições ocorreram antes do alvo;
- estado salvo no F2: `1/1`;
- estado após o retorno e no retrace 8467: `1/1`;
- build completo de `wpj2_probe.exe` aprovado.

Também foi corrigido o metadado auxiliar `quick.txt`, que agora declara
`WPJ2_BOOKMARK_2`. Um novo F2 substitui o arquivo antigo automaticamente.

---

# 41. Início do save state real por continuations

O replay foi classificado como solução temporária e não será ampliado. O
Project64 consegue restaurar instantaneamente porque CPU, PC e periféricos são
estruturas explícitas; no runtime atual de WPJ2, a continuação de cada thread
vive numa fiber Win32 não serializável.

Foi escolhida uma pilha portátil de frames `{function_vram, callsite_vram}`.
Ao ceder, a pilha C é abandonada; no próximo despacho, as funções saltam pelos
callsites gravados e reconstroem a cadeia. O núcleo inicial está em
`runtime/continuation.c` e passou um teste de roundtrip com três chamadas
aninhadas, incluindo uma operação anterior à pausa que não pode ser repetida.

O inventário encontrou 8.274 chamadas diretas, 82 indiretas, 8.353 rótulos
`after_N` e 10 pausas diretas em 37 fontes gerados. O mapa completo, as seções
do futuro arquivo e a ordem de integração estão em
`analise/projeto/savestate_runtime.md`.

Na etapa seguinte, `src/scripts/injetar_continuacoes.py` transformou as 3.651
funções reais. Todas as 8.356 chamadas foram associadas à instrução MIPS que
as originou e ganharam push/retomada/propagação/pop. Os 37 arquivos stateful
compilaram com sucesso no MSVC. Essa variante ainda não é ligada ao executável:
o próximo corte é fazer polls e pausas retornarem ao primeiro dispatcher sem
fibers.

---

# 42. Save state real concluído e integrado ao F2/F4

O escalonador por fibers foi substituído no build principal pelo dispatcher
stateful. As 3.651 funções recompiladas recebem continuations geradas; cada
frame conserva função, callsite, temporários C e o alvo de chamadas indiretas.
Polls e preempções abandonam a pilha hospedada e a reconstroem apenas a partir
dos dados serializáveis da OSThread.

O arquivo `sav/bookmarks/quick.wpstate` (formato interno v4) grava 8 MiB de
RDRAM, até 32 threads/contextos/continuations e os estados lógicos de HLE, RSP,
PIF e áudio. Há magic, versão, tamanhos, validação de cada thread e hash FNV-1a
do payload. A carga valida tudo antes de alterar o mundo vivo. Buffers do host
e o swapchain RT64 não são serializados: áudio e RT64 são encerrados e
recriados a partir do estado lógico restaurado.

Dois defeitos estruturais apareceram e foram corrigidos durante a integração:

- ao consumir a última frame antiga, a continuação ainda se declarava em
  reconstrução e confundia a primeira chamada nova com uma frame persistida;
- alvos `JALR` e temporários `hi/lo/result/c1cs` viviam em variáveis C locais e
  eram sobrescritos por chamadas profundas; agora fazem parte da frame.

Validação final:

- testes unitários de continuation e thread por arquivo: OK;
- execução ativa por 10 s: 511 funções e 479.601 chamadas, sem alvo inválido;
- snapshot de 9.726.968 bytes gravado e restaurado durante a abertura;
- o arquivo foi carregado em um processo novo e continuou a execução;
- três comandos F4 consecutivos restauraram no mesmo processo sem trava;
- vídeo RT64 e áudio continuaram ativos; a ponte RT64 foi recriada após cada
  carga, sem fechar a janela e sem cair no rasterizador CPU.

`tools/build_probe.cmd` agora gera e liga `RecompiledFuncsStateful` com
`sched_stateful.c`. O replay antigo permanece somente nos perfis especializados
de stress. A pendência F2/F4 foi removida de `PENDENCIAS.md`.

---

# 43. Estabilização da abertura e do snapshot — contexto completo v6

A primeira integração do dispatcher stateful ainda tinha uma regressão real:
às vezes congelava depois das logos e, em outras execuções, durante o corredor
3D. Foram isoladas e corrigidas três causas estruturais:

- eventos de VI/RSP cediam a CPU mesmo quando só havia threads de prioridade
  inferior prontas. Agora a preempção assíncrona respeita a prioridade da
  libultra; a abertura voltou de 154 listas gráficas em 180 s para mais de
  50 listas por segundo;
- overrides nativos podiam ser interrompidos enquanto mantinham uma pilha C
  não serializável. A preempção é adiada nessa fronteira e pontos bloqueantes
  explícitos continuam funcionando;
- cada continuation guardava o callsite e o SP, mas não os registradores usados
  para montar os argumentos do `JAL`. O formato v6 agora conserva GPRs, FPRs,
  HI/LO e estado de FPU em cada frame, restaurando exatamente o contexto do pai
  antes de reconstruir a chamada.

O microcódigo RSP também passou a limitar e quebrar transferências DMA dentro
da memória física do RSP, evitando asserts do host sem alterar a semântica de
wrap do hardware.

Validação final desta revisão:

- abertura automática por 120 s: 6.142 listas gráficas, 3.527 listas de áudio,
  9.279.159 chamadas e nenhuma exceção/fault/assert;
- WAV contínuo de 117,9 s: DC -25,8, RMS 5.081, pico 32.161 e zero saturações;
- F2 aos 38 s no corredor e dois F4 na mesma janela: ambos restauraram `12/50`
  e a execução continuou até 85 s;
- `quick.wpstate` v6 com 10.808.312 bytes carregado por um processo novo, que
  produziu mais 1.727 listas gráficas e 1.036 listas de áudio em 35 s;
- testes unitários: `continuation snapshot roundtrip: OK` e
  `stateful thread file restart: OK`.

Snapshots anteriores ao v6 são recusados deliberadamente; um novo F2 os
substitui de forma atômica.
# 44. Nove slots de estado e diagnóstico de cadência — 29/08/2026

O snapshot v6 deixou de usar apenas `quick.wpstate`. `Ctrl+Shift+1..9` grava
`sav/bookmarks/slotN.wpstate` e `Ctrl+1..9` restaura o número correspondente,
sem fechar a janela. Gravações são atômicas e substituem somente o slot
escolhido. Dois slots distintos foram gravados e restaurados na mesma execução;
os testes unitários de continuation/thread também passaram.
Uma notificação sobreposta ao RT64 confirma por 1,8 segundo `Slot N salvo` ou
`Slot N carregado`; falhas usam o mesmo aviso em vermelho.

A sensação de desempenho não vinha de um relógio abaixo de 60 Hz: foram
medidos 1.200 VIs em 20 s. `hle_deliver_events`, porém, devolvia o mesmo valor
para VI e para conclusões antecipadas de RSP/SI, fazendo o RT64 apresentar a
65–77 Hz com intervalos irregulares. Separar os eventos revelou que o scheduler
ocioso não apresentava seus VIs; a forma final moveu a apresentação para o
próprio pulso VI. Assim todos os caminhos apresentam uma vez por retrace.

O tracing detalhado por função também foi retirado do perfil normal e tornou-se
opt-in por `WPJ2_TRACE_DETAIL=1`. As métricas completas estão em
`analise/projeto/performance_cadencia.md`. Ainda existem picos ocasionais acima
de 33 ms ligados ao trabalho cooperativo de CPU/RSP e parcialmente à fila de
áudio, mas o RT64 em si mede apenas 0,032 ms por apresentação em média.

---

# 45. Perfil normal enxuto e estabilidade após reinício — 29/08/2026

Depois de reiniciar o computador, o usuário repetiu o corredor e informou que
o engasgo visual aparentemente desapareceu. Uma nova execução também foi vista
como praticamente fluida. Como isso sugere contenção externa/driver, a ordem
síncrona SP/DP validada foi preservada.

A revisão eliminou trabalho diagnóstico que ainda ocorria durante o jogo:

- o perfil padrão não grava mais WAV; a reprodução WinMM continua ativa;
- logs contínuos de tradução ficaram restritos a `TESTAR.bat legendas`;
- 42 `printf` residuais de antigas sondas de carregamento foram desativados no
  build normal;
- `RecompiledFuncsStateful` passou a ser compilado com `/O2`.

O slot 1 foi executado por 45 s com áudio, tradução e RT64/Vulkan: sem trava,
60,000 apresentações/s nas métricas internas, nenhum WAV indevido e nenhum
novo atraso acima de 33 ms depois do aquecimento inicial. Os testes unitários
das continuations e da restauração de OSThread continuam OK. A medição externa
por `gdigrab` foi rejeitada porque não lê corretamente a swapchain Vulkan; não
se confunde mais submissão interna com fluidez visual comprovada.

---

# 46. Cadência visual do menu e interpolação RT64 — 29/08/2026

O menu salvo no slot 2 foi isolado. Uma execução PT-BR e outra sem tradução
tiveram comportamento praticamente idêntico, descartando o catálogo como
gargalo principal. A apresentação da janela ocorre em 60 Hz, mas o jogo produz
novas tarefas gráficas nesse menu em intervalos de aproximadamente 33 e 50 ms;
por isso repetir o último framebuffer a 60 Hz ainda parece engasgado.

A ponte RT64 usava `RefreshRate::Original`, apesar de o backend de referência
já oferecer interpolação nativa de frames, transformações e tiles. O padrão foi
alterado para `RefreshRate::Display`, preservando a cadência lógica original e
interpolando somente a apresentação até a taxa do monitor. O modo antigo pode
ser comparado com `WPJ2_RT64_REFRESH=original`.

O teste automático restaurou explicitamente `slot2.wpstate`, movimentou o
cursor nos quatro sentidos durante 15 s e terminou sem trava. Houve uma rodada
anterior inválida no slot 1: `WPJ2_STATEFUL_LOAD_ON_START=1` também seleciona o
slot 1; ela foi descartada e a repetição correta usou o valor `2`. A avaliação
visual do usuário ainda é necessária, pois métricas internas de apresentação
não provam a fluidez percebida na swapchain Vulkan.

O usuário confirmou que o cursor permaneceu engasgado. A instrumentação interna
do RT64 mostrou `swap=60 target=60 original=0`: a interpolação não chegou a ser
usada, pois o backend exige uma taxa original válida. O padrão foi restaurado
para `RefreshRate::Original`.

A corrida revelou ainda que o perfil normal continuava com `WPJ2_DEBUG` ligado
por padrão na build de sondagem. Isso fazia uma segunda interpretação completa
de cada display list para estatísticas, tracing/console contínuo e exportações
automáticas. `TESTAR.bat` agora força `WPJ2_DEBUG=0` nos perfis `padrao` e
`sem_legendas`; apenas os perfis de análise reativam a telemetria.

---

# 47. Interpolação RT64 efetiva e fila de áudio do menu — 29/08/2026

A taxa nativa não chegava à ponte RT64: mudar somente para `Display` mantinha
`original=0` e nenhum quadro era interpolado. A ponte agora injeta 30 fps antes
de cada display list. A sonda confirmou `original=30`, `target=60` e dois
quadros por imagem, portanto `TESTAR.bat` voltou a usar `Display` como padrão.

Áudio, VI e GFX foram registrados na mesma timeline. O dispositivo de áudio
esvaziava durante as rajadas de produção, confirmando que o sintoma sonoro não
era impressão visual. Foi acrescentado pré-buffer configurável, correção linear
limitada por profundidade e reconstituição da reserva após underflow. Seis
blocos reduziram os esvaziamentos medidos de cinco para três em 30 s; isso fica
documentado como mitigação, não como correção completa.

O caminho release também deixou de imprimir/forçar logs em disco a cada tecla
e leitura do PIF. `WPJ2_DEBUG=1` continua oferecendo a telemetria completa nos
perfis especializados. A hipótese de transformar o pendente da AI num contador
de dois slots piorou a cadência no comparativo e foi revertida.

---

# 48. Correção estrutural da cadência — 29/08/2026

A conclusão do item 47 sobre 30 fps foi substituída por medição do registrador
real: `VI_ORIGIN` alterna a 60,00 Hz nos slots 1, 2 e 3. A ponte RT64 agora
informa 60 Hz; contar display lists confundia composição com frame apresentado.

O defeito central era `hle_deliver_events` dormir também dentro de uma thread
executável. Só o dispatcher ocioso pode esperar pelo próximo VI; `RECOMP_POLL`
agora consulta o prazo sem bloquear. GFX passou de 51,97/25,31/21,24 tarefas por
segundo nos slots 1/2/3 para 60,00/48,78/51,77, enquanto o áudio estabilizou em
aproximadamente 30 blocos/s nos três.

Depois disso, a compensação adaptativa de áudio tornou-se contraproducente:
enchia os oito slots e fazia a thread esperar 5--12 ms. Ela foi removida. A
saída final usa pré-buffer de quatro blocos, doze slots e espera zero. No slot 3
foram medidos VI 60,04 Hz, GFX 60,04/s, `VI_ORIGIN` 59,93 Hz, áudio 29,96/s,
zero underflows, zero descartes e zero esperas. Os testes de continuation e
restauração de OSThread permanecem aprovados.
