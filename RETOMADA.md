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
