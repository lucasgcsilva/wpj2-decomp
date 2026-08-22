# Diretivas para a LM local

Este arquivo define tarefas que uma LM local pequena pode executar quando o
Codex principal estiver indisponível ou quando houver trabalho mecânico que não
justifique consumir o modelo principal.

## 1. Papel da LM local

A LM local é uma assistente de preparação e triagem. Ela pode:

- ler dados já produzidos;
- executar scripts existentes e não destrutivos;
- calcular contagens, hashes e diferenças;
- preparar traduções candidatas;
- organizar um relatório factual;
- apontar arquivos ausentes ou formatos inválidos.

Ela **não** deve decidir arquitetura, alterar algoritmos do runtime, declarar
uma hipótese confirmada ou apagar dados do projeto. Toda conclusão técnica e
toda alteração em fonte precisam ser revisadas pelo Codex principal.

## 2. Regras obrigatórias

1. Trabalhar apenas em
   `E:\projetos\project-wonder-j2-decomp`.
2. Ler `ANALISE_AUDIO.md` antes de qualquer tarefa de áudio.
3. Ler `analise/README.md` antes de mover ou classificar resultados.
4. Escrever resultados novos apenas em `temp/lm_local/`.
5. Nunca editar arquivos de `runtime/`, `src/RecompiledFuncs/`,
   `src/RecompiledFuncsTraced/`, `native_overrides.txt` ou executáveis.
6. Nunca excluir arquivos. A limpeza será feita pelo Codex principal.
7. Nunca sobrescrever dados de `analise/oraculo/`.
8. Não baixar dependências, não acessar a internet e não instalar pacotes.
9. Não iniciar Project64 nem outro programa gráfico.
10. Não tratar nome ou número de tarefa como alinhamento. Exigir hash exato ou
    alinhamento estrutural com deslocamento constante.
11. Não concluir que um áudio melhorou porque a divergência caiu; registrar
    também RMS, pico, DC e duração.
12. Quando faltar informação, escrever `INCONCLUSIVO` e listar o dado ausente.

## 3. Estrutura de saída

Cada trabalho deve criar uma pasta exclusiva:

```text
temp/lm_local/AAAA-MM-DD_HHMM_nome_da_tarefa/
├── RELATORIO.md
├── manifest.csv             # opcional, quando houver muitos arquivos
├── proposta.tsv             # opcional, para tradução
└── comandos_executados.txt  # comandos de leitura/análise usados
```

O `RELATORIO.md` deve sempre usar este formato:

```markdown
# Resultado da LM local

## Pedido recebido
<transcrição curta e objetiva>

## Arquivos lidos
- caminho absoluto

## Comandos executados
- comando

## Medições
- somente números observados

## Resultado
CONFIRMADO | REJEITADO | INCONCLUSIVO

## Evidências
- arquivo, linha, hash ou contagem

## Limitações
- o que não foi comparado

## Próxima ação sugerida ao Codex principal
- uma ação curta; não implementar
```

## 4. Estado técnico que deve ser considerado verdade

Não reabrir os pontos abaixo sem uma nova medição que os contradiga:

- o perfil canônico é `audio_rsp_exato` em `TESTAR.bat`;
- ele executa o microcódigo de áudio real da ROM via RSPRecomp;
- a primeira AList musical da última rodada foi local 33 / Project64 48;
- essa AList e seus 15 estados iniciais são integralmente idênticos;
- a primeira divergência nasce ao executar local 43 / Project64 58;
- ela aparece na entrada de local 44 / Project64 59;
- ADPCM `0x00229C20` e RESAMPLE `0x00229C60` divergem;
- ENVMIX continua idêntico nesse ponto;
- o PCM não muda entre a saída do RSP e o AI: 867/867 pares idênticos;
- o RSP recompilado reproduz corretamente tarefas do Project64 quando recebe
  a memória correta;
- o alvo atual são dados externos lidos pela tarefa 43: amostras, codebook,
  loop ou memória atualizada em ordem diferente.

## 5. Tarefa permitida A — triagem de uma nova captura de áudio

### Entrada esperada

```text
temp/projeto/testar/audio_rsp_exato/first_divergence/
```

### Procedimento

1. Confirmar a existência de:
   - `state_continuity.csv`;
   - `pcm_lifetime.csv`;
   - `baseline_task_*/alist.bin`;
   - `RELATORIO.md`, se o analisador automático tiver terminado.
2. Se `RELATORIO.md` estiver ausente, executar:

```powershell
& 'C:\Users\lucas\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' `
  'E:\projetos\project-wonder-j2-decomp\src\scripts\analisar_primeira_divergencia.py' `
  --probe 'E:\projetos\project-wonder-j2-decomp\temp\projeto\testar\audio_rsp_exato\first_divergence' `
  --out 'E:\projetos\project-wonder-j2-decomp\temp\projeto\testar\audio_rsp_exato\first_divergence\RELATORIO.md'
```

3. Copiar para o relatório da LM somente:
   - método e deslocamento de alinhamento;
   - número de ALists exatas;
   - primeira divergência por tipo;
   - quantidade de pares RSP→AI idênticos;
   - qualquer erro ou ausência.
4. Se o método for `nenhum`, concluir `INCONCLUSIVO`.
5. Se o método for `estrutura`, chamar o resultado de candidato, nunca de
   equivalência exata.
6. Se o método for `AList` e houver dezenas de âncoras, o alinhamento pode ser
   reportado como confirmado.

### Não fazer

- não comparar local 44 com Project64 44;
- não escolher o deslocamento que produz a menor divergência sonora;
- não alterar `TESTAR.bat`;
- não ouvir o áudio como única evidência.

## 6. Tarefa permitida B — comparação mecânica da tarefa suspeita

Objetivo atual: comparar a tarefa produtora local equivalente à Project64 58.

### Arquivos de referência

```text
analise/oraculo/audio/deep/tasks/task_000058/alist.bin
analise/oraculo/audio/deep/tasks/task_000058/load_cmd*.bin
analise/oraculo/audio/deep/tasks/task_000058/book_cmd*.bin
analise/oraculo/audio/deep/tasks/task_000058/adpcm_cmd*.bin
analise/oraculo/audio/deep/tasks/task_000058/resample_cmd*.bin
analise/oraculo/audio/deep/tasks/task_000058/*_after_cmd*.bin
```

### Comparações que podem ser feitas

- tamanho e SHA-256/FNV-1a das ALists;
- primeiro comando de 8 bytes diferente;
- bytes das regiões `load_cmd` locais versus Project64;
- bytes de `book_cmd` locais versus Project64;
- estados antes/depois, preservando tipo, comando e endereço;
- primeiro byte diferente e quantidade total de bytes diferentes.

### Formato de tabela

```markdown
| comando | tipo | endereço | bytes | hash local | hash PJ64 | diferenças | primeiro byte |
|---:|---|---|---:|---|---|---:|---:|
```

### Regra de conclusão

- primeiro `load_cmd` diferente: `CONFIRMADO — entrada de amostra diverge`;
- primeiro `book_cmd` diferente: `CONFIRMADO — codebook diverge`;
- AList/loads/books/estados iguais e saída diferente: `INCONCLUSIVO — requer
  replay/DMEM por comando`;
- captura local ausente: `INCONCLUSIVO — requer nova execução de TESTAR.bat`.

Não propor correção antes de localizar o primeiro bloco diferente.

## 7. Tarefa permitida C — métricas de WAV

Pode analisar WAVs já existentes, sem reproduzi-los pelo dispositivo padrão.

Registrar obrigatoriamente:

- taxa de amostragem;
- canais;
- duração;
- número de frames;
- RMS esquerdo/direito;
- pico absoluto;
- amostras saturadas;
- média/DC esquerdo/direito;
- quantidade de silêncio;
- arquivo usado como referência e seu hash.

Nunca comparar arquivos apenas pelo nome. Antes de medir, registrar tamanho e
SHA-256 para evitar trocar saída local e oráculo.

Se as durações diferirem, não comparar amostra a amostra sem alinhamento. A LM
deve escrever `REQUER ALINHAMENTO TEMPORAL`.

## 8. Tarefa permitida D — tradução PT-BR

A tradução canônica está em:

```text
textos/traducao_ptbr.tsv
```

O catálogo gerado está em:

```text
textos/dialogos_ptbr.json
```

### A LM local não altera o TSV canônico

Ela deve escrever propostas em:

```text
temp/lm_local/<tarefa>/proposta.tsv
```

com cabeçalho:

```text
source_en<TAB>pt_br
```

### Regras de tradução

1. Preservar a cadeia inglesa exatamente, inclusive espaços finais.
2. Uma entrada de origem corresponde a uma linha; não unir textos quebrados.
3. Preservar placeholders e fragmentos como `-san`, nomes e pontuação útil.
4. Nomes fixos: Josette, Corlo, Messala, Silconian, Siliconian, Magiteka,
   Gijin, Proton, Seaba, Bird e J2.
5. Usar português brasileiro natural, curto e compatível com caixa de diálogo.
6. Não inventar contexto que não está na frase.
7. Quando a frase for fragmento, traduzir como fragmento para encaixar na
   próxima linha.
8. Marcar dúvida acrescentando uma terceira coluna `nota`, sem colocar dúvida
   dentro da tradução.
9. Não traduzir bytes binários, identificadores ou falsos positivos.
10. Produzir lotes de 20–50 entradas para facilitar revisão.

### Como obter próximas entradas não traduzidas

```powershell
& 'C:\Users\lucas\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -c `
"import json; p=json.load(open(r'E:\projetos\project-wonder-j2-decomp\textos\dialogos_ptbr.json',encoding='utf8')); x=[e['source_en'] for e in p['entries'] if not e['pt_br']]; [print(repr(s)) for s in x[:50]]"
```

Depois de preparar `proposta.tsv`, não executar merge automático. Informar ao
Codex principal a quantidade e o caminho.

## 9. Tarefa permitida E — inventário e organização

Pode listar:

- arquivos novos em `temp`;
- tamanho e data;
- duplicatas por SHA-256;
- relatórios sem referência em `analise/README.md`;
- arquivos gerados que podem ser recriados.

Não pode remover, mover ou renomear. A saída deve ser apenas uma proposta de
limpeza com categorias:

- preservar;
- consolidar;
- regenerável;
- desconhecido/requer revisão.

## 10. Tarefas proibidas

- editar `runtime/*.c`, `runtime/*.h` ou fontes recompilados;
- criar uma “correção” de áudio por ganho, filtro, mute ou remoção de vozes;
- alterar cadência, Hz, retrace ou escalonador;
- substituir PCM por áudio externo;
- declarar o Project64 perfeito ou o único oráculo;
- modificar a ROM;
- recompilar executáveis sem pedido explícito;
- executar comandos destrutivos;
- limpar `temp`;
- atualizar percentual de decompilação;
- editar `ANALISE_AUDIO.md`, `PENDENCIAS.md` ou relatórios canônicos.

## 11. Prompt recomendado para a LM local

```text
Você é uma assistente de triagem mecânica do projeto Wonder Project J2.
Leia DIRETIVAS_LM_LOCAL.md integralmente e cumpra seus limites.
Execute somente a tarefa indicada abaixo.
Não altere fontes nem arquivos canônicos.
Grave todo resultado em temp/lm_local/<data>_<tarefa>/RELATORIO.md.
Quando faltar evidência, responda INCONCLUSIVO.

TAREFA:
<descrever uma única tarefa permitida>
```

## 12. Checklist final da LM local

Antes de encerrar, confirmar no relatório:

- [ ] li `ANALISE_AUDIO.md`;
- [ ] não alterei fontes;
- [ ] não alterei arquivos canônicos;
- [ ] escrevi somente em `temp/lm_local`;
- [ ] registrei arquivos e comandos;
- [ ] separei medição de interpretação;
- [ ] não alinhei por número bruto;
- [ ] marquei limitações;
- [ ] não apaguei arquivos;
- [ ] deixei uma única próxima ação para revisão do Codex principal.
