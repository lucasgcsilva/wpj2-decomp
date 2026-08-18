# Análise do chiado no áudio

Documento vivo da investigação. Substitui a versão anterior (lista de 11
causas especuladas), que foi escrita antes de existir instrumentação capaz de
decidir entre elas.

**Estado atual:** o áudio funciona — a música toca correta, com chiado
sobreposto, intermitente, com estalos ("pipoco") em alguns trechos. Não é
bloqueador para o resto da decompilação.

**Divergência contra o microcódigo real da ROM: 25,31% em média, com 223
listas (74%) errando acima de 1% do fundo de escala.**

> ### ⚠️ Leia 5k antes de 5c–5j
>
> As seções 5c a 5j relatam uma melhora de 223 para ~45 listas. **Ela não
> existiu.** A alteração de 5c zerava os ganhos mestres e silenciava a mistura;
> a divergência caiu porque silêncio diverge pouco. Tudo que foi medido sobre
> aquela base está contaminado. As seções ficam no documento porque os
> experimentos e as reversões continuam informativos — mas os *ganhos* não.

> Use **as duas** métricas. A média é dominada por poucos casos que saturam em
> 200%; a contagem acima de 1% é a que corresponde ao que se ouve. Uma
> alteração pode piorar uma e melhorar a outra, e foi o que aconteceu em 5c.

> **Ruído do instrumento — medido, não estimado.** Três execuções do mesmo
> código deram **9,66% / 46**, **10,90% / 46** e **8,98% / 43**. Portanto:
> diferenças de média abaixo de **~1,5 ponto** e de contagem abaixo de
> **~4 listas** não significam nada. As listas musicais variam entre
> execuções, e é isso que move os números. Para julgar uma alteração, exija
> movimento maior que isso — ou repita a medição.
>
> *(Os valores acima foram medidos sobre a base quebrada de 5c. A ordem de
> grandeza do ruído continua válida; os níveis absolutos, não.)*

> ### ⚠️ Meça o RMS junto com a divergência
>
> A divergência sozinha **premia silêncio**: uma saída atenuada diverge menos
> em valor absoluto. Foi assim que 5c passou por melhora durante uma sessão
> inteira. Toda medição de divergência deve vir acompanhada de RMS e DC —
> `tools\medir_voz.py` faz os três de uma vez.

---

## 0. Paliativo ativo — `no_wet` por padrão

**Desde 16/08/2026 o caminho wet (reverb) está desligado por padrão** em
`acmd_no_wet()` (`runtime/rsp.c`).

Motivo: na comparação A/B de ouvido entre `ab_base.wav`, `ab_no_wet.wav` e
`ab_sem_polef.wav`, o `no_wet` foi o que menos chiou — especialmente nos
trechos de maior volume. E a medição sustenta: o caminho wet responde por
~70% do offset DC (+278 com ele, +84 sem; o Project64 fica em −32).

**Isto não é a correção.** Desligar o reverb remove também o efeito legítimo
que a trilha usa, e o defeito real continua aberto. É uma troca consciente —
menos chiado agora, menos fidelidade — enquanto a causa é investigada.

```
WPJ2_AUDIO_NO_WET=0     restaura o caminho completo
```

> **Importante:** ao medir divergência contra o microcódigo real, use sempre
> `WPJ2_AUDIO_NO_WET=0`. Com o paliativo ligado, a comparação mede um caminho
> que a ROM não executa, e o número deixa de significar o que se quer.

Quando a causa for corrigida, este padrão deve voltar a `0`.

---

## 1. Como medir

```
tools\sonda_audio.cmd 25 -2 5C0:F80
```

Executa nosso HLE em C e o microcódigo da ROM recompilado sobre as mesmas
listas, a partir do mesmo estado, e imprime estatística acumulada:

```
[populacao] 300 listas: media=5966 LSB (18.21%), max=65535 (200.00%),
            acima de 1%=223 (74%), falhas=0
```

Essa é a **linha de base**. Qualquer correção deve ser comparada contra ela,
com a mesma contagem de listas. Sem isso não há como afirmar melhora.

> **Atenção:** o `RODAR.bat` **não serve** para investigar áudio. A matriz dele
> roda quase toda com `WPJ2_AUDIO_FAST=1`, que por desenho pula ADPCM,
> RESAMPLE e ENVMIX. Durante dias medimos um modo em que a síntese não roda.

---

## 2. O que está estabelecido por medição

### O defeito é anterior à saída
O WAV gravado (`WPJ2_AUDIO_WAV`) já contém o chiado. WinMM, driver e fila de
reprodução estão descartados.

### Não é clipping
Nossa saída tem pico 27038 e zero amostras saturadas. O Project64 satura mais
que nós (pico 32768, 10 amostras). A hipótese de "estamos quentes demais"
nasceu de um arquivo trocado e não sobreviveu à medição correta.

### O ganho global está correto
Comparando percentis de RMS por segundo contra o Project64:

| percentil | PJ64 | nosso | diferença |
|---|---|---|---|
| p50 | 5331,8 | 4391,9 | −1,68 dB |
| p75 | 5818,0 | 5759,8 | −0,09 dB |
| p90 | 6062,7 | 6206,2 | **+0,20 dB** |

Nas passagens altas batemos dentro de 0,2 dB. Não há shift errado nem ganho
aplicado duas vezes — isso apareceria como desnível constante em todos os
percentis.

### Existe offset DC que o Project64 não tem

```
Project64:  L = −31,7    R = −32,0
nosso:      L = +278,6   R = +282,9
```

Com `WPJ2_AUDIO_NO_WET=1` o DC cai para +83,9 (**~70% vem do caminho wet**).
Com `WPJ2_AUDIO_BYPASS_POLEF=1` cai para +252,0 (~10% do POLEF).

### O piso não é ruído branco
Aspereza (energia da primeira diferença sobre energia do sinal) das janelas
mais silenciosas: **0,055** — sinal grave e correlacionado, não banda larga.
Nossa janela de 50 ms mais silenciosa nunca desce de RMS 252; a do Project64
chega a 5,1. O piso é essencialmente a componente contínua.

### O defeito é abrangente, não caso de borda
74% das 300 listas medidas divergem acima de 1%. Isso invalidou várias
análises que eu havia feito sobre listas isoladas — a lista que bate a 0,02%
é a exceção, não a regra.

---

## 3. Hipóteses descartadas

Todas verificadas contra o Project64 e/ou o microcódigo recompilado.

| # | Hipótese | Como caiu |
|---|---|---|
| 1 | ABI errada (NAUDIO em vez de ABI1) | `SETBUFF` aparece 198×/AList; na NAUDIO o opcode 8 é no-op. É ABI1. |
| 2 | `INTERLEAVE` com endereços/ordem errados | Endereços (`w2`-derivados) e laço (`r2,l2,r1,l1`) idênticos ao PJ64 |
| 3 | Estado do ENVMIXER lido sem word-swap | Offsets batem byte a byte com o PJ64; acesso cru reproduz o `memcpy` dele |
| 4 | `SETBUFF` decidindo ramo por igualdade | Censo mostra só `0x00` e `0x08`; nenhum valor cai fora |
| 5 | `SETVOL` decidindo por igualdade | Nosso `case 9` já usa teste de bit (`f & 8`, `f & 2`, `f & 4`) |
| 6 | Destinos do ENVMIXER nunca configurados | `dry_l/dry_r/wet_l/wet_r` corretos, distintos, espaçados de `0x140` |
| 7 | POLEF como fonte do DC | Responde por apenas 10% do offset |
| 8 | Acumulador do ENVMIXER | `sample_mix` do PJ64 é idêntico ao nosso, com clamp |
| 9 | `MIXER` | `alist_mix` idêntico |
| 10 | Wraparound na soma | `acmd_write_s16` satura, não dá a volta |
| 11 | Arredondamento `(a*b)>>15` sem `+0x4000` | Reduziu divergência de bytes mas **aumentou** o DC de 278 para 337 |
| 12 | Guarda `if (step)` do envelope | Medido: piora de 18,21% para **33,08%** |
| 13 | Defeito no driver/WinMM | O WAV já chia |

---

## 4. Pista viva

Vozes com `target = 0` e `value ≠ 0` — ou seja, em decaimento — apresentam:

```
target_l = 0            value_l = 400317496       (ganho 6096)
exp_seq_l = 400317499   exp_seq − value = 3
```

O passo é calculado como `(exp_seq − value) >> 3`. Com diferença 3, o `>> 3`
trunca para **zero**. E o guarda `if (step_l)` passa então a ser falso para
sempre: `exp_seq` nunca mais é atualizado, `step` não sai de zero, e a voz
**congela no ganho corrente em vez de sumir**.

O microcódigo real, partindo do mesmo estado, leva a voz a zero e **zera os 80
bytes** do bloco (libera a voz).

Contraste entre duas listas vizinhas, rodando o mesmo código nosso:

| | lista que acerta (0,02%) | lista que erra (15,84%) |
|---|---|---|
| vozes | 25 | 25 |
| com `target = 0` | 25 | 10 |
| `value` dessas | **0** (já extintas) | **400317496** |
| `exp_seq − value` | 0 | **3** |

A lista correta não expõe o defeito porque nela não há decaimento pendente.

**Ressalva importante:** corrigir só o guarda (trocar por `value != target`)
**piora** o número. O travamento é real e observado, mas há algo mais junto —
provavelmente na atualização de `exp_seq` entre listas.

---

## 5. Ferramentas construídas

| Ferramenta | Para quê |
|---|---|
| Bisseção diferencial (`WPJ2_BISSECAO`) | Busca binária pelo primeiro comando cuja DMEM diverge do microcódigo real |
| Limiar de magnitude (`WPJ2_BISSECAO_LIMIAR`, padrão 64) | Sem ele a busca para num erro de 1 LSB (−90 dB) e mascara erros grandes |
| Faixa (`WPJ2_BISSECAO_FAIXA`, hex `ini:fim`) | Restringe a comparação; comparar tudo dilui o sinal |
| Modo população (`WPJ2_BISSECAO=-2`) | Estatística sobre todas as listas — a única métrica reproduzível |
| Hash de conteúdo | Nomeia listas de forma estável |
| `tools\sonda_audio.cmd` | Sonda com áudio ligado e fast mode desligado |
| `Scripts\wpj2_audio_replay_oracle.js` | Captura AList + estados + saídas do Project64 |
| `tools\medir_dc.py`, `piso_espectro.py`, `comparar_pj64.py` | Medições de DC, aspereza e comparação com o PJ64 |

**Detalhe de implementação que custou caro:** truncar a lista alterando
`data_size` fazia o microcódigo abortar e deixar a RDRAM zerada; o harness lia
isso como divergência e a busca convergia para "onde o nativo falha". A
truncagem correta mantém o tamanho e transforma os comandos seguintes em
`SPNOOP`.

---

## 5b. Captura do Project64 — endereço errado (descartada)

A captura feita por `Scripts\wpj2_audio_replay_oracle.js` gerou 8 ALists, 200
blocos de estado e 549 saídas. **Os blocos de estado não servem**: não são o
bloco de 80 bytes do ENVMIXER.

Testadas quatro leituras (big/little-endian, com e sem swap `^2`) contra dois
critérios diferentes:

1. Faixa absoluta (`wet`/`dry` cabendo em 16 bits, `exp_rate` perto de 65536)
   → 0 acertos em 175 blocos. **Critério inválido**: a faixa de `exp_rate` foi
   tirada de uma única voz nossa (65505), mas ela varia — no mesmo dump nosso
   aparece `-1` noutra voz.
2. Invariante estrutural (`exp_seq` ≈ `value`, que no nosso dump diferem por 3
   em 400 milhões) → **0 acertos nas quatro leituras**. Este critério não
   depende de faixa e mesmo assim rejeitou tudo.

O que os números mostram:

```
wet=398006201  dry=397940664  target=397875127 / 397875126
```

Campos consecutivos diferindo por exatamente **65537 (0x10001)**, de forma
repetida. Isso é uma região de memória com padrão que varia devagar, lida em
offsets sucessivos — não uma estrutura com campos distintos.

**Causa:** o script resolve o endereço do estado acumulando os comandos
`SEGMENT` da AList, e essa resolução não corresponde à que o microcódigo usa.
O `dump()` grava memória válida, só que do lugar errado.

**Consequência prática:** não existe ordem de bytes a descobrir. Quem retomar
não deve gastar ciclos testando interpretações — o problema é o endereço.

Para consertar a captura, o endereço do estado deve vir do mesmo cálculo que o
nosso runtime faz em `acmd_address()` (`rsp.c`), que já produz valores
coerentes (`0x229BD0` e vizinhos, com campos plausíveis). O script do PJ64
precisa replicar essa resolução, não inventar a sua.

---

## 5c. `wet`/`dry` com swap `^2` — melhora medida

**Alteração aplicada e mantida** em `acmd_envmix` (`rsp.c`):

```c
dry = acmd_read_s16(rdram + state + 4);   /* era *(int16_t*)(rdram + state + 4) */
wet = acmd_read_s16(rdram + state + 0);
```

Resultado sobre 300 listas:

| | leitura crua (antes) | com swap `^2` |
|---|---|---|
| média | 18,21% | **22,05%** |
| listas acima de 1% | 223 (74%) | **61 (20%)** |
| máximo | 200% | 200% |

A média piora, a contagem melhora **3,6×**. Não é contradição: a média é
dominada por poucos casos extremos que saturam em 200%, enquanto a contagem
mede quantas listas de fato erram.

**Para audibilidade, a contagem é a métrica que importa** — 61 listas erradas
soam melhor que 223, mesmo que as erradas errem mais. A média foi eleita
manchete por engano nas rodadas anteriores; usar as duas é o correto.

Por que a leitura crua estava lá: justificada por "o Project64 faz igual, com
`memcpy` + leitura nativa". Essa justificativa **caiu** quando a captura do
PJ64 se mostrou inválida (5b) e quando ficou claro que o PJ64 é HLE
aproximado, enquanto o nosso oráculo é o microcódigo real da ROM. Todo o resto
do código de áudio usa os helpers com swap; `wet`/`dry` eram a exceção.

**Ainda não resolvido:** os 20% de listas que continuam divergindo, e os casos
extremos que saturam em 200%. O `exp_rate` (+16) e os campos de 32 bits
continuam sem validação externa.

---

## 5d. Rampas em 32 bits — melhora nas duas métricas

**Alteração aplicada e mantida** em `acmd_envmix` (`rsp.c`):

```c
int32_t value_l, value_r, target_l, target_r, step_l, step_r;
/* era int64_t */
```

| | int64 (antes) | int32 (agora) |
|---|---|---|
| média | 22,05% | **9,66%** |
| listas acima de 1% | 61 (20%) | **46 (15%)** |

A média cai pela metade e a contagem melhora. Ganho nas duas — diferente de
5c, onde as métricas divergiram.

O motivo estava documentado no próprio código desde antes (`rsp.c`, comentário
do `acmd_mac32`): *"O acumulador do ucode e de 32 bits. Os filtros contam com o
transbordo em complemento de dois... promove-los a 64 bits parece mais seguro,
mas altera a assinatura sonora dos picos."* A observação existia para os
filtros, mas as **rampas do envelope** continuaram em 64 bits. Onde o hardware
transborda e dá a volta, nós seguíamos crescendo.

Isso também explicava parte dos casos extremos: uma rampa que não transborda
diverge sem limite da que transborda.

### Progresso acumulado da sessão

| etapa | média | listas acima de 1% |
|---|---|---|
| linha de base | 18,21% | 223 (74%) |
| 5c — `wet`/`dry` com swap | 22,05% | 61 (20%) |
| 5d — rampas em 32 bits | **9,66%** | **46 (15%)** |

**Quase 5× menos listas divergentes.** Nenhuma dessas duas correções foi
verificada de ouvido ainda — a métrica é contra o microcódigo real, que é a
referência certa, mas o efeito audível permanece por confirmar.

---

## 5e. Ganhos em 32 bits — testado, revertido, e calibrou o instrumento

Aplicado o mesmo princípio de 5d aos quatro ganhos do ENVMIXER (remover o
`(int64_t)` do intermediário). Resultado:

| | com `int64_t` | sem |
|---|---|---|
| média | 9,66% | 10,90% |
| listas acima de 1% | 46 | **46** |

**Revertido.** E a análise do porquê é o que dá valor ao teste: `lv` é
`value >> 16`, no máximo ~32767; multiplicado por `dry` (s16) dá ~1e9, que cabe
em `int32` com folga. **Não há transbordo a reproduzir aqui** — a alteração é
semanticamente um no-op, ao contrário das rampas, onde os valores chegam a
4·10⁸ e o transbordo é real.

**O que isso mediu de graça:** como a mudança não faz nada, a diferença de
9,66% para 10,90% é **variação entre execuções**. Foi assim que o ruído do
instrumento ficou conhecido — ver o aviso no topo do documento. Diferenças de
média abaixo de ~1,5 ponto não devem ser interpretadas; a contagem acima de 1%
manteve-se em 46 nas duas execuções e é o número confiável.

Lição de método: um experimento que não muda nada ainda é útil, desde que se
saiba de antemão que ele não muda nada. Serve de controle.

---

## 5f. Alvo restante e a hipótese mais motivada

Bisseção após as correções 5c/5d:

```
PRIMEIRA DIVERGENCIA na tarefa de audio 31, comando 167 de 668
  ENVMIXER flags=0x08  w0=03080000  w1=00229BD0
```

**Mesmo ponto de antes das correções**: ENVMIXER com `flags=0x08`, ou seja o
caminho **sem** `A_INIT` — o que retoma o bloco de 80 bytes da RDRAM. As
correções reduziram muito a divergência geral, mas não tocaram neste caso.

### Hipótese testada e descartada

Levantei que, se `wet`/`dry` precisaram do swap (5c), o layout real do bloco
seria diferente do que o PJ64 assume — e portanto os campos de 32 bits também
estariam deslocados.

**Testado** (meias-palavras trocadas em `target`, `exp_rate`, `exp_seq`,
`value`) e **descartado**:

| | leitura direta | meias-palavras trocadas |
|---|---|---|
| média | ~9% | 14,21% |
| listas acima de 1% | 43–46 | **52** |

Piora acima do ruído (~4 listas), nas duas métricas.

**E isso corrige o raciocínio do 5c.** A correção de `wet`/`dry` não revelou
"layout diferente do PJ64" — revelou uma **inconsistência nossa**. Numa RDRAM
word-swapped, um `u32` alinhado já sai correto; só os acessos de meia-palavra
precisam do `^2`. Todo o código de áudio usa os helpers com swap; `wet`/`dry`
eram a exceção, e o 5c apenas alinhou isso. O layout continua sendo o do PJ64.

Conclusão: **os oito campos de 32 bits estão sendo lidos corretamente.** A
causa da divergência residual em `flags=0x08` está em outro lugar.

### O que resta investigar

O caminho `flags=0x08` difere do `A_INIT` apenas por carregar o estado da RDRAM
em vez de calculá-lo. Se os campos são lidos certo, sobra:

1. **O que gravamos** ao final do ENVMIXER — se a escrita do bloco diverge, a
   leitura seguinte parte de valores errados mesmo lendo corretamente. Nunca
   foi verificado contra o microcódigo real.
2. **O `>> 3` do passo** e o guarda `if (step_l)`, já documentados como
   travando a rampa (5, "Pista viva"). Corrigir só o guarda piora, mas a
   interação entre gravação e travamento não foi testada em conjunto.

O item 1 é o mais promissor e nunca foi olhado: toda a atenção foi para a
leitura.

Lembrar do ruído medido: exigir movimento maior que ~4 listas para concluir.

---

## 5g. A assimetria leitura/escrita é correta — não "consertar"

O item 1 do 5f (a gravação do bloco) foi testado. A assimetria salta aos olhos:
depois do 5c, `wet`/`dry` são **lidos** com swap `^2` e **gravados** cru.
Parecia bug óbvio.

**Tornar a escrita simétrica piora muito:**

| | leitura swap + escrita crua | ambas com swap |
|---|---|---|
| média | ~9% | 30,92% |
| listas acima de 1% | 43–46 | **224 (75%)** |

Volta ao patamar do início da sessão. Revertido.

**Por que a assimetria é correta:** são operações diferentes. A leitura acessa
uma meia-palavra individual, e nossa RDRAM word-swapped exige `^2` para isso. A
escrita reproduz o que o microcódigo faz com `MOVEMEM`/DMA — cópia de bytes,
sem semântica de halfword. O par assimétrico é o que bate com o oráculo.

> ⚠️ Este é o tipo de código que alguém "conserta" numa leitura rápida e
> quebra o áudio inteiro. O comentário no `rsp.c` avisa explicitamente. Não
> alterar sem medir.

Também elimina o item 1 do 5f. Do que restava naquela lista, sobra o item 2: a
interação entre o `>> 3` do passo e o guarda `if (step_l)`.

---

## 5h. ⚠️ Teste auditivo: as correções 5c/5d são INAUDÍVEIS

Gerado `lab\ab_corrigido.wav` com o código corrigido, nas mesmas condições do
`ab_base.wav` (reverb ligado, sem paliativo). Escuta do usuário:

> "ainda tem muito chiado... o `ab_no_wet.wav` tem menos chiado"

E o número concorda:

| | ab_base (antes) | ab_corrigido (depois) | Project64 |
|---|---|---|---|
| DC médio L | +278,6 | **+297,9** | −19,3 |

**O offset DC não se moveu.** As correções 5c e 5d reduziram a divergência
contra o microcódigo de 223 para ~45 listas — uma melhora real de fidelidade —
mas **não atacam o defeito audível**.

### Consequência para a prioridade

A divergência contra o microcódigo e o chiado audível são **coisas
diferentes**. Otimizar a primeira não move a segunda. Isso não invalida as
correções (o código ficou mais fiel), mas invalida a estratégia de perseguir
os ~45 casos residuais esperando que o som melhore.

**O que se ouve é o piso de corrente contínua**, e a medição de 5 já localizou
a origem: ~70% do DC vem do caminho wet (+278 com ele, +84 sem). É por isso
que o `no_wet` soa melhor — remove a fonte, junto com o reverb legítimo.

**Alvo correto:** a origem do DC no caminho wet, não os casos residuais de
divergência. Ver 5i.

---

## 5i. O DC é proporcional à atividade, não constante

Medido com `tools\dc_no_tempo.py` — DC médio por segundo:

| trecho | nosso | no_wet | Project64 |
|---|---|---|---|
| 0–9 s | ~0 (oscila) | ~0 | ~−20 |
| **10–18 s** | **600–935** | 166–430 | ~−20 |
| 19–23 s | ~−20 | ~0 | ~−20 |

**O offset não é constante — ele aparece na janela de 10–18 s e some depois.**
É exatamente a janela onde o chiado piora e onde a contagem de vozes dobra
(ADPCM 18→41, medido em 5).

Consequências:

1. **Não é viés por amostra.** Um viés uniforme daria DC constante. Este é
   **proporcional à atividade**: cada voz contribui com um offset, e eles
   somam.
2. **O wet amplifica, não origina.** O `no_wet` reduz para ~1/3 (batendo com
   os 70% medidos), mas não zera. O DC já vem das vozes; o anel de reverb o
   realimenta e multiplica.
3. **O Project64 fica plano em ~−20** durante toda a faixa, inclusive na
   janela cheia. Não é característica da trilha.

**Próximo experimento:** isolar uma voz e medir a média do PCM que ela produz.
Se o ADPCM decodificado já tiver média não-nula, a origem está no decodificador
(inicialização do preditor ou arredondamento), não na mistura. Existe a chave
`WPJ2_AUDIO_VOICE=<n>` que deixa passar só uma voz — feita exatamente para
isso.

---

## 5k. ⚠️ INVALIDAÇÃO — a melhora de 5c a 5j não existiu

O teste auditivo não bateu com o número: a métrica dizia 5× melhor, o ouvido
dizia que o chiado continuava. Investigando a discrepância, o RMS apareceu 10×
menor, e a instrumentação dos ganhos deu a resposta:

```
[ganhos] swap: dry=0 wet=0 | cru: dry=26698 wet=18997
```

**A alteração de 5c zerava os dois ganhos mestres.** Toda voz que retoma
estado (`flags=0x08`) ficava muda; só as vozes em `A_INIT` sobravam. A
divergência caiu de 223 para ~45 listas porque a saída virou quase silêncio, e
silêncio diverge pouco em valor absoluto.

Verificação da reversão — os três números voltam ao original:

| | DC total | DC 10–18 s | RMS |
|---|---|---|---|
| `ab_base` (origem) | 278,6 | 761,8 | 4892 |
| `ab_semround` (base quebrada) | −24,1 | −41,6 | **466** |
| `ab_revertido` (agora) | 278,1 | 716,6 | 4915 |

População após reverter: **25,31% de média, 223 listas (74%)** — o valor de
origem.

### O que cai junto

- **5c** — a leitura com swap `^2` está errada. A leitura crua é a correta, e
  o comentário original do código, que foi sobrescrito, estava certo.
- **5d** (rampas em `int32`) — **revalidada e mantida.** Refeita sobre a base
  correta:

  | | média | listas >1% | DC | RMS |
  |---|---|---|---|---|
  | `int32` | **25,31%** | 223 | 278,1 | 4915 |
  | `int64` | 30,92% | 224 | 282,5 | 4848 |

  Ganha 5,6 pontos de média, acima do ruído de ~1,5, e desta vez com o
  contrapeso: RMS e DC praticamente idênticos, logo não é atenuação
  disfarçada. A contagem não move — a mudança age nos casos extremos, não em
  quantos divergem.
- **5f, 5g** — as reversões continuam válidas como reversões (aquelas
  variantes de fato pioravam), mas os números absolutos citados não valem.
- **O teste do `+0x4000`** — o DC de −24 daquele experimento veio do sinal
  estar silenciado, não de correção de viés. Precisa ser refeito.

### O erro de método

**Divergência foi medida sem nível.** Uma métrica que premia silêncio precisa
de contrapeso, e o RMS só foi consultado quando o teste auditivo contradisse o
número. Sem esse teste, o artefato teria ficado registrado como progresso.

A regra que fica: **nenhuma medição de divergência vale sozinha.** RMS e DC
entram junto, sempre.

### O que sobrevive

- A instrumentação (bisseção, modo população, oráculo do microcódigo).
- O ruído do instrumento, em ordem de grandeza.
- **5i** — o DC é proporcional à atividade, não constante. Medido no
  `ab_base`, que é base boa.
- **5j** — vozes isoladas têm DC entre −7 e −14 (centradas), mas somadas dão
  +278. O offset é criado pela soma. Também medido sobre base boa.

Esses dois últimos continuam sendo as pistas mais fortes, e apontam para a
**acumulação** — não para o decodificador nem para a leitura de estado.

---

## 6. Próximo passo

A captura do Project64 está **descartada** (ver 5b): o endereço do estado está
errado, e nenhuma interpretação de bytes conserta isso. Duas opções:

**(a) Consertar a captura** — fazer o script do PJ64 resolver o endereço do
estado do mesmo modo que `acmd_address()` no `rsp.c`. Só vale se a terceira
opinião do PJ64 for considerada necessária.

**(b) Ignorar o PJ64 e usar só o oráculo do microcódigo real**, que já está
validado (`rc=0` com a lista inteira) e é referência melhor — o PJ64 também é
HLE aproximado. Esta é a opção recomendada: a comparação de três vias existia
para checar se o nosso oráculo estava quebrado, e a validação com a lista
completa já respondeu que não.

Seguindo por (b), o trabalho é iterar no `acmd_envmix` contra a métrica de
população, uma alteração por vez:

```
tools\sonda_audio.cmd 25 -2 5C0:F80     → media=18,21% e a distribuicao
```

Candidatos ainda não testados, na ordem em que eu tentaria:

1. A ordem de `value += step` em relação ao teste de chegada ao alvo.
2. O tratamento do `A_INIT` (flags `0x09` contra `0x08`) na primeira lista de
   cada voz.
3. A largura dos acumuladores: `ramp_t` do PJ64 usa 64 bits (os `(int32_t)`
   nos `save_buffer` denunciam isso), e nós também — mas o microcódigo real
   trabalha em 32 com transbordo.

Um aviso que vale mais que os três: `wet`/`dry` (16 bits, em +0 e +4) e
`exp_rate` (+16) nunca foram confirmados contra referência externa. Nosso
runtime os lê com valores plausíveis (`18997`, `26698`, `65505`), mas isso é
autoconsistência, não validação. Se algum deles estiver errado, o defeito é
sistêmico e explicaria os 74% de listas divergentes melhor que qualquer ajuste
de rampa.

O **replay determinista** — alimentar nosso HLE com entrada gravada e comparar
a saída do `SAVEBUFF` — continua sendo a ferramenta certa para eliminar a
dependência do instante de execução. Só precisa de uma captura com o endereço
correto, ou de ser montado a partir do próprio oráculo do microcódigo em vez
do Project64.

---

## 7. Armadilhas de método já pagas

Não repetir:

1. **Comparar listas por índice de tarefa.** O índice desliza entre execuções
   porque o timing varia; "tarefa 30" não é a mesma lista em duas rodadas.
2. **Confiar em nome de arquivo.** `wpj2_audio_oracle.wav` é cópia do oráculo
   do PJ64, não a nossa saída. Gerou conclusões inteiras que precisaram ser
   retiradas.
3. **Tratar o Project64 como verdade.** Ele também é HLE aproximado. A verdade
   é o microcódigo recompilado da própria ROM.
4. **Medir áudio com o `RODAR.bat`.** Fast mode pula a síntese.
5. **Confiar no harness sem validá-lo** contra um caso conhecido (lista
   inteira, `rc == 0`).
6. **Tratar falha de execução como divergência.** `rc != 0` significa "não
   comparável", não "diferente".
7. **Raciocinar por plausibilidade em vez de medir.** Das hipóteses levantadas
   lendo código, nenhuma se confirmou; todas as pistas úteis vieram de
   instrumentação.
