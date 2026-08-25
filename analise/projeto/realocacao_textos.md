# Realocação de textos PT-BR maiores que o recurso inglês

## Limite identificado

O teto não pertence ao renderizador de texto. Ele vinha da estratégia inicial
de substituir a tradução no mesmo endereço da cadeia inglesa. O patch Ryu já
realocou grande parte dos diálogos para a expansão `0x800000..0x880000` da ROM,
mas as cadeias continuam empacotadas; os zeros livres não ficam necessariamente
adjacentes ao texto que precisa crescer.

## Por que a primeira arena quebrou o jogo

`func_80096B38` é um carregador de blocos, não de strings isoladas. Ele devolve
dois cursores sincronizados: a origem e uma cópia alocada no heap. Rotinas como
`func_80096C6C` avançam os dois cursores segundo o mesmo layout de cadeias
separadas por NUL. Reapontar apenas origem ou cópia para uma tradução maior
desalinha o bloco. No A/B experimental isso desviava o subestado `24` para
`50` e elevava as chamadas indiretas de cerca de 3 milhões para dezenas de
milhões.

## Ponto seguro

`func_800319B0` recebe uma string individual e apenas publica seu ponteiro na
fila circular `D_801879D0`. A realocação passou para esse consumidor:

1. o carregador conserva ambos os cursores originais;
2. ao reconhecer uma cadeia longa, prepara um slot estável na faixa física
   `0x00400000..0x006FFFFF`, fora do heap de 4 MB declarado pelo jogo;
3. `func_800319B0` publica o endereço virtual do slot, sem alterar a progressão
   do bloco;
4. textos com controles `E0/E1/E2` usam um anel de slots, porque ocorrências da
   mesma fala podem carregar argumentos diferentes;
5. `func_80090E58(char**)` pode reapontar diretamente o argumento do formatador.

Quando um slot fixo já foi preparado, ele tem prioridade sobre qualquer faixa
de zeros observada após a origem. Isso evita voltar a expandir sobre memória
vizinha.

## Validações de 25/08/2026

- A/B de `WPJ2_REALOCAR=0` contra `2`: ambos terminaram no subestado `24`, com
  respectivamente 3.640 e 3.668 tarefas gráficas e carga equivalente. Antes da
  mudança de consumidor, o modo 2 terminava em `50` e entrava em laço.
- `The Siliconian Empire` foi consumido corretamente por endereço acima de
  4 MB, provando que a etapa final aceita o ponteiro da arena.
- Catálogo sintético: tradução de 69 bytes para uma origem de 21 bytes gerou
  `recurso_ptbr_arena` seguido por `recurso_ptbr_arena_cache`, manteve o
  subestado `24`, 3.657 tarefas gráficas e carga normal.
- Replay após `End`, com catálogo real e arena no modo 1: 5.042 tarefas
  gráficas, sem retorno ao congelamento em `vsprintf`; `It's morning...` e
  `Gulp...` foram substituídas pelo caminho nativo seguro.

`TESTAR.bat` habilita agora `WPJ2_REALOCAR=1` e `WPJ2_REALOCAR_FMT=1`. O modo 2
continua reservado a diagnóstico, pois força a arena mesmo para traduções que
cabem.

## Rota de tabelas

`func_80096D40` constrói uma tabela de ponteiros diretamente, sem passar por
`func_800319B0`. O wrapper deixa o corpo original construir e terminar a tabela
e só então reaponta individualmente cada elemento reconhecido. O limite de 24
entradas deriva do maior bloco de tabela observado no `wonder-source` (`0x60`
bytes). O replay tardio permaneceu estável após habilitar essa cobertura, com
4.440 tarefas gráficas em 18 segundos e a thread principal em `0x800CC98C`.

Permanece necessária a validação interativa de falas longas reais em cenas
mais avançadas, mas não há mais uma rota textual conhecida que exija alterar o
layout do bloco carregado.
