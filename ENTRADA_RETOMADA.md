# Entrada de controle — estado da investigação

> # ✅ 23/08 — RESOLVIDO: O PIF SÓ EXECUTAVA A FITA NA ESCRITA
>
> **Causa raiz.** `osContStartReadData` monta e escreve a fita de comandos
> **uma única vez**. Depois disso ela só faz a leitura de volta
> (`tools/libreultra/src/io/contreaddata.c`):
>
> ```c
> if (__osContLastCmd != CONT_CMD_READ_BUTTON) {
>     __osPackReadData();
>     __osSiRawStartDma(OS_WRITE, &__osContPifRam);   // 1a vez só
>     osRecvMesg(mq, NULL, OS_MESG_BLOCK);
> }
> __osSiRawStartDma(OS_READ, &__osContPifRam);        // toda vez
> __osContLastCmd = CONT_CMD_READ_BUTTON;
> ```
>
> No hardware a PIF RAM **retém** a fita e o PIF a **reexecuta a cada
> leitura**, então o `OS_READ` sozinho já traz botões novos. O nosso
> `func_800CD4F0` chamava `pif_process()` só em `dir=1`, e portanto devolvia
> para sempre o retrato tirado na última escrita.
>
> **Medição que isolou o defeito** (relatório periódico `[pif-per]`, de um em
> um segundo, disparado pelo relógio e não por leitura):
>
> ```
> antes:  t=2000  leituras=10    si_w=29  si_r=35
>         t=40000 leituras=10    si_w=29  si_r=1176   <- si_w congelado
> depois: t=2000  leituras=25    si_w=29  si_r=35
>         t=40000 leituras=1114  si_w=59  si_r=1134
> ```
>
> O jogo lia 30 vezes por segundo uma resposta de dois segundos atrás.
>
> **Correção:** `pif_process()` também em `dir=0`, antes do `copy_bswap` de
> volta (`runtime/pif.c`). É idempotente: `rx` é remascarado com `& 0x3F`, e o
> bit `NO_DEVICE` posto em canais ausentes não se acumula.
>
> **Resultado, perfil `input_tardio` (START aos 21 s):**
>
> ```
> [ctrl-raw] #612..#638  pad=1000  raw+0x04=10000000  estado=8/26
> estado final: 11/24    (sem entrada o jogo termina em 12/50)
> ```
>
> Isto também explica por que `WPJ2_BUTTONS` segurado desde o boot "funcionava"
> e apertar no título nunca funcionava: só a janela das ~10 leituras iniciais
> era real.
>
> **Armadilha de método.** Todo log de entrada até aqui era disparado *por
> leitura*. Um relatório assim desaparece junto com o sintoma — o silêncio a
> partir de 2,3 s foi lido durante ciclos como "o roteiro não produz botão",
> quando era "ninguém está mais perguntando". Sonda de estagnação tem que ter
> relógio próprio.

> ## ✅ 22/08 — A CADEIA DE ENTRADA ESTÁ CORRETA (confirmado; era só metade)
>
> Tudo abaixo continua verdadeiro, mas media apenas os primeiros 2,3 s — que é
> exatamente a janela em que a fita ainda era reescrita. Daí a conclusão de que
> "o transporte não está quebrado": estava, só que fora da janela medida.
>
> Verificado de ponta a ponta com os símbolos da decompilação de referência
> (`tools/wonder-source`), cujos endereços conferem com os nossos:
>
> ```
> PIF entrega            0x1000  ✓
> gContPad.button        0x1000  ✓   (osContGetReadData funciona)
> gContPad.errno         0x00    ✓
> gControllerRaw +0x04   0x1000  ✓   (botões mantidos)
> gControllerRaw +0x0A   0x1000  ✓   (borda de pressionamento)
> ```
>
> `Update_ControllerRaw` (`0x80098D24`) é chamada continuamente (600+ vezes
> numa execução de 25 s) e propaga corretamente.
>
> **Nada abaixo desta linha sobre "a entrada não chega" continua válido.** O
> defeito, se houver, está na lógica de estado do jogo que consome
> `gControllerRaw` — não no transporte.
>
> Armadilha que custou vários ciclos: `gControllerRaw + 0` é **zerado de
> propósito** a cada quadro (`sw $zero, 0x0($t9)` em `0x80098DFC`). A estrutura
> tem 40 bytes por controle; observar só o offset 0 dá zero sempre e parece
> defeito. Os botões ficam em `+0x04` e `+0x0A`; o analógico em `+0x10`/`+0x14`,
> escalado por 80.0.
>
> Sonda: `src\scripts\sonda_input.cmd 25 0x1000`

O START não responde. O objetivo da etapa era: título → START → menu "Start"
→ A → seleção de save. Não foi concluída.

O teclado **funciona** até a fronteira do runtime. O que falta é o caminho de
dentro do jogo.

---

## Como testar

```
TESTAR.bat            janela + teclado no perfil atual
TESTAR.bat audio      teste com o perfil de áudio selecionado
```

Teclas: **Enter**=START, **X**/Espaço=A, **Z**=B, **C**=Z, **A**/**S**=L/R,
setas=direcional. F5 captura quadro, F11 alterna voz do áudio.

Ao fechar, o `.bat` resume três números de `temp\projeto\jogar\jogar.log`: teclas que
chegaram ao runtime, leituras que o jogo pediu, e valores que ele recebeu.

---

## Verificado por medição

| Item | Resultado |
|---|---|
| Teclado → `pif_set_buttons` | Funciona. `[elo] escrita: thread=77436 &g_buttons=0x…A890 valor=1000` |
| Uma variável ou duas? | **Uma.** Mesmo endereço e mesma thread nos dois lados |
| Formato da resposta do PIF | Correto — layout joybus (`out[0]`=alto, `out[1]`=baixo, `out[2..3]`=analógico) |
| Valor entregue ao jogo | `0x1000` em toda leitura, quando fixado por `WPJ2_BUTTONS` |
| Detecção do controle | Responde `05 00 02` (padrão, sem pak); o jogo identifica |
| Buffer da fita na RDRAM | `0x1AFB40` |
| VI | 60 Hz, postado uma vez por passagem (`hle.c:1359`) |
| Stubs no caminho de controle | Nenhum — `stubs_motivos.txt` só tem cache/COP0/TLB |

**O defeito observado:** o jogo faz ~11 leituras de controle durante a
inicialização e **para de pedir**. Nenhuma leitura ocorre depois, então
qualquer tecla pressionada no título nunca é consultada.

---

## Hipóteses descartadas

| Hipótese | Como caiu |
|---|---|
| Cadência de 10 Hz é defeito | Não provado. É compatível com a ROM ler a cada 6 quadros por decisão dela |
| Conclusão do SI presa ao retrace | Drenagem implementada; **zero** efeito medido |
| Carga do áudio impedindo escalonamento | Testado com `AUDIO_FAST` ligado e desligado — sem diferença |
| Roteiro gravado engolindo o teclado | Era real (`WPJ2_INPUT_POLLS` vazava sem `setlocal`), corrigido, mas não era *o* defeito |
| `g_buttons` sem `volatile` | Era real, corrigido, mas mesma thread — não era o defeito |
| Duas cópias da variável | Falso: mesmo endereço nos dois lados |
| Identificar funções pelo `ra` | **Método inválido aqui.** Com fibers, o `ra` reflete a retomada, não o chamador |

---

## O caminho indicado: modelo do ultramodern

`tools\N64ModernRuntime-source\ultramodern\src\input.cpp` — a mesma biblioteca
que o Zelda64Recomp usa — **substitui as funções da libultra por nativas**:

```cpp
extern "C" s32 osContStartReadData(RDRAM_ARG PTR(OSMesgQueue) mq) {
    if (input_callbacks.poll_input != nullptr) input_callbacks.poll_input();
    update_poll_time();
    ultramodern::send_si_message();
    return 0;
}

extern "C" void osContGetReadData(OSContPad *data) {
    // preenche direto do callback do host
}
```

Sem PIF, sem fita joybus, sem esperar DMA de SI. O jogo nunca depende do
aperto de mão do hardware — e é por isso que lá a entrada não quebra.

São duas camadas. **A do host nós já temos** (o `WndProc` no `video.c` e o
`g_buttons`). Falta a que substitui a libultra.

---

## ✅ Funções localizadas (passo 1 concluído)

Encontradas buscando `func_800CD4F0` (o `__osSiRawStartDma`) **no código
recompilado**, em `src\RecompiledFuncs\funcs_33.c` — não pelo `ra`, que é inútil
com fibers.

As chamadas aparecem como comentário do endereço original:

```
0x800C5344: jal 0x800CD4F0   com a0 = 1   (escrita)
0x800C5368: jal 0x800CD4F0   com a0 = 0   (leitura)
0x800C55C8: jal 0x800CD4F0   com a0 = 1
0x800C5624: jal 0x800CD4F0   com a0 = 0
```

Cruzando com os limites de função em `funcs.h`:

| função | intervalo | contém |
|---|---|---|
| **`func_800C51D0`** | `0x800C51D0`–`0x800C53C7` | o 1º par (escrita+leitura) |
| **`func_800C5590`** | `0x800C5590`–`0x800C5653` | o 2º par |

Duas funções, cada uma com o par escrita→leitura — a assinatura de
`osContStartQuery` e `osContStartReadData` na libultra. **São as funções a
substituir por nativas**, no modelo do `ultramodern`.

**Falta distinguir qual é qual.** Uma execução instrumentada resolve: a que é
chamada repetidamente durante o jogo é `osContStartReadData`; a que roda uma
vez na inicialização é `osContStartQuery`. Também falta `osContGetReadData`,
que faz o parse da fita para o `OSContPad` e não chama o SI (por isso não
aparece nesta busca) — provavelmente vizinha das duas.

---

## Próximos passos

1. ~~Identificar as funções~~ — **feito**, ver acima. Falta apenas distinguir
   qual das duas é `osContStartReadData` e localizar `osContGetReadData`.
   Métodos originalmente propostos (mantidos por referência):
   - por assinatura, como foi feito o `libultra_names.txt`: uma função monta a
     fita de 64 bytes e chama `__osSiRawStartDma` com `dir=1`; a outra lê o
     `OSContPad` do bloco;
   - ou pelo buffer já conhecido: quem lê `0x1AFB40` logo após a
     transferência é o `osContGetReadData`.
2. Escrever as nativas no modelo acima, lendo o `g_buttons`.
3. Registrar em `native_overrides.txt` (o build já usa esse mecanismo para
   `func_800CCAE4` e `func_800CBBB0`).
4. Compilar e testar com `TESTAR.bat`.

O `pif.c` não precisa ser removido — continua servindo à inicialização. Só
sai do caminho crítico da entrada.

---

## Instrumentação disponível

| Marcador | O que mostra |
|---|---|
| `[controle] botoes=0x….` | Tecla registrada pelo `WndProc` |
| `[elo] escrita/leitura` | Thread e endereço de `g_buttons` dos dois lados |
| `[pif] MUDOU botoes=…` | Valor que o jogo recebeu, quando muda |
| `[pif] leitura=N` | Cada leitura pedida pelo jogo |
| `[pif] destino=0x…` | Onde a fita cai na RDRAM |
| `[chamador] …` | Chamador de `__osSiRawStartDma` (pouco útil: ver acima) |

O `[elo]` e o `[chamador]` são temporários — podem sair quando a entrada
funcionar.

---

## Armadilhas já pagas

1. **Medir num arranjo e concluir sobre outro.** A cadência de ~10 Hz veio de
   sondas sem janela e com o áudio pulado; o comportamento real (leituras que
   param) só apareceu na execução interativa hoje centralizada no `TESTAR.bat`.
2. **Interpretar rajada de boot como regime permanente.** As 11 leituras
   iniciais foram lidas como "10 leituras por segundo, continuamente".
3. **Script sem `setlocal`.** `WPJ2_INPUT_POLLS` vazava para o console e
   valia nas execuções seguintes.
4. **`printf` antes da chamada.** O `[controle]` é impresso antes de
   `pif_set_buttons`, então sua presença não prova que a chamada ocorreu.
5. **Propor correção antes de medir o elo.** Três consertos aplicados sem que
   ninguém tivesse verificado que os dois lados falavam da mesma variável.
