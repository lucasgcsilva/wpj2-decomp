# Checkpoints F2/F4 e arquitetura de save state

## Resultado da comparação

### N64DX / oot-dx

O `oot-dx` atual não possui serializador de save state arbitrário. Sua utilidade
para este projeto está na navegação de depuração no nível do próprio jogo:
avanço quadro a quadro, noclip, controle durante cutscene, editor de inventário,
seleção de mapa e edição de flags de save. Essa abordagem chega rapidamente a
uma cena por uma transição lógica conhecida, mas não congela CPU, memória e
periféricos num instante qualquer.

### MegaManX4Recomp / psxrecomp-v4

O submódulo `psxrecomp-v4` implementa um save state completo e estabelece os
requisitos que faltam ao nosso runtime:

1. pedido de salvar/carregar é adiado até um block leader seguro;
2. o arquivo é versionado e possui chave de integridade do jogo, codegen e ABI;
3. CPU, RAM e todo subsistema mutável são seções obrigatórias;
4. o carregamento restaura o PC, ressincroniza o relógio e reinicializa apenas
   o estado derivado de apresentação;
5. o código abandona a pilha corrente e volta ao laço externo do escalonador,
   que despacha o PC restaurado;
6. o próprio projeto recusa load state no modo antigo de host fibers, pois um
   salto entre fibers não consegue restaurar de forma segura a pilha nativa.

## Diferença para o runtime de WPJ2

Cada `OSThread` de WPJ2 possui um `recomp_context`, mas depois da primeira
execução sua continuação real vive na pilha C de uma fiber Win32. Os campos de
PC da `OSThread` só criam a fiber e não descrevem o ponto atual. Por isso uma
cópia de RDRAM, RSP, áudio e registradores ainda deixaria o retorno das funções
no futuro. Esse é o motivo estrutural das antigas travas após F4.

## Mudança entregue: bookmark v2

O F2/F4 ainda usa reinício + reprodução, pois é o único método completo com o
escalonador atual. A instabilidade concreta foi removida do índice:

- v1: mudança de entrada associada à contagem de leituras do PIF;
- v2: mudança de entrada associada ao retrace emulado observado pelo jogo;
- o alvo do turbo também é o retrace, não o poll;
- arquivos v1 continuam carregando para não invalidar bookmarks locais;
- novos arquivos declaram `formato=WPJ2_BOOKMARK_2`, `alvo_poll` apenas para
  diagnóstico, `alvo_retrace` e `roteiro_retrace`.

O teste mínimo carregou três transições, ativou o avanço e voltou à velocidade
normal exatamente no retrace 4. Na primeira validação distante apareceu um
segundo defeito: remover completamente a cadência do VI deixava a thread de
maior prioridade sempre pronta. O contador alcançava o retrace salvo antes da
primeira leitura do controle, portanto o número do quadro coincidia, mas o
estado do jogo não.

O retorno v2 agora mantém cadência acelerada de 8x (480 Hz). Assim as threads
de controle, RSP e lógica progridem junto com o VI, sem a espera normal de 60
Hz. Com o bookmark real de `alvo_retrace=8164`, a reprodução consumiu as 161
transições gravadas antes do alvo e terminou em `estado=1/1`, igual ao
`quick.txt` salvo. A execução continuou até o retrace 8467 no mesmo estado.

## Caminho para save state real

Não basta acrescentar um arquivo de RDRAM. A implementação segura precisa ser
feita nesta ordem:

1. converter cada ponto de cessão/preempção numa continuação explícita
   (`resume_pc` + `recomp_context`) que retorna ao escalonador, sem depender da
   pilha da fiber;
2. tornar cada continuação reentrante por um despachante de block leaders;
3. remover as fibers e manter filas/threads integralmente em estruturas do
   runtime e na RDRAM;
4. criar stream versionado com seções obrigatórias: RDRAM, CPU por thread,
   filas/eventos/retrace, MMIO e DMA, PIF, RSP/DMEM/IMEM/TMEM, estado de áudio
   e demais latches mutáveis;
5. tratar RT64 e saída sonora como estado derivado: limpar filas hospedadas e
   reapresentar o primeiro quadro após a restauração;
6. incluir identidade da ROM traduzida e versão/hash do codegen; rejeitar
   estados incompatíveis, nunca carregar parcialmente;
7. validar por hashes de quadros/áudio e repetição da mesma sequência após o
   load, além de testes em transições 2D/3D e tarefas RSP em andamento.

Até a etapa 3, o replay cadenciado por retrace é o checkpoint correto; chamar
uma cópia parcial de RDRAM de save state apenas esconderia corrupção futura.

## Referências locais

- `tools/oot-dx/README.md`, seção **Debug features**;
- `tools/MegaManX4Recomp/psxrecomp-v4/runtime/include/savestate.h`;
- `tools/MegaManX4Recomp/psxrecomp-v4/runtime/src/savestate.c`;
- `tools/MegaManX4Recomp/psxrecomp-v4/runtime/include/boot_state.h`;
- `tools/MegaManX4Recomp/psxrecomp-v4/runtime/src/boot_state.c`;
- `tools/MegaManX4Recomp/psxrecomp-v4/runtime/include/psx_scheduler.h`;
- `runtime/sched.c`, que documenta a dependência atual das fibers.
