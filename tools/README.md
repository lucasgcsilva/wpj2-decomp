# Ferramentas locais

`run_initial_analysis.ps1` executa o scanner de segmentos sobre a ROM indicada
e requer o pacote isolado em `tools/python-deps`.

O scanner é deliberadamente conservador: seus candidatos são insumos para a
validação de segmentos e não devem ser usados diretamente para gerar
`wpj2.syms.toml`.

`find_overlays.py` recebe uma hipótese explícita para o fim do boot e mostra
as chamadas `jal` que escapam dele, junto dos trechos pós-boot decodificáveis.
Ele não infere nem grava endereços VRAM de overlays.

`find_jal_refs.py` localiza os blocos da ROM que possuem instruções `jal` para
uma faixa VRAM indicada; é a etapa de triagem antes da validação do delta de
um overlay.

`disasm_range.py` apresenta uma faixa da ROM com um VRAM explícito; use-o para
inspecionar entrypoints e limites de segmentos sem assumir um mapa global.

`N64Recomp-source/` é a cópia oficial do N64Recomp com submódulos, e
`N64Recomp-build-official/` contém os executáveis compilados localmente. Ambos
são dependências de desenvolvimento, não dados da ROM.


## Pipeline de build e sondagem

`env.cmd` define o ambiente compartilhado (vcvars64, `PROJ`, `RECOMP_INC`,
`ROM`). Ele se protege contra chamada dupla porque encadear scripts que chamam
o vcvars estoura o limite da linha de comando.

`autostub.py` roda o N64Recomp em laco, transformando em stub cada funcao que
ele nao consegue traduzir, ate a recompilacao passar. A lista fica em
`stubs.txt`.

`explain_stubs.py` e `one_stub_reason.py` recuperam a mensagem exata que o
recompilador emite por cada stub. Sem isso a lista vira um ponto cego: nada
distingue uma instrucao de cache (inofensiva no host) de uma escrita em EPC
(o nucleo do SO). O resultado esta em `stubs_motivos.txt`.

`check_boundary.py` diz se um endereco e alvo de `jal`. Use antes de fundir duas
funcoes vizinhas nos simbolos: fundir um alvo de `jal` quebraria toda chamada
direta a ele.

`build_lib.cmd` compila `src/RecompiledFuncs/` em `build/wpj2_recompiled.lib`. Nao
linka nem executa nada, entao um erro ali e sempre do recompilador ou dos
limites de funcao — nunca do runtime.

`gen_table.py` gera `runtime/func_table.c` (vram → ponteiro de funcao) a partir
de `src/RecompiledFuncs/funcs.h`.

`trace_inject.py` produz `src/RecompiledFuncsTraced/` a partir de
`src/RecompiledFuncs/`, com um hook no topo de
cada funcao. Chamadas diretas entre funcoes recompiladas nao passam pelo lookup
do runtime, entao sem os hooks nao da para medir o que executou.
`src/RecompiledFuncs/` nunca e modificado no lugar.

`build_probe.cmd` encadeia os tres passos acima e linka `wpj2_probe.exe`.

`callers.py` responde "quem chama quem" nos dois sentidos, a partir de
`analise/projeto/codigo/callgraph.txt`.

`sweep.py` roda varias sondagens ao mesmo tempo, uma por configuracao, e compara
cobertura entre elas. Cada corrida leva 20 s de relogio; seis em sequencia
custariam dois minutos, seis em paralelo custam vinte segundos. A sondagem le
`WPJ2_TIMEOUT`, `WPJ2_MEMSIZE`, `WPJ2_EVENTS`, `WPJ2_BUTTONS` e `WPJ2_OUT` do
ambiente — o ultimo e o que impede as instancias de pisarem na saida uma da
outra. `--entrada` troca o eixo de eventos pelo de entrada e duracao.

`callee_status.py` lista cada `jal` do corpo de uma funcao marcando quais
destinos executaram. Leia o cabecalho antes de usar: o traco e por funcao, nao
por local de chamada, e isso gera falso positivo em codigo em linha reta.

`frontier.py` cruza `executadas.txt` (gravado pelo runtime ao fim de cada
corrida) com o callgraph e lista o que esta a uma chamada de distancia do que ja
roda. E o inverso util do traco: em vez de "por onde passou", responde "o que
estava prestes a acontecer e nao aconteceu". Tambem ranqueia pelo outro lado —
funcoes que rodam muito e nao se ramificam, que costumam ser despachantes presos
num unico caso.

`xref_addr.py` procura, em paralelo, quem forma um endereco qualquer nas 3.651
funcoes, distinguindo leitura, escrita e mera formacao de endereco. Serve para
achar quem escreve uma variavel de estado.

`symbolize.py` transforma o RVA impresso num relatorio de falha em nome de
funcao, usando `build/wpj2_probe.map`. O runtime imprime o deslocamento dentro do
modulo, e nao o endereco absoluto, justamente para sobreviver ao ASLR.

`classify_hw.py` classifica cada funcao do segmento boot pelo bloco de MMIO cujo
endereco ela forma. E como a fronteira da libultra foi localizada por evidencia
em vez de suposicao; a saida vai para `hw_map.txt`.
