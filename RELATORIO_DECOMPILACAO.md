# Estado real — Wonder Project J2

Ultima verificacao: **2026-08-09** (sexta rodada)
Escopo medido: **port nativo por recompilacao estatica**, nao uma decompilacao
que reconstroi a ROM byte a byte.

Este e o documento canonico de status. `decompilation_report.md` e um resumo
antigo mantido so por historico.

## Entrada verificada

| Campo | Valor |
|---|---|
| ROM | `Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64` |
| Tamanho | 8.912.896 bytes (8,5 MiB) |
| Titulo interno | `WONDER PROJECT J2` — codigo `NJ2J`, versao 0 |
| Entry point | `0x80000400` |
| CRC1 / CRC2 | `4F1E88F7` / `4A5A3F96` |
| SHA-256 | `CF0E08C8349291EE5561BA03FA82287A505ACECE1644588001937B0ABC732D31` |

O runtime confere o CRC1 na carga e avisa se a imagem nao for esta build.

## Onde o boot chega hoje (sexta rodada)

Dois erros de uma linha cada, e o jogo saiu do lugar.

| Medida | Quarta | Quinta | Sexta |
|---|---:|---:|---:|
| Funcoes distintas | 112 | 103 | **259** |
| Chamadas totais | 11.122 | 14.088 | **34.649** |
| Threads criadas | 6 | 6 | **7** |
| DMA do cartucho | 0 | 0 | **22 (70.762 bytes)** |
| Comandos de PIF | — | 4 | **8** |
| Tarefas de RSP | 0 | 0 | **1** |

O jogo carrega dados do cartucho, responde ao protocolo do controle, submete uma
tarefa de RSP e a ve concluir. Nenhuma falha de acesso. Ao fim dos 20 s todas as
threads vivas estao em espera, cada uma numa fila do proprio jogo.

## Como estava na quinta rodada

O jogo entra no proprio laco e para num impasse estavel.

- **14.088 chamadas** (quarta rodada: 11.122), +27%.
- **103 funcoes distintas**, contra 112 na rodada anterior. A queda e artefato,
  nao regressao: tres funcoes de libultra passaram a ser nativas e deixaram de
  ser contadas junto com as que elas chamavam. Chamadas totais e a medida mais
  fiel entre rodadas em que o numero de substituicoes muda.
- **O PIF responde**: uma troca completa, quatro comandos joybus processados.
  O jogo enxerga um controle padrao no canal 1, sem pak e sem botao pressionado.
- Impasse: a trilha das ultimas 32 funcoes e **identica aos 15 s e aos 20 s**.
  A thread de tarefas (`0x8009756C`, pri 64) cicla entre `osSendMesg` e
  `osRecvMesg` e espera conclusao de tarefa de RSP que ninguem submete.

## Como estava na quarta rodada

O jogo passa da inicializacao do SO e entra nos proprios subsistemas.

- **6 threads**, quatro delas do jogo: `0x800BD76C` (boot, pri 10 -> 0),
  `0x80000450` (pri 10), **`0x800ADC50` (pri 53)** e **`0x8009756C` (pri 64)`.
  As duas ultimas so nascem depois que os temporizadores passam a disparar.
- **112 funcoes executadas**, 11.122 chamadas (terceira rodada: 69 e 7.971).
- **AI tocado**: o jogo configura frequencia de audio via `osAiSetFrequency`.
- Estado final: o gerenciador de VI cicla a 60 Hz, o de PI espera comando, a
  thread pri 53 espera controle (fila do SI) e a pri 64 espera conclusao de
  tarefa de RSP/RDP (fila `0x80153E90`).

Como o avanco foi obtido: a trilha das ultimas funcoes antes de uma thread
desistir apontou `func_800D54F0` — `__osSetCompare`, um dos stubs. Todo
`osSetTimer` do jogo armava um alarme que nunca tocava. Entregar o evento
COUNTER destravou a cadeia; entregar o SI destravou a seguinte, e foi ai que as
duas threads de jogo restantes nasceram.

## Como estava na terceira rodada

O SO sobe inteiro e a maquina entra num **laco de retrace estavel**:

- **4 threads**, todas com identidade clara pela prioridade que a libultra usa:
  jogo (id 3, pri 10), **gerenciador de PI** (id 0, pri 150 = `OS_PRIORITY_PIMGR`,
  entrada `0x800D5140`), jogo (id 10, pri 10, entrada `0x80000450`) e
  **gerenciador de VI** (id 0, pri 254 = `OS_PRIORITY_VIMGR`, entrada `0x800C4468`).
- **7 eventos registrados** em `__osEventStateTab` (`0x801AFA80`): COUNTER, SP,
  SI, VI, PI, DP e PRENMI.
- **638 retraces entregues** em 20 s (~32 Hz, limitado pela granularidade do
  `Sleep`), 646 despachos, 650 trocas de contexto.
- **`__osViSwapContext` chamado 640 vezes** — uma por retrace. O gerenciador de
  VI acorda, troca o buffer e volta a dormir, como num console ocioso.
- 69 funcoes executadas, 7.971 chamadas. MI, VI, PI e SI tocados.

Isso e um N64 rodando o proprio sistema operacional: threads sendo escalonadas
por prioridade, interrupcoes chegando a 60 Hz, o gerenciador de video
apresentando um frame por vez. Falta o renderizador — nao ha imagem, so a troca
de buffer sendo pedida.

## Historico: onde o boot parava antes

`wpj2_probe.exe` monta 8 MB de RDRAM com KSEG0 e KSEG1 como aliases reais do
mesmo mapeamento, carrega o cartucho em `0xB0000000`, replica a copia de boot e
as globais do IPL3, e chama `recomp_entrypoint`. Resultado atual
(`probe_run.log`):

- **42 funcoes executadas**, 218 chamadas.
- **6 despachos de thread, 5 trocas de contexto**, tres threads criadas:
  id 3 em `0x800BD76C`, id 0 em `0x800D5140`, id 10 em `0x80000450`.
- Duas threads chegaram ao fim e cairam corretamente em `__osCleanupThread`.
- Hardware tocado: MI, PI e SI.
- Fim: a fila de execucao fica so com a sentinela e a maquina entra no laco
  ocioso, com `__osRestoreInt` como ultima funcao em todas as quatro amostras do
  watchdog.

Esse desfecho e o **comportamento correto** de um N64 sem interrupcoes: o SO
termina a inicializacao, nao tem mais nada pronto para rodar e fica girando a
espera de um retrace que ninguem entrega. O proximo desbloqueio nao e mais
analise estatica; e a entrega de interrupcoes.

## O que mudou na decima primeira rodada

O processo estava serial: uma build, uma corrida de 20 s, uma analise, repete.
Cada hipotese custava um ciclo inteiro, e testar seis custava dois minutos de
relogio. Trocado por um harness paralelo.

**A sondagem virou configuravel por ambiente** — `WPJ2_TIMEOUT`, `WPJ2_MEMSIZE`,
`WPJ2_EVENTS` (mascara de bits, um por evento), `WPJ2_BUTTONS` e `WPJ2_OUT`.
O ultimo e o que torna o paralelismo possivel: cada instancia grava o proprio
`executadas.txt` e nao pisa na das outras.

**`tools/sweep.py`** dispara N configuracoes simultaneas, coleta as metricas de
cada log e imprime a tabela comparativa mais o diff de cobertura contra a base.
Seis corridas de 20 s: **20,1 s de relogio** em vez de 120 s.

Quatro respostas de uma tacada:

| Corrida | Funcoes | Chamadas | O que responde |
|---|---:|---:|---|
| base | 342 | 480.197 | — |
| sem_si | 71 | 10.053 | SI e indispensavel |
| sem_counter | 68 | 7.407 | COUNTER e indispensavel |
| sem_pi | 121 | 27.247 | PI e indispensavel |
| sem_sp | 276 | 57.204 | SP e indispensavel |
| mem8 | 342 | 480.109 | **pak de expansao e irrelevante** — cobertura identica |

E o segundo eixo, entrada e tempo:

| Corrida | Funcoes | Chamadas | Diff vs base |
|---|---:|---:|---|
| start | 322 | 921.895 | **+32 funcoes que a base nunca alcanca** |
| botao_a / botao_b | 342 | 480.197 | identico a base |
| longo_60 | 342 | 935.899 | cobertura identica, so mais do mesmo |
| longo_120 | 342 | 1.618.815 | cobertura identica |

**O jogo responde a START.** As 32 funcoes novas incluem `func_80002824`,
`func_8001635C` e `func_80016074` — que estavam justamente no topo da lista de
fronteira da rodada anterior. A/B nao mudam nada, e tempo tambem nao: 120 s
alcancam exatamente o mesmo conjunto que 20 s.

Isso reposiciona a pergunta. O jogo nao esta travado esperando hardware; esta
num estado que **avanca com entrada**. Ainda assim, nenhuma tarefa grafica: com
START foram 333 tarefas, todas do tipo 2.

## O que mudou na decima rodada

`tools/callee_status.py`: para uma funcao dada, lista cada `jal` do corpo na
ordem em que aparece e marca quais destinos executaram. A ideia era distinguir
"sequencia de inicializacao que parou no meio" de "despachante que so entrou em
alguns casos".

`func_80000C90` tem **123 chamadas** no corpo, 66 marcadas como alcancadas,
espalhadas. Parecia despachante.

**Nao e — e um falso positivo da minha ferramenta, e vale registrar o tipo.** O
traco e por *funcao*, nao por *local de chamada*: um destino aparece como
alcancado se executou em qualquer lugar do programa. `func_800B1B04` aparece
"ok" com 7 execucoes, mas o desassemblador mostra que a chamada dela em
`0x80000EEC` esta num bloco em linha reta que termina em `jal func_800B1F0C` sem
nenhum desvio entre as duas — e `func_800B1F0C` nunca executou. Se aquele bloco
tivesse rodado, as duas teriam rodado. As 7 execucoes vieram de outro chamador.

Trechos em linha reta alternando "ok" e "--" sao a assinatura desse erro.
Distinguir de verdade exige um contador por instrucao `jal`; ate la a saida vale
como pista, nao como conclusao. A limitacao esta escrita no cabecalho da propria
ferramenta, para nao enganar quem a usar depois.

## O que mudou na nona rodada

Rodada de instrumentacao: nenhuma mudanca de comportamento, uma imagem bem mais
nitida de onde o jogo esta.

**Analise de fronteira** (`executadas.txt` + `tools/frontier.py`). O runtime passa
a gravar toda funcao que executou, com a contagem. Cruzando com o callgraph sai
o inverso util do traco: em vez de "por onde passou", **"o que estava prestes a
acontecer e nao aconteceu"**. Das 3.651 funcoes, 342 executam e **143 estao a
exatamente uma chamada de distancia** do que ja roda.

As substituicoes nativas sao excluidas da conta: o corpo recompilado delas foi
renomeado, entao nunca aparecem como executadas e poluiriam a lista com funcoes
que na verdade rodam o tempo todo.

**O gerenciador de tarefas esta saudavel.** Os tres caminhos de submissao rodam
(`func_8009769C` 636x, `func_80097988` 324x, `func_80097B0C` 30x) e
`func_80097DDC` submete 325 vezes — exatamente o numero de tarefas. O jogo nao
deixa de submeter graficos por falha no caminho; ele simplesmente nunca *monta*
uma tarefa grafica.

**Um alarme falso, registrado como tal.** A fronteira destacou `func_800BA9D4`:
295 chamadas, **zero** dos sete destinos alcancados. Ela le um estado em
`0x800E8CF0` e so age se o valor for 2 ou 4; o watch mostrou `1` nas quatro
amostras. Parecia um travamento — mas `tools/xref_addr.py` mostrou que quem
escreve esse endereco esta na familia `0x800B9xxx`/`0x800BBxxx`, que chama as
rotinas de audio, e os destinos nao alcancados sao decodificadores de amostra.
**E o tocador de sequencia parado porque nao ha musica tocando.** Nao e defeito.

O que sobra como pergunta real: `func_80000C90` — o init principal do jogo —
rodou **uma vez** e deixou **36 dos 62 destinos** sem alcancar.

## O que mudou na oitava rodada

**O jogo entrou num laco estavel de audio.** Nenhuma falha em 20 s.

| Medida | Setima | **Oitava** |
|---|---:|---:|
| Funcoes distintas | 258 | **342** |
| Chamadas totais | 31.643 | **479.559** |
| Tarefas de RSP | 1 | **325** |
| Comandos de audio executados | 0 | **116.408** |
| Bytes gravados pelo audio | 0 | **5.950.336** |
| Threads mortas por falha | 1 | **0** |

Duas mudancas, e a segunda e a que importa.

1. **Interpretador minimo da ABI de audio** (`run_acmd_list`). Executa so os
   quatro comandos que movem dados — `CLEARBUFF`, `SETBUFF`, `LOADBUFF`,
   `SAVEBUFF` — e ignora `ADPCM`, `ENVMIXER`, `RESAMPLE`, `MIXER`, `POLEF` e
   `INTERLEAVE`, que e onde moraria a sintese. O efeito e **silencio, nao lixo**:
   a DMEM fica zerada onde a sintese escreveria, e o `SAVEBUFF` leva esse zero
   para os buffers que o jogo le.

   Sozinho, isto **nao mudou nada**: mesma falha, mesmo endereco, mesma
   cobertura. A hipotese de que a thread morria por falta da saida de audio
   estava errada.

2. **SP e DP dividem a mesma fila neste jogo** (`0x80153E90`, junto com PRENMI).
   Eu postava os dois por tarefa concluida, o que entrega *duas* mensagens de
   conclusao para *uma* conclusao. `func_80097B0C` le a tarefa pendente em
   `+0x298`, **zera o campo** e so entao a desreferencia; na segunda mensagem o
   campo ja e nulo, e ela morre em `+0x101`.

   A leitura veio do desassemblador, nao de tentativa: o `sw $zero, 0x298($t8)`
   entre a leitura e o uso e a assinatura de "consumir a tarefa pendente". Uma
   tarefa de audio nao usa o RDP e nao deve gerar DP.

Corrigido isso, as tarefas passaram de 1 para 325 e as chamadas de 31 mil para
479 mil. As sete threads terminam a execucao vivas e ciclando.

## O que mudou na setima rodada

**A OSTask ficou visivel, e ela desmente a hipotese anterior.**

`__osSpRawStartDma` (`func_800CD060`) foi substituida e agora mantem os 8 KB de
DMEM/IMEM do RSP no runtime. A libultra deposita a tarefa na DMEM em `0x0FC0`
antes de soltar o RSP, entao interceptar essa copia mostra a tarefa inteira:

```
tipo=2 audio (M_AUDTASK)  flags=0x0
ucode  boot=0x800D6B70 (208 B)   data=0x800F1620 (2048 B)
lista=0x8025ACE0 (3696 B)        saida=0x00000000
```

**A primeira tarefa nao e grafica, e de audio.** A rodada anterior concluiu que o
proximo bloqueio ficava "na fronteira do renderizador"; estava errado. Quem morre
logo depois e a thread de tarefas, e o que ela espera e o resultado de um
microcodigo de *audio* que nao rodou.

De brinde, uma confirmacao do mapa: `ucode_boot = 0x800D6B70` e exatamente o fim
do segmento boot (`0x1000 + 0xD6770`). O microcodigo do RSP comeca onde o codigo
de CPU termina, o que valida o limite da secao.

**E um erro meu, com efeito pior que nao fazer nada.** Li o desvio de
`func_800CD060` ignorando o delay slot e inverti a direcao do DMA. Como a
primeira coisa que o jogo faz e mandar a OSTask para a DMEM, a copia invertida
gravava zeros *por cima da tarefa dele*. O `ucode_boot = 0` que apareceu no
primeiro log era obra minha. Uma substituicao nativa errada nao e neutra: ela
corrompe o estado do jogo e produz sintomas que parecem do jogo.

## O que mudou na sexta rodada

Duas causas, ambas do runtime, ambas de uma linha.

1. **`ctx->f_odd` nunca era inicializado.** Sem o modo de ponto flutuante do
   MIPS3, um registrador impar como `$f13` e a metade alta do par `$f12/$f13`, e
   o codigo gerado o alcanca por `ctx->f_odd[(13-1)*2]`. Esse campo e um ponteiro
   que **o runtime** tem de apontar para a metade alta de `$f0`. Eu zerava o
   contexto e parava ai. Resultado: toda escrita num registrador impar virava
   escrita em `NULL + (n-1)*8`.

   O endereco da falha era `0x60`, que e exatamente `(13-1)*2*4`. A pista veio de
   reconstruir o registrador a partir do endereco: `0xFFFFFD97EA750062`, cuja
   metade alta nao e `0xFFFFFFFF` — logo nao era um endereco do N64 com sinal
   estendido, e portanto o problema nao estava nos dados do jogo.

   Uma linha (`ctx_init`) levou a cobertura de 103 para 206 funcoes e fez o
   primeiro DMA de cartucho acontecer.

2. **`SP_STATUS` e registrador de comando na escrita.** Na leitura ele reporta
   estado; na escrita cada bit manda *por* ou *tirar* outro bit. Com a MMIO sendo
   memoria crua, `__osSpSetStatus(0x2B00)` gravava `0x2B00` por cima do estado e
   apagava o bit 0, "RSP parado". Dali em diante `__osSpSetPc` devolvia `-1` para
   sempre, e `osSpTaskLoad` girava num laco de nova tentativa: **3.587.273
   chamadas em 20 segundos sem sair do lugar**.

   `runtime/rsp.c` mantem o estado a parte e aplica a semantica correta. E como
   nao ha microcodigo, uma tarefa iniciada termina no mesmo instante — o bit de
   parado volta, `broke` e posto, e fica pendente uma interrupcao de SP.

3. **Tres identidades corrigidas.** `func_800CC98C` nao e `__osGetGlobalIntMask`
   e sim **`__osEnqueueAndYield`**: ela recebe a fila em `$a0` e e chamada logo
   depois de a thread ser marcada como ESPERANDO. Le `MI_INTR_MASK` porque salva
   a mascara no contexto — foi essa leitura que me fez errar antes. E o par
   send/recv estava trocado: `func_800C4AA0` bloqueia quando `validCount ==
   msgCount`, logo e **`osSendMesg`**; `func_800C4C40` bloqueia quando
   `validCount == 0`, logo e **`osRecvMesg`**.

4. **Falhas passaram a ser legiveis.** O acesso gerado e
   `rdram + ((reg + off) - 0xFFFFFFFF80000000)`; invertendo a conta a partir do
   endereco que o Windows reporta, sai o proprio `reg + off`. Sem isso um
   ponteiro nulo do jogo aparecia no log como um endereco de host gigante "fora
   do mapa", que nao sugere nada. Agora sai "ao seguir um ponteiro nulo
   (offset +0x4)".

5. **SP e DP entregues, agora com condicao.** Na quarta rodada eu tinha testado
   entregar conclusao de tarefa a cada quadro e o resultado piorou: a thread que
   esperava o quadro morria lendo o resultado de trabalho que nunca aconteceu.
   A diferenca agora e a condicao — so se anuncia o fim de uma tarefa que alguem
   de fato iniciou.

## O que mudou na quinta rodada

1. **PIF minimo (`runtime/pif.c`).** A interface saiu de `func_800CD4F0`
   (`__osSiRawStartDma`): `$a0` e a direcao, `$a1` o endereco na RDRAM, e o
   tamanho e sempre 0x40 porque a propria funcao invalida exatamente 64 bytes de
   cache. O bloco e uma fita de comandos joybus; o runtime a percorre e escreve
   as respostas. Isto nao e emulacao de controle: e a *ausencia de entrada*
   dita de forma que o jogo entenda. Antes ele lia zeros crus, que nao sao uma
   resposta valida do protocolo, e decidia com dado invalido.

2. **Valores de ligar do RCP.** Uma pagina de MMIO recem-comprometida le zero, e
   zero nao e o estado de repouso de todo registrador. `__osSpSetPc`
   (`func_800CD020`) le `SP_STATUS`, exige o bit 0 — "RSP parado" — e devolve
   `-1` se ele estiver limpo. Com `SP_STATUS` lendo zero, **nenhuma tarefa de
   RSP podia ser carregada**. Corrigido, mas o caminho ainda nao e alcancado:
   e uma barreira removida antes de chegar nela, nao um avanco observado.

3. **Trilha periodica no watchdog.** A mesma trilha que resolveu o caso do
   temporizador agora e amostrada a cada quarto do tempo limite. Foi ela que
   mostrou que o impasse atual e *estavel*: trilha identica aos 15 s e aos 20 s
   significa que nenhuma funcao recompilada roda ha cinco segundos.

## O que mudou na quarta rodada

1. **Trilha de execucao.** Um anel com as ultimas 32 funcoes alcancadas. Um
   contador por funcao diz *quanto*; so a ordem diz *onde* uma thread desistiu.
   Foi ela que apontou `__osSetCompare` como causa do bloqueio.

2. **Eventos COUNTER e SI entregues.** COUNTER e o que a libultra usa para
   temporizadores, e nasce de uma comparacao com o registrador `Compare` do
   COP0 — que e stub. SI e a leitura de controles, que no console termina uma
   vez por quadro. Os dados lidos continuam zerados: nao ha PIF, entao para o
   jogo e como nao haver controle conectado.

3. **SP e DP deliberadamente fora.** Foram testados: destravam a thread que
   espera o quadro, mas ela morre logo depois, lendo o resultado de uma tarefa
   que nunca rodou. A cobertura caiu de 112 para 109 funcoes. Anunciar
   "terminou" para trabalho que nao aconteceu troca um bloqueio visivel por uma
   corrupcao silenciosa.

4. **Protecao de falha por fiber.** O `__try` do `main` so cobre a pilha do
   fiber principal; uma falha dentro de uma thread do jogo matava o processo em
   silencio, sem relatorio. Aparecia como log truncado logo apos o audio
   inicializar. Cada fiber agora tem o proprio manipulador, e a pilha subiu de
   256 KB para 1 MB.

5. **`__osEnqueueThread` e `__osEPiRawStartDma` substituidas.** A primeira nao
   por necessidade, e sim por informacao: e o unico ponto onde se sabe *em qual
   fila* uma thread esta sendo estacionada. A segunda porque e o caminho que o
   gerenciador de PI usa, e sem ela o gerenciador anunciava transferencia
   concluida com o buffer de destino intacto.

6. **Semantica de `osYieldThread`.** Uma thread que chama o despachante ainda
   com estado RODANDO nao foi colocada em fila nenhuma por quem chamou; ela
   continua executavel. Sem devolve-la a fila de execucao ela sumia — foi o que
   aconteceu com a thread de boot.

## O que mudou na terceira rodada

1. **Terceiro defeito de limite de funcao — e o mais caro.** O entrypoint termina
   em `0x80000430` com `jr $t2`, e ninguem chama `0x80000450` por `jal`: ela e
   entrada de thread, entregue a `osCreateThread` por ponteiro. Nenhuma das duas
   heuristicas do gerador se aplicava, entao as duas funcoes viraram uma so de
   `0x890` bytes. Em execucao isso aparecia como uma thread nascendo apontando
   para o meio do entrypoint e nao executando nada. Corrigido, o boot passou de
   42 para 69 funcoes e de 218 para quase 8.000 chamadas.

2. **Fiber de scheduler como ponto fixo.** A primeira versao devolvia o controle
   "a quem despachou". Quando essa thread ja tinha terminado, o controle voltava
   para um fiber morto — e o retorno da rotina de um fiber encerra a *thread do
   sistema*. O processo continuava vivo so por causa do watchdog, sem executar
   nada, com `polls` congelado em 24. Um fiber de escalonamento que nunca termina
   elimina a classe inteira de erro.

3. **Pontos de interrupcao em rotulos.** O laco ocioso da libultra e `for(;;)`
   puro: nao chama nada, entao nenhum hook por funcao alcanca. No C gerado o laco
   vira `goto` para um rotulo, e `tools/trace_inject.py` agora insere um
   `RECOMP_POLL()` nos 12.304 rotulos — um decremento e um desvio por iteracao.
   E onde um interrupt real chegaria: num limite de instrucao qualquer.

4. **Entrega de evento transcrita, nao chamada.** `__osPostEvent` guarda `$ra` em
   `$s2` e retorna com `jr $s2`. O N64Recomp nao reconhece isso como retorno e
   emite salto indireto, que com contexto zerado vira chamada para `0x00000000`.
   A rotina foi transcrita para C instrucao por instrucao, junto de
   `__osEnqueueThread`. Dai sairam o `__osEventStateTab` e o layout de
   `OSMesgQueue`, ambos em `libultra_names.txt`.

5. **Retrace com taxa.** Sem portao de 60 Hz o laco ocioso entregava interrupcoes
   na velocidade do host: 464 mil retraces em 20 s. Nao e so feio — temporizacao
   por contagem de frames passa a medir outra coisa.

## O que mudou na segunda rodada

1. **Analise completa em paralelo.** `tools/analyze_all.py` varre as 3.650
   funcoes em 16 processos e produz `analysis/callgraph.txt`,
   `analysis/hw_signatures.txt` e `analysis/cop0_usage.txt`. 24 funcoes formam
   enderecos de MMIO; 16 usam COP0, `cache` ou `eret`.

2. **34 identidades de libultra** em `libultra_names.txt`, cada uma com a
   evidencia ao lado. As de confianca alta saem de assinatura inequivoca —
   quem escreve o bloco VI inteiro mais `VI_ORIGIN` so pode ser a troca de
   buffer; quem restaura 32 GPR de `$k0` e faz `eret` so pode ser o
   despachante de threads.

3. **Layout de OSThread derivado da propria ROM.** `__osDispatchThread`
   (`func_800CCAE4`) restaura o contexto registrador por registrador, entao os
   deslocamentos que ele carrega *sao* a estrutura. Dai sairam tambem quatro
   globais do SO: `__osRunQueue` `0x800ECD08`, `__osRunningThread` `0x800ECD10`,
   `__OSGlobalIntMask` `0x800ECC4C`, `__osRcpImTable` `0x800EFEE0`.

4. **Scheduler cooperativo com fibers.** Cada OSThread vira um fiber do Windows;
   trocar de thread e trocar de fiber. O contexto salvo na RDRAM so e lido para
   *criar* o fiber. E isso que permite substituir apenas `__osDispatchThread` e
   ainda assim manter o resto da libultra original funcionando: quando o codigo
   recompilado chama o despachante, trocamos de fiber, e quando a thread voltar
   a ser despachada ela continua de dentro do proprio yield.

5. **Segundo defeito de limite de funcao encontrado e corrigido.** O `eret` em
   `0x800CCC5C` encerra o despachante; o que vem depois e `__osCleanupThread`, o
   endereco que `osCreateThread` grava no `$ra` de cada thread. O gerador nao
   separou porque `eret` nao e `jr $ra`. Sem a correcao, o `$ra` das threads
   apontava para o meio de outra funcao.

6. **DMA do cartucho implementado.** A interface de `osPiRawStartDma` foi lida
   no desassemblador (`dir, devAddr, dramAddr, size`; `PI_WR_LEN` para ler,
   `PI_RD_LEN` para escrever) e a transferencia agora acontece de verdade. Ainda
   nao foi exercitada: o boot para antes de pedir a primeira.

## Fronteira da libultra

| Regiao | Funcoes | Toca hardware | Leitura |
|---|---:|---:|---|
| `0x80000400`–`0x800C419F` | 3.348 | 0 | codigo do jogo |
| `0x800C41A0`–`0x800D6B70` | 302 | 24 | libultra |

Ordem de link padrao — o jogo primeiro, o SDK anexado — entao as candidatas a
HLE ja estao delimitadas sem chute.

## Percentual estimado

| Etapa | Peso | Feito | Contribuicao | Proximo resultado mensuravel |
|---|---:|---:|---:|---|
| Inventario da ROM e reprodutibilidade | 3% | 100% | 3,0% | — |
| Toolchain e N64Recomp compilado | 5% | 100% | 5,0% | — |
| Mapa de boot, RSP e overlays | 10% | 45% | 4,5% | Overlays mapeados |
| Simbolos, TOML e recompilacao do CPU | 10% | 100% | 10,0% | — |
| Biblioteca C e harness de bootstrap | 7% | 100% | 7,0% | — |
| Identificacao de libultra | 10% | 85% | 8,5% | Filas de mensagem do jogo |
| HLE: threads, scheduler, filas, DMA, timers | 20% | 80% | 16,0% | Tarefa grafica submetida |
| RSP e renderizacao | 20% | 20% | 4,0% | Primeira tarefa de graficos |
| Audio, entrada e persistencia | 5% | 40% | 2,0% | Som audivel e teclado ligado |
| Patches, regressao e distribuicao local | 10% | 0% | 0,0% | Fluxo basico jogavel |
| **Total** | **100%** | — | **60,0%** | — |

Rodadas anteriores: 30,0%, 39,0%, 46,0%, 48,8%, 50,5%, 55,5% e 56,5%.

Os 20% em "RSP e renderizacao" sao caminho de dados de audio funcionando, nao
imagem: nenhuma tarefa grafica foi submetida ainda, e nao ha um pixel desenhado.
Os 30% em audio sao silencio entregue corretamente, nao som.

O salto da sexta rodada **nao veio de trabalho novo**, e sim de dois defeitos do
runtime que mascaravam tudo que ja existia. O DMA, o PIF e a entrega de eventos
ja estavam escritos e corretos ha rodadas; eles simplesmente nunca eram
alcancados. Corrigir `f_odd` e a semantica de `SP_STATUS` nao acrescentou
capacidade, so parou de esconde-la.

O +1 ponto da setima rodada e a OSTask capturada e decodificada: pouco em
percentual, decisivo em direcao — foi ela que mostrou que o proximo passo e
audio, e nao graficos.

Renderizador e RSP continuam sendo o grosso do que falta. Os 10% em "RSP e
renderizacao" refletem tarefa submetida e concluida — nao ha um pixel desenhado,
nem microcodigo interpretado.

Progresso observavel e progresso ponderado nao sao a mesma coisa; misturar os
dois foi como o percentual deste projeto ja chegou a estar errado.

## Perguntas em aberto

- **Todas as threads vivas terminam em espera**, cada uma numa fila do proprio
  jogo (`0x800F9C20`, `0x8010A230`, a fila do SI, a do VI e a de comandos do
  gerenciador de PI). Nenhuma esta bloqueada por hardware que falte; falta saber
  quem deveria alimentar cada uma. Esta e a proxima investigacao.
- **A thread de tarefas `0x8009756C` morre em `func_80097B0C+0x101`**, seguindo
  um ponteiro nulo com deslocamento `+0x4`, logo apos a tarefa de audio ser
  anunciada como concluida. O que ela le nao foi produzido porque o microcodigo
  de audio nunca rodou. **Nao e o renderizador**, como a sexta rodada supos: e o
  microcodigo de audio.
- **Tres das cinco filas nao tem referencia estatica.** `tools/xref_addr.py`
  varreu as 3.651 funcoes procurando quem forma cada endereco: `0x80187B30` sai
  de `func_800BD76C` (a chamada que cria o gerenciador de PI) e `0x801AFA10` de
  `func_800C42E0` (cinco vezes). As outras tres sao alcancadas por ponteiro, nao
  por par `lui`/`addiu` — entao quem as alimenta so aparece em execucao.
- **So uma tarefa de RSP foi submetida** em 20 s. Um jogo em execucao submeteria
  uma por quadro. O numero baixo sugere que o laco principal ainda nao comecou.
- **A taxa de retrace e ~32 Hz, nao 60 Hz**, limitada pela granularidade do
  `Sleep(1)` do Windows.
- **`__osException` continua sendo stub.** A entrega de interrupcao e sintetica:
  postamos o evento e reescalonamos, sem passar pelo tratador da ROM. Basta para
  eventos que so precisam da mensagem na fila; nao vai bastar para nada que
  dependa de `Cause` ou de mascaras reais.
- **O controle responde, mas nao ha entrada.** O PIF devolve "controle padrao
  conectado, nenhum botao pressionado". Nada le teclado ou joystick do host.

RESOLVIDOS, com a causa que se confirmou:

- 3a rodada — a thread `0x80000450` apontava para endereco "invalido" porque a
  funcao nao existia na tabela de simbolos, nao porque o ponteiro fosse lixo.
- 6a rodada — a falha no audio **nao** era dado ausente do cartucho, como a
  quinta rodada supos: era `ctx->f_odd` nulo no runtime. A hipotese do DMA
  ausente estava errada, e o DMA so nao acontecia porque a mesma falha impedia
  o jogo de chegar la.
- 6a rodada — nenhuma tarefa de RSP era submetida porque `SP_STATUS` era tratado
  como memoria comum, nao como registrador de comando.

## O executavel mostra alguma imagem?

**Nao, e nem tem como.** `wpj2_probe.exe` e um programa de console: nao abre
janela, nao cria contexto grafico, nao escreve um pixel. O nome e literal — e uma
sondagem, nao um port.

Mesmo que houvesse janela, nao haveria o que mostrar:

- Nenhuma tarefa grafica foi submetida ainda. As 325 tarefas de RSP foram todas
  do tipo 2, audio.
- Nao ha rasterizador. O RDP nao e emulado nem substituido.
- O gerenciador de VI pede a troca de buffer a cada retrace, mas o buffer nunca
  e preenchido por ninguem.

Para aparecer imagem faltam tres coisas, nesta ordem: o jogo montar uma tarefa
grafica, algo interpretar a lista de exibicao dela, e uma janela para apresentar
o resultado. Nenhuma das tres existe.

## Proxima acao

**Varrer sequencias de entrada, nao estados fixos.** Segurar START mudou o
comportamento; um botao *segurado* nao e a mesma coisa que um botao *apertado e
solto*, e a maioria dos jogos so reage na transicao. O harness ja aceita
qualquer combinacao por ambiente — falta so dar a ele um roteiro temporal
(apertar no segundo N, soltar no N+k) e varrer varios roteiros em paralelo.

Junto disso, **contador por local de chamada**, que continua pendente e continua
sendo o que separa "esta funcao rodou" de "esta chamada rodou".

--- acao ja superada, mantida por historico ---

**Contador por local de chamada.** A rodada mostrou que o traco por funcao nao
basta para dizer *qual* caminho o jogo tomou. `tools/trace_inject.py` ja
reescreve todo o C gerado; acrescentar um contador por `jal` e a mesma tecnica
dos hooks que ja existem, e transforma "esta funcao rodou" em "esta chamada
rodou". Sem isso, cada leitura de fluxo continua sujeita ao mesmo falso positivo.

Com isso no lugar, **entrar em `func_80000C90`.** E o init principal do jogo, chamado uma vez, e
deixou 36 dos 62 destinos sem alcancar. O gerenciador de tarefas ja foi
descartado como suspeito: ele submete 325 vezes sem falhar. O que falta e o lado
do jogo que *monta* uma tarefa grafica, e ele nasce aqui.

Duas formas de atacar, ambas baratas com o que ja existe:

1. Instrumentar a ordem: gravar a sequencia de chamadas dentro de
   `func_80000C90` e ver em que ponto ela para de se ramificar.
2. Desassemblar o corpo dela procurando o desvio que separa os 26 destinos
   alcancados dos 36 que nao sao — provavelmente um unico teste de estado, como
   foi no caso de `func_800BA9D4`.

O renderizador continua sendo o item de 20% intocado, e continua sem fazer
sentido antes de existir uma lista de exibicao para ler.

--- acao ja superada, mantida por historico ---

**Descobrir por que nenhuma tarefa grafica e submetida.** Em 325 tarefas, todas
foram do tipo 2 (audio). Resolvido em parte: o caminho de submissao esta
saudavel, o problema e a montagem da tarefa.

--- acao ja superada, mantida por historico ---

**O microcodigo de audio**, nao o renderizador. A tarefa esta capturada e
decodificada; o que falta e interpretar a lista de comandos em `0x8025ACE0`
(3.696 bytes, ABI de audio) ou, no minimo, preencher com silencio os buffers que
ela referencia. E o que a thread de tarefas le em seguida.

Duas rotas, em ordem de custo:

1. Ler a lista de comandos e atender so os que alocam ou apontam buffer,
   escrevendo silencio. Barato, e desbloqueia a thread sem inventar audio.
2. Interpretar a ABI de audio de verdade. Custa muito mais e so vale quando
   houver saida de som.

O renderizador vem depois: nenhuma tarefa de graficos foi submetida ainda, e nao
sera enquanto a de audio bloquear a thread que submete as duas.

Nao ha bloqueio de infraestrutura antes disso: DMA, PIF, temporizadores, threads,
interrupcoes e a troca com a memoria do RSP funcionam.

--- acao ja superada, mantida por historico ---

Seguir a thread `0x800ADC50` (pri 53), que espera na fila do SI. Ela e a mais
provavel candidata a chegar em `func_800BD218` — a rotina de leitura de ROM do
jogo, chamada por 54 funcoes diferentes e a unica que usa `osPiRawStartDma`.
Assim que ela rodar, o DMA passa a ser exercitado e as duas substituicoes de
transferencia ganham validacao empirica.

Em paralelo, vale um PIF minimo: devolver "controle 1 conectado, nenhum botao
pressionado" em vez de zeros. E barato e tira do caminho uma classe inteira de
decisao do jogo que hoje e tomada com dado invalido.

Depois disso, o renderizador — que e o item de 20% intocado. O gerenciador de VI
ja pede a troca de buffer a cada retrace; falta alguem escrever pixels. Antes de
mexer nele, as tarefas de RSP/RDP precisam existir de verdade: entregar SP e DP
sem elas ja foi testado e piora o resultado.

## Atualizacao de progresso — 10/08/2026

**Estimativa consolidada: 70%.** A tabela de 60% acima foi preservada como
historico da rodada anterior. A recompilacao estatica do CPU esta concluida
(3.651 funcoes geradas e o executavel e reproduzivel); os 30% restantes sao a
fidelidade de execucao: popular os buffers graficos com os assets corretos,
interpretar os estados restantes do RDP/RSP, audio e regressao jogavel.

O aumento nao representa imagem pronta. O runtime ja cria tarefas graficas,
executa `TEXRECT`, carrega TLUTs e escreve framebuffer, mas a rodada de 10/08
mostrou que os buffers de 60 KiB recebidos pelo RDP ainda contem o preenchimento
preto. A proxima meta mensuravel e uma cor de paleta diferente de `0x0001` no
framebuffer produzido pelos sprites.

## Revisao de estimativa — 10/08/2026

**Estimativa corrigida: 65%, e nao 70%.** Os 70% davam peso excessivo ao fato
de as 3.651 funcoes de CPU terem sido geradas. Isso mede traducao estatica, nao
fidelidade do jogo em execucao. A nova evidência separa claramente as duas
coisas: START abre um segundo estado (32 funcoes adicionais e 832 `TEXRECT`),
mas o caminho `func_80090E58 -> func_80094230` ainda monta o atlas CI com o
indice `0x10`, cuja paleta e preto opaco; nenhum texel RGB chegou ao quadro.

| Area | Peso | Evidencia atual | Progresso |
|---|---:|---|---:|
| CPU recompilado estaticamente | 35% | 3.651 funcoes, build reproduzivel | 100% |
| SO/HLE e fluxo de boot | 20% | threads, DMA, PIF, RSP e retrace executam | 85% |
| Fluxo de jogo e entrada | 15% | dois estados observados; navegacao ainda nao avanca | 30% |
| Graficos RSP/RDP | 20% | CI8, TLUT, `TEXRECT` e framebuffer funcionam; sem imagem colorida | 35% |
| Audio | 5% | transporte de buffers, sem sintese fiel | 20% |
| Regressao jogavel | 5% | ainda inexistente | 0% |

O valor e uma calibragem, nao perda de trabalho: o nucleo de CPU segue completo.
Ele será elevado novamente quando uma das proximas metas observaveis ocorrer:
imagem RGB, novo estado apos navegacao, ou uma sequencia jogavel repetivel.

### Evidencia adicional da rodada seguinte

A sonda corrigida mostrou que o preenchimento do atlas e deliberado no estado
atual: `func_80094230` chama o rasterizador em modo `0x0600` (CI8), com
`valor=0x0001`, e grava o indice `0x10` em `0x802CEF20`. A entrada 0x10 da TLUT
e preto opaco. Portanto DMA, TMEM, TLUT e o blitter ja explicam corretamente o
quadro preto; falta atingir outro estado do jogo.

Os roteiros antigos por milissegundo foram removidos da matriz principal porque
69 processos concorrentes deslocavam a janela de entrada no relogio do host. O
laboratorio agora usa `WPJ2_INPUT_POLLS`, contado em leituras reais do Joybus,
para tornar as proximas varreduras de START e navegacao reprodutiveis.

### Referencia externa de sprites

O projeto `josette`, de Ruin0x11, foi incorporado apenas como referencia de
engenharia. Ele documenta a tabela de 96 paletas RGB5551 e o formato SPI dos
objetos. A cada sonda profunda o runtime exporta a TLUT de 512 bytes usada pelo
RDP e `tools/josette_reference.py` a compara com essas paletas, escrevendo
`lab/JOSSETTE_REFERENCIA.md`. Isso permite confirmar se o estado em execucao
usa uma paleta conhecida antes de tentar interpretar os objetos comprimidos.

A comparacao inicial encontrou apenas 7 das 64 primeiras entradas iguais a
uma paleta de referencia (candidata 93). A TLUT atual e portanto parcial ou
montada em RAM, embora a entrada 0x10 preta exista em 13 das 96 paletas. O
extrator e util como oraculo de formatos/paletas, mas nao e uma fonte para
preencher artificialmente a memoria do runtime.

### Estado A+START

START simples executa 319 funcoes e 832 `TEXRECT`; A+START sustentado chegou a
356 funcoes, 178 KiB de DMA e 156 `TEXRECT`, sem imagem RGB. Ele nao aumenta a
uniao total de funcoes, mas percorre uma combinacao diferente de inicializacao,
inclusive `func_80000C90` e `func_80002F20`. A proxima matriz testa a ordem e
a liberacao de A+START por leituras do Joybus, e roda uma sonda de textura/TLUT
separada nesse estado.

### Revisao de reprodutibilidade da matriz

A rodada mais recente expôs uma limitação da medição, não um novo avanço de
emulação: a configuração base variou entre 313 e 339 funções, e A+START entre
356 e 371, quando 63 instâncias e a análise estática disputavam CPU ao mesmo
tempo. Como o retrace do HLE usa relógio real, esses números não podem ser
atribuídos com segurança à entrada enquanto houver sobrecarga do host.

O `RODAR.bat` passou a executar no máximo seis jogos por lote
(`WPJ2_LAB_WORKERS=6`) e inicia a análise estática apenas após a matriz. Mantém
o modelo temporal do N64 e permite que `base`, `rep1`, `rep2` e `rep3` virem
uma medida válida de variância. A próxima rodada também acrescenta uma terceira
sonda profunda para a janela `A` na leitura 1 e `A+START` na leitura 3.

**O percentual permanece 65%.** Ainda não foi gerada imagem visível: os PPMs
existentes são diagnósticos de framebuffer/preto e não contam como sprite ou
tela renderizada.

### Correção de interrupção SI pendente

A matriz estabilizada mostrou que cada execução fazia somente **uma** leitura
`CMD_READ_BTN` do controle, embora o loop do jogo recebesse retraces por 20 s.
Isso torna inválidos os roteiros `WPJ2_INPUT_POLLS` posteriores: não havia uma
segunda leitura na qual aplicar START.

A causa encontrada foi o HLE postar SI a cada retrace. No N64, SI é a conclusão
de um `__osSiRawStartDma`, não um evento periódico; as mensagens espúrias podiam
encher a fila de oito posições antes da próxima transferência real. O PIF agora
mantém conclusões pendentes por DMA e o HLE posta SI somente para elas. A tabela
do laboratório também passa a registrar a quantidade de leituras reais de
controle por corrida. A build foi verificada; a próxima rodada valida se a
sondagem passa a receber entrada recorrente.

### Validação de SI e nova fronteira de entrada

A rodada seguinte validou a correção: a base fez **10** leituras reais de
controle e START sustentado fez **11**, em vez da única leitura anterior. START
nas leituras 2 a 7 chega repetivelmente ao mesmo estado de 319 funções, 37–38
listas gráficas e 780–832 `TEXRECT`; um pulso de apenas uma leitura retorna ao
estado base. Portanto a entrada é observada, mas ainda não há uma escolha de
jogo posterior à transição de boot nos roteiros atuais.

A matriz passa a testar A, B, direções, soltura e novo pressionamento nas
leituras 8–11, já dentro do estado START; a sonda profunda correspondente é
`start_a_p8`. O percentual e o veredito visual permanecem **65%** e **sem
imagem visível**.

### Platô confirmado e mudança de estratégia

A varredura pós-START confirmou um terceiro caminho reprodutível:
`START` desde a primeira leitura e `A` na leitura 8 chega a **371 funções** e
218 KiB de DMA. É o máximo alcançado até aqui, mas ainda usa
`func_80090E58 -> func_80094230`, escreve índice CI8 `0x10` e produz zero
texels RGB. As 71 sondagens já cobrem todos os botões principais, transições,
ordem, memória e taxa de retrace que são úteis nesse estágio.

Assim, o projeto está **bloqueado no progresso funcional**, embora o runtime
continue compilando e executando de modo estável. Não é honesto aumentar o
percentual por novas sondagens que apenas confirmam o mesmo quadro preto;
mantemos **65%** até surgir uma referência diferencial ou um renderizador mais
fiel. A próxima frente não é ampliar `RODAR.bat`, e sim comparar o estado da
RDRAM/listas gráficas contra uma execução original e avaliar a migração do HLE
artesanal para N64ModernRuntime + RT64.

Referências de maior valor, em ordem:

1. Estados salvos e capturas do Project64 da **mesma ROM traduzida**, em tela
   inicial, após START e após a primeira ação que muda a tela. Eles permitem
   comparar RDRAM, TLUT e display list com um oráculo real.
2. Qualquer símbolo, mapa de objetos/SPI, dump de memória ou documentação de
   *Wonder Project J2*. O extrator `josette` já cobre apenas paletas e objetos;
   um formato de cenas/tabelas de scripts seria especialmente valioso.
3. Projetos N64Recomp que usam N64ModernRuntime e RT64. Eles são referência de
   integração do runtime/RSP/RDP, mas não fornecem lógica ou assets deste jogo.

### Oráculo visual e endereço segmentado F3DEX

As capturas do Project64 confirmam a sequência real: preto inicial, logos ENIX,
Givro e J2, seguido de um menu 2D colorido com Bird e slots de diário. Portanto
o quadro preto do recompilado não é um estado final válido.

A comparação revelou uma falha específica no blitter: a lista raiz contém
`G_MOVEWORD` que define o segmento 1 em `0x001B20A0` e depois chama
`G_DL 0x01000058`. O interpretador tratava `0x01000058` como endereço físico
de 16 MiB e pulava a sublista; o endereço correto é `0x001B20F8`. A resolução
dos 16 segmentos Fast3DEX foi implementada também para `SETTIMG` e para a
varredura de listas. A build foi concluída, mas ainda precisa da próxima rodada
para validar pixels coloridos antes de alterar os **65%**.

### Validação do F3DEX segmentado e próxima sonda dirigida

A rodada de validação confirmou a correção estrutural: o dump agora entra nas
sublistas `0x01000058` e `0x01000080`, e o contador de comandos gráficos de
START subiu de 12.422 para **13.421**, sem trocar o estado de entrada. Portanto
o resolvedor de segmentos está ativo; ele não foi apenas uma alteração teórica.

Ainda assim, os 780 `TEXRECT` dessa tela leem o atlas `0x802CEF20` com índice
CI8 `0x10`; a entrada 0x10 da TLUT é `0x0001`, preto opaco. As fontes e a TLUT
chegam à memória, mas o compositor `func_80094230` está selecionando a faixa
preta. Nenhum dos framebuffers/PPMs possui RGB diferente de preto, logo **não
há imagem gerada pelo decompilador** nesta rodada.

A próxima build adiciona telemetria de baixo custo ao próprio compositor:
chamador, descritor e argumentos das primeiras chamadas, além dos contadores de
animação e deslocamento que o alimentam. Isso identifica se a transição preta
está parada por relógio/estado ou se a descrição de tela carregada já é a
errada. O `RODAR.bat` permanece com a matriz estabilizada de 71 cenários; não
foi ampliado, pois a nova evidência vem do runtime e não de combinações extras
de botões.

**Percentual mantido em 65%.** Houve correção mensurável do interpretador de
listas, mas ainda não houve avanço funcional/visual suficiente para aumentar a
estimativa.

### Compositor ativo; tabela de objetos pendente de validação

A telemetria confirmou que a transição não está parada por ausência de execução:
em START, `func_80094230` foi chamada **6.870 vezes** e os acumuladores de
posição variaram durante os 20 s. A hipótese de um relógio imóvel foi descartada.

Também foi corrigida uma interpretação do diagnóstico: o primeiro argumento do
compositor é um **índice de objeto**, não um ponteiro. A rotina o multiplica por
12 e consulta a tabela apontada por `0x8015F880`; o instrumento anterior
imprimia esses índices pequenos como se fossem endereços e por isso seus
"descritores" não eram dados válidos.

A build seguinte registra o ponteiro da tabela, o endereço/primeiro byte do
objeto consultado e os seletores CI8. Essa é a última sonda dirigida antes de
decidir entre corrigir o carregamento da tabela ou tratar a seleção de estado
como o próximo bloqueio. **Percentual: 65%; imagem visível: não.**

### Oráculo diferencial com Project64

O atlas CI8 do recompilado foi exportado e visualizado: ele contém dados
coloridos, enquanto o estado alcançado só escreve os índices `0x10` e `0xFF`
no atlas final. Isso confirma que não falta asset nem suporte básico a CI8; a
máquina de estado/HLE está levando o jogo a uma composição preta, antes da
tela colorida observada no Project64.

Para sair dessa inferência, foi clonado e compilado em modo Debug o código-fonte
oficial do Project64 em `tools/Project64-source`. A variante instrumentada
exporta a RDRAM real após `UpdateScreen`, em anel de três snapshots, quando a
variável `WPJ2_ORACLE_DUMP` está definida. O inicializador
`ORACULO_PROJECT64.bat` define essa variável e abre a ROM; os dumps serão
gravados em `oraculo/pj64-rdram/`. O script
`tools/extract_project64_oracle.py` transforma cada dump no atlas CI8+TLUT para
comparação visual e por índices com o runtime recompilado.

Este é um avanço de método, ainda não de execução funcional: **65%**, sem imagem
visível gerada pelo decompilador. A próxima evidência útil será um snapshot do
Project64 no menu ou na seleção de diário, que permite localizar exatamente o
estado, atlas e paleta ausentes no recompilado.

### Ajuste de compatibilidade do oráculo Project64

O primeiro executável instrumentado era a variante Debug, que carrega DLLs
`*_d.dll`. A instalação existente em `E:\projetos\Project64\Plugin` contém
plugins Release da série 3.0.1; misturá-los com o núcleo Dev-4.0 compilado do
oráculo pode falhar no carregamento e não é uma base confiável para a comparação.

Foram gerados o `Project64.exe` Release e os plugins Release correspondentes
(`Project64-Video.dll`, `Project64-Audio.dll`, `Project64-Input.dll`,
`Project64-RSP.dll` e `PJ64_NRage.dll`) a partir do mesmo código-fonte
instrumentado. `ORACULO_PROJECT64.bat` passou a abrir essa combinação e a
configuração força esses nomes. A instalação original do usuário não foi
alterada. O pós-build acusou somente a verificação de formatação do arquivo
instrumentado; os binários Release foram produzidos antes dessa checagem.

Estado atual: aguarda uma execução visível do `ORACULO_PROJECT64.bat` para
produzir snapshots de RDRAM. **Progresso: 65%; imagem do decompilador: não.**

### Correção da preferência de plugins por-ROM

O primeiro lançamento visual revelou que, apesar do executável Release, a ROM
tinha quatro substituições persistentes em `Config/Project64.rdb.user` que
forçavam `Project64-Video_d.dll`, `Project64-Audio_d.dll`,
`Project64-Input_d.dll` e `Project64-RSP_d.dll`. Essas preferências por-ROM
tinham precedência sobre a configuração global e explicam o primeiro print com
plugins Debug. As quatro chaves foram removidas; a configuração global agora
seleciona explicitamente os DLLs Release gerados junto ao oráculo. A próxima
execução deve mostrar os nomes sem `(Debug)`.

### Diffs reais do Project64 e bloqueio do despachante de conteúdo

Os três dumps válidos do Project64, gerados após a correção dos plugins,
concordam no estado relevante: `state=2`, `limite=79`, `task=54` e índice
ativo já liberado (`-1`). O mesmo ponto do recompilador permanece em
`state=1`. Isso transforma a diferença em um alvo verificável, não apenas uma
diferença visual.

A telemetria corrigiu também uma hipótese anterior: a fila gráfica inicial
recebe mensagens normalmente (165 postagens por `osSendMesg`), portanto DMA/PI
e essa fila não são a causa direta. A cadeia ausente é o despachante de
conteúdo `0x8000DB14 -> 0x800B55B0 -> 0x800B5FB4`; ela chamaria
`0x80021ED0`, que seleciona o índice ativo usado pela transição. Nenhuma dessas
rotinas aparece no traço do recompilador.

Foi incluído `WPJ2_FORCE_STATE2_AFTER` como **desvio diagnóstico opcional**.
Ele não corrige a emulação; permite executar o ramo posterior e medir os
próximos bloqueios. Com ele, a sonda alcançou 245 tarefas RSP, 30 listas
gráficas e framebuffer configurado. O PPM contém somente 126 pixels não pretos
na última linha, sem textura/TLUT útil: é estado parcial/ruído de framebuffer,
não uma tela do jogo. Logo, **ainda não há imagem útil gerada pelo
decompilador**.

**Percentual mantido em 65%.** O diagnóstico avançou ao isolar a rotina que
não entra no traço, mas a transição real e a primeira tela visível ainda não
foram reproduzidas.

### Validação causal do índice ativo

O teste posterior não forçou o estado: no poll 500 ele escreveu somente o
índice ativo `0` que deveria vir de `0x80021ED0`. A rotina original
`0x800BA9D4` então fez a transição e manteve `state=2`, exatamente como os
três dumps do Project64. O índice voltou a `-1`, também igual ao oráculo.

Isso confirma a causa imediata: falta executar a cadeia de callbacks que
produz o índice, e não implementar mais DMA, RSP, fila gráfica ou paleta. A
sonda continuou sem `LOADBLOCK` de textura e sem texel RGB útil; o framebuffer
permaneceu com apenas 126 pixels espúrios na última linha. **Não há imagem
útil do jogo gerada pelo decompilador.**

**Percentual revisado para 66%.** O aumento de um ponto representa a transição
real reproduzida pelo código do jogo e a causa isolada com teste causal; não
representa uma tela jogável.

### Scheduler e carregador de cena validados

Foi corrigida uma lacuna do runtime: para um salto MIPS para ele mesmo, o
N64Recomp gera `pause_self()`. A implementação local era vazia e deixava a
execução C cair no bloco seguinte, que é inalcançável no console. Agora ela
estaciona a fiber corrente no scheduler cooperativo. Esta é uma correção de
semântica do runtime, não uma escrita artificial de estado do jogo.

O carregador `0x8000C584` recebeu o índice 500 e retornou sucesso. O VI também
passou a expor framebuffer válido (`VI_ORIGIN=0x00200200`, largura 320). A
rotina de fluxo avançou de estado 1 para 8, com pendência `0x8008`, mas a cadeia
`0x80001F54 -> 0x80021ED0` ainda não é chamada. O alvo seguinte é a sequência
de mensagens recebidas pelo loop principal.

O framebuffer continua sem `TEXRECT`, `LOADBLOCK`, TLUT ou texel RGB útil.
**Ainda não há imagem útil gerada pelo decompilador.**

**Percentual revisado para 67%.** O ponto adicional cobre a correção verificável
de fibers e a validação do carregador/VI; não significa que a tela inicial
esteja renderizada.

### Teste decisivo preparado: cadeia de ativação no Project64

Foi preparada uma sonda de execução pontual no Project64 já funcional. O
script `Bin\Win32\Release\Scripts\wpj2_exec_chain.js`, executado com
depurador e core Interpreter, grava em `oraculo\pj64-rdram\wpj2_exec_chain.txt`
somente: escrita nos campos de guarda, entrada em `0x80001F54` e entrada em
`0x80021ED0`. Assim ele consegue separar, em uma única execução, a origem real
da ausência de ativação da cena de um efeito de temporização do recompilado.

Também foi deixada uma instrumentação equivalente no fonte do recompiler do
Project64. A reconstrução dela foi interrompida: o MSBuild do ambiente atual
falha antes de compilar o núcleo, em `FileTracker`, por uma configuração de
ambiente externa (`Path`/`PATH` duplicados ou caminho inválido). Não é falha
do código da ROM e não justifica repetir compilações. O script usa o depurador
embutido e substitui essa dependência.

O resultado desta sonda é uma condição de continuidade: se ela não revelar um
próximo PC concreto, o trabalho deve ser pausado em vez de retomar matrizes de
testes amplas. **Percentual mantido em 67%; imagem do decompilador: não.**

### Resultado decisivo: o original executa a ativação

A sonda do Project64 produziu 4.152 entradas em `0x80001F54`, uma entrada em
`0x80021ED0` e a escrita seguinte do índice ativo em `0x80021EE4`. Portanto a
ativação não é uma hipótese: ela faz parte do caminho inicial da ROM original.
Os valores registrados na primeira chamada foram `flags=0x1421` e
`active=0xFFFFFFFF`; a própria `0x80021ED0` mudou o índice para `7`.

No runtime recompilado, `func_80000C90` entra uma vez e alcança
`func_80002F20`. Quando ela devolve `1`, o código MIPS executa seu laço
intencional em `0x80000EC8` (`pause_self`) e aquela thread não chega ao trecho
posterior que chama `0x80001F54`. A implementação de `pause_self` está correta;
o ponto comparável agora é o retorno/estado produzido por `func_80002F20`.

O mesmo teste local alcançou 30 tarefas gráficas e listas de exibição válidas,
mas ainda sem `TEXRECT`/`LOADBLOCK`; elas apenas limpam os framebuffers. Assim
há uma cadeia concreta a corrigir antes da renderização de sprites.

**Percentual revisado para 68%.** Este ponto adicional representa uma prova
dinâmica no emulador original e o isolamento do retorno que bloqueia a thread,
não uma imagem jogável. **Imagem do decompilador: não.**
