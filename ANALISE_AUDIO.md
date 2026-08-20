# Análise consolidada do áudio

Atualizado em 20/08/2026. Este documento contém somente resultados ainda
válidos, limites demonstrados e o próximo experimento. Tentativas rejeitadas
foram removidas do corpo principal; o histórico completo permanece no Git.

## 1. Estado atual

O runtime reproduz a música original de forma reconhecível, mas ainda há
trechos intermitentes de chiado/aspereza e alguns estalos. Há passagens limpas,
inclusive uma música posterior de flauta, o que mostra que a saída não está
globalmente danificada.

O defeito é bloqueador para o projeto. A investigação deixou de ser tentativa
auditiva: existe agora uma fronteira reproduzível e alinhada ao Project64.

### Primeira divergência válida

Na captura interativa mais recente:

- execução local: 868 tarefas musicais monitoradas;
- alinhamento exato com o Project64: deslocamento `N+15`;
- ALists byte a byte idênticas: 370;
- primeira AList musical local 33 = Project64 48;
- tamanho das duas: 5.488 bytes;
- AList inicial: integralmente idêntica;
- estados iniciais ADPCM/RESAMPLE/ENVMIX: 15/15 idênticos;
- tarefas local 33–43 entram alinhadas;
- local 43 = Project64 58, com AList exata (`0EF781EE040FAE93`);
- a tarefa 43 produz os primeiros históricos diferentes;
- a divergência fica visível na entrada de local 44 / Project64 59;
- primeiro ADPCM divergente: `0x00229C20`;
- primeiro RESAMPLE divergente: `0x00229C60`;
- ENVMIX continua idêntico nesse ponto.

Hashes ao terminar a tarefa produtora 43/58:

| estado | local após 43 | Project64 após 58 |
|---|---|---|
| ADPCM `0x229C20` | `1F3FA75BFAAB2B9E` | `1F69D539F84472E6` |
| RESAMPLE `0x229C60` | `0065D99B6231C229` | `C582A2FAAD955971` |

Não houve alteração desses estados pela CPU entre as duas ALists
(`changed_between=0`). A diferença nasce durante a execução da tarefa 43.

### Interpretação atual

AList e históricos de entrada são iguais, mas ADPCM/RESAMPLE saem diferentes.
O microcódigo recompilado já foi validado offline quando recebe toda a memória
correta. Assim, o alvo atual é algum dado externo lido pela tarefa 43:

- amostra carregada por `LOADBUFF`;
- livro de coeficientes carregado por `LOADADPCM`;
- endereço de loop ou outro bloco apontado pela AList;
- região da RDRAM atualizada em ordem diferente antes da tarefa.

Não alterar ENVMIX, ganho, reverb ou driver antes de comparar esses dados.

## 2. Cadeia de áudio confirmada

```text
CPU recompilada constrói AList ABI1
        ↓
RSPRecomp executa o microcódigo de áudio real da ROM
        ↓
SAVEBUFF grava PCM final na RDRAM
        ↓
AI recebe o mesmo intervalo
        ↓
runtime converte big-endian → little-endian
        ↓
WAV de diagnóstico e saída WinMM
```

O perfil canônico de teste é `audio_rsp_exato`, selecionado por padrão em
`TESTAR.bat`. Ele usa o microcódigo real recompilado; o HLE C permanece apenas
como ferramenta de comparação e fallback histórico.

## 3. Fatos demonstrados

### 3.1 A ROM usa ABI1

Os padrões de `SETBUFF`, ADPCM, RESAMPLE, ENVMIX, MIXER, INTERLEAVE e POLEF
correspondem à ABI1. A recompilação do microcódigo da própria ROM eliminou a
necessidade de inferir a ABI pelo HLE.

### 3.2 O microcódigo recompilado é fiel quando recebe memória correta

Replays offline de tarefas Project64 de alta atividade foram executados pelo
RSP recompilado:

| tarefa Project64 | diferença RMS do PCM | pico |
|---:|---:|---:|
| 358 | 2,18 LSB | 6 LSB |
| 406 | 1,99 LSB | 6 LSB |

Outro replay completo apresentou RMS 3,09 LSB e pico 8 LSB. Estados
ADPCM/RESAMPLE/ENVMIX/POLEF posteriores coincidiram, e o PCM convertido foi
localizado exatamente nos buffers do plugin Project64. Essas diferenças são
inaudíveis e não explicam o chiado ao vivo.

### 3.3 O PCM não é corrompido depois do RSP

A sonda de continuidade identifica todos os fragmentos `SAVEBUFF` que formam
o buffer final e volta a calcular o hash quando o AI o recebe.

Na rodada mais recente:

- buffers produzidos pelo RSP: 868;
- pares RSP→AI encontrados: 867;
- pares byte a byte idênticos: 867/867;
- submissões sem par: 39, todas no bootstrap anterior à primeira AList
  musical monitorada;
- submissões pareadas alteradas: zero.

Portanto a causa está antes do término da síntese, não na fila hospedada, WAV,
conversão ou dispositivo do Windows.

### 3.4 ALists podem variar com a preempção, mas existe uma rota alinhável

Interrupções baseadas no relógio do host mudam o ponto MIPS onde VI/AI/COMPARE
interrompem a CPU e podem mudar a sequência construída pela ROM. Por isso não
se compara tarefa apenas pelo número.

O alinhamento usa, nesta ordem:

1. hash integral da AList;
2. sequência/contagem de opcodes para localizar a vizinhança;
3. estados de entrada como confirmação;
4. deslocamento constante por uma sequência, nunca por um único par.

A execução atual obteve 370 âncoras integrais consecutivas e deslocamento
`+15`, suficiente para aceitar a primeira divergência 43/58.

### 3.5 A cadência AI precisa reproduzir os tamanhos variáveis

Depois do bootstrap, o Project64 produz o ciclo de blocos:

```text
3008, 3008, 2944, 2880, 2880, 2944 bytes
```

O modo `WPJ2_AI_VIRTUAL_CADENCE=1` reproduz essa distribuição. A fila de dois
DMAs e o espelho `DMA_BUSY/FIFO_FULL` foram implementados como sonda, mas não
alteraram o chiado. A correção de fronteira de 8 KiB já existe dentro da
função recompilada equivalente a `osAiSetNextBuffer` e não deve ser duplicada.

### 3.6 A ROM realoca históricos entre slots

Algumas mudanças de estado entre ALists repetem exatamente o hash posterior de
outro endereço. Na rodada atual, 39 de 78 mudanças tinham essa assinatura.
Isso é compatível com realocação de vozes pela ROM. Uma mudança entre listas
não é, sozinha, evidência de corrupção.

### 3.7 O problema não é simples saturação ou ganho mestre

O WAV capturado já contém o defeito, porém não apresenta saturação suficiente
para explicá-lo. Em passagens fortes, o RMS local fica próximo da referência.
Reduzir ganho apenas reduz volume; não remove o chiado. Métricas de divergência
sem RMS e DC são inválidas porque premiam silêncio.

## 4. Referências preservadas

### Project64

- captura profunda: `analise/oraculo/audio/deep/`;
- ALists e estados por tarefa: `analise/oraculo/audio/deep/tasks/`;
- PCM do plugin: `analise/oraculo/audio/deep/ai_plugin/`;
- replays validados: `analise/oraculo/audio/replay/`;
- casos mínimos: `analise/oraculo/audio/validacoes/`.

### Projeto recompilado

- resumo da primeira divergência:
  `analise/projeto/audio_primeira_divergencia.md`;
- configurações e referências externas:
  `analise/projeto/configuracoes_audio.md`;
- resultados novos e descartáveis:
  `temp/projeto/testar/audio_rsp_exato/`.

### Código e ferramentas

- runtime RSP/HLE: `runtime/rsp.c`;
- submissão AI: `runtime/audio.c`;
- microcódigo recompilado: `runtime/rsp_native.cpp` e
  `src/gerado/rsp_audio/`;
- analisador de alinhamento:
  `src/scripts/analisar_primeira_divergencia.py`;
- reconstrutor/replay: `tools/reconstruir_audio_deep_task.py`;
- perfil interativo: `TESTAR.bat`;
- oráculo: `SONDAR_AUDIO_PROJECT64.bat`.

## 5. Regras de medição

1. Nunca alinhar execuções somente pelo índice da tarefa.
2. Nunca concluir melhora usando apenas divergência média.
3. Medir também RMS, pico e DC; silêncio artificial não é correção.
4. `RODAR.bat` com áudio rápido não serve para validar síntese.
5. `rc != 0` no harness significa captura não comparável, não divergência.
6. Não abrir milhares de arquivos durante a execução; grave hashes em buffer e
   preserve bruto apenas na fronteira encontrada.
7. Um teste auditivo aceita ou rejeita fidelidade, mas não localiza a causa.
8. Project64 é referência comportamental; o microcódigo real da ROM é a
   referência da síntese.
9. Toda alteração deve mudar uma variável causal por vez.
10. Resultados novos nascem em `temp`, são resumidos em `analise` e então
    removidos.

## 6. Próximo experimento

Capturar a entrada integral da tarefa que **produz** a primeira divergência:

```text
local 43  ↔  Project64 58
```

Como o índice absoluto pode deslizar entre execuções, a sonda deve selecionar
a décima tarefa após a primeira AList musical alinhada, não um número absoluto.

Preservar somente:

- AList completa antes do RSP;
- RDRAM lógica antes da tarefa;
- estados ADPCM/RESAMPLE/ENVMIX antes e depois;
- PCM produzido;
- metadados de tarefa e hashes.

Comparar os arquivos Project64 `load_cmd*`, `book_cmd*` e os estados da tarefa
58 contra as regiões correspondentes da RDRAM local. O primeiro bloco de
entrada diferente decide o próximo ramo:

- `load_cmd*` diferente: investigar DMA/sample cache e ordem da CPU;
- `book_cmd*` diferente: investigar carregamento do codebook;
- todos os dados iguais, mas saída diferente: reproduzir a tarefa local
  offline com sua RDRAM integral e comparar DMEM por comando;
- saída offline correta: investigar a memória/DMEM entregue ao RSP ao vivo.

Critério de sucesso imediato: identificar o primeiro comando ADPCM/RESAMPLE
que consome bytes diferentes, com endereço, tamanho e os dois hashes. Não
tentar corrigir o som antes dessa prova.

## 7. Critério de aceite final

A correção de áudio só será considerada válida quando:

- a primeira divergência 43/58 desaparecer;
- históricos ADPCM/RESAMPLE permanecerem alinhados nas tarefas seguintes;
- PCM local se mantiver próximo ao microcódigo/oráculo sem atenuação artificial;
- música, vozes e efeitos forem audíveis;
- o chiado não aparecer nos quatro trechos fortes anteriormente marcados;
- cadência visual e progressão da cutscene não regredirem.
