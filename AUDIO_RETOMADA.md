# Áudio — estado da investigação do chiado

Documento de retomada. O áudio **funciona**: toca a música correta, com chiado
sobreposto. Não é bloqueador para o resto da decompilação.

## Métrica de referência

```
tools\sonda_audio.cmd 25 -2 5C0:F80
```

Compara nosso HLE em C contra o microcódigo da ROM recompilado, sobre todas as
listas musicais, e imprime estatística acumulada:

```
[populacao] 300 listas: media=5966 LSB (18.21%), max=65535 (200.00%),
            acima de 1%=223 (74%), falhas=0
```

**18,21% de divergência média** é a linha de base. Qualquer correção deve ser
comparada contra esse número, com a mesma contagem de listas.

## Estabelecido por medição

- O defeito é **anterior à saída de áudio**: o WAV gravado já contém o chiado,
  então WinMM e driver estão descartados.
- **Não é clipping**: nós não saturamos; o Project64 satura mais que nós.
- **O ganho global está correto**: nos percentis altos batemos com o PJ64
  dentro de 0,2 dB.
- Existe **offset DC de ~+280** que o PJ64 não tem (ele fica em −32). Cerca de
  70% dele desaparece com `WPJ2_AUDIO_NO_WET=1`, ~10% com
  `WPJ2_AUDIO_BYPASS_POLEF=1`.
- O piso de ruído **não é ruído branco**: aspereza 0,055 (grave e
  correlacionado), e nossa janela mais silenciosa nunca desce de RMS 252,
  contra 5,1 do PJ64.
- **74% das listas divergem acima de 1%.** O defeito é abrangente, não caso de
  borda — isso invalidou várias análises feitas sobre listas isoladas.

## Descartado (verificado contra o Project64 e/ou o microcódigo real)

| Hipótese | Como caiu |
|---|---|
| ABI errada (NAUDIO) | SETBUFF=198/AList prova ABI1 |
| INTERLEAVE divergente | Endereços e laço idênticos ao PJ64 |
| Estado do ENVMIXER sem word-swap | Offsets batem com o PJ64 |
| SETBUFF por igualdade vs bit | Só 0x00 e 0x08 ocorrem; ramos corretos |
| SETVOL por igualdade | Nosso `case 9` já usa teste de bit |
| Destinos do ENVMIXER não configurados | dry_l/dry_r/wet_l/wet_r corretos e distintos |
| POLEF como fonte do DC | Responde por só 10% |
| Acumulador do ENVMIXER | `sample_mix` idêntico ao do PJ64 |
| MIXER | `alist_mix` idêntico |
| Guarda `if (step)` do envelope | Testado: piora de 18,21% para 33,08% |

## Pista viva

Vozes com `target=0` e `value≠0` (em decaimento) têm `exp_seq − value = 3`,
que o `>> 3` trunca para zero, travando o guarda: `exp_seq` nunca mais decai e
a voz congela no ganho corrente em vez de sumir. O microcódigo real, no mesmo
estado, leva a voz a zero e **zera o bloco de 80 bytes** (libera a voz).

O travamento é real e observado, mas corrigir só o guarda **piora** o número —
logo há algo mais junto, provavelmente na atualização de `exp_seq` entre
listas.

## Ferramentas construídas

- **Bisseção diferencial** (`WPJ2_BISSECAO`): acha por busca binária o primeiro
  comando cuja DMEM diverge do microcódigo real. Trunca a lista transformando
  comandos em SPNOOP (truncar por `data_size` fazia o microcódigo abortar e
  o harness lia isso como divergência).
- **Limiar de magnitude** (`WPJ2_BISSECAO_LIMIAR`, padrão 64): sem ele a busca
  para num erro de 1 LSB inaudível e mascara erros grandes.
- **Faixa de comparação** (`WPJ2_BISSECAO_FAIXA`, hex `ini:fim`).
- **Hash de conteúdo**: nomeia listas de forma estável. Atenção: dá nome
  estável mas **não** alvo reproduzível — a mesma lista não recorre entre
  execuções. Por isso existe o modo população (`WPJ2_BISSECAO=-2`).
- **Sonda de áudio** (`tools\sonda_audio.cmd`): o `RODAR.bat` roda quase todo
  com `WPJ2_AUDIO_FAST=1`, que **pula ADPCM/RESAMPLE/ENVMIX** — ele não serve
  para investigar som.
- **Replay do PJ64** (`Scripts\wpj2_audio_replay_oracle.js`): captura AList,
  estados de entrada e saídas. Já executado; dados em
  `analise\oraculo\audio\replay\`.

## Próximo passo

Terminar de interpretar os estados capturados do PJ64. O campo `value` (+32)
já lê certo — ganhos 6149/6213/6475, mesma faixa dos 6096 do nosso runtime, o
que indica que endereço e layout dos campos de 32 bits estão corretos. Faltam
`wet`/`dry` (16 bits, em +0 e +4) e `exp_rate` (+16), que nenhuma das quatro
combinações de endianness/swap tornou plausíveis.

Com isso resolvido, dá para montar o replay determinista: alimentar nosso HLE
com a mesma entrada gravada e comparar a saída do `SAVEBUFF` — comparação que
não depende de reproduzir o instante da execução.

## Armadilhas já pagas (não repetir)

- Comparar listas por índice de tarefa: o índice desliza entre execuções.
- Confiar em `wpj2_audio_oracle.wav` como sendo nossa saída — é cópia do PJ64.
- Validar contra o Project64 e concluir "igual, logo certo": o PJ64 também é
  HLE aproximado; a verdade é o microcódigo recompilado.
- Usar o `RODAR.bat` para medir áudio (fast mode pula a síntese).
- Confiar no harness sem validá-lo contra um caso conhecido.
