# Sonda passiva da primeira divergência de áudio

Data: 20/08/2026.

## Objetivo

Localizar a primeira diferença válida entre a execução recompilada e o
Project64 sem alterar a cadência que está sendo medida. A sonda registra:

- hash integral das ALists;
- estados ADPCM/RESAMPLE/ENVMIX antes e depois do RSP;
- alterações entre ALists;
- PCM ao terminar o RSP e na submissão ao AI;
- conteúdo bruto apenas nas fronteiras selecionadas.

## Resultado consolidado

Na execução interativa de 31,387 segundos:

- 868 tarefas musicais monitoradas;
- 370 ALists byte a byte idênticas ao Project64;
- deslocamento exato e constante: `N+15`;
- primeira AList musical local 33 = Project64 48;
- AList inicial idêntica, com 5.488 bytes;
- 15/15 estados iniciais idênticos;
- local 43 = Project64 58, com AList idêntica;
- os estados entram iguais na tarefa 43;
- a execução da tarefa 43 produz os primeiros históricos divergentes;
- a diferença aparece na entrada de local 44 / Project64 59;
- ADPCM divergente: `0x00229C20`;
- RESAMPLE divergente: `0x00229C60`;
- ENVMIX ainda idêntico.

Hashes posteriores à tarefa produtora:

| estado | local após 43 | Project64 após 58 |
|---|---|---|
| ADPCM `0x229C20` | `1F3FA75BFAAB2B9E` | `1F69D539F84472E6` |
| RESAMPLE `0x229C60` | `0065D99B6231C229` | `C582A2FAAD955971` |

`changed_between=0` nos dois estados: a CPU não os alterou entre as tarefas.

## Continuidade do PCM

- buffers produzidos pelo RSP: 868;
- buffers pareados RSP→AI: 867;
- buffers pareados idênticos: 867/867;
- buffers pareados alterados: zero;
- submissões sem par: 39, pertencentes ao bootstrap anterior à primeira AList
  musical monitorada.

Está descartada corrupção depois do RSP.

## Movimentação de estados

Foram observadas 78 mudanças entre tarefas; 39 repetiam exatamente o hash
posterior de outro slot. Isso confirma que a ROM realoca históricos entre
vozes. Mudança de endereço não deve ser tratada automaticamente como defeito.

## Fronteira causal

AList e estados de entrada iguais com ADPCM/RESAMPLE de saída diferentes
significam que a tarefa consome outro dado divergente. Os candidatos mensuráveis
são amostras `LOADBUFF`, livros `LOADADPCM`, loop e regiões gravadas/lidas
dentro da própria AList.

A sonda seguinte captura uma única vez a décima tarefa após a primeira AList
musical, correspondente à tarefa produtora 43/58 na sequência alinhada:

- `alist.bin`;
- `rdram_before.bin` lógica completa;
- estados antes/depois;
- PCM final;
- metadados.

`src/scripts/comparar_entrada_audio_suspeita.py` compara entradas estáticas
contra `analise/oraculo/audio/deep/tasks/task_000058`. LOADBUFFs que dependem
de um SAVEBUFF anterior são marcados como dinâmicos e exigem replay por comando.
