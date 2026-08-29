# Save state real: mapa e implementação

## Estado final da implementação — 29/08/2026

O replay F2/F4 foi substituído no build principal por snapshots versionados
com execução retomável, independente de pilhas/fibers do sistema operacional.
`Ctrl+Shift+1..9` grava `sav/bookmarks/slotN.wpstate`; `Ctrl+1..9` restaura o
slot correspondente no mesmo processo e mantém
a janela aberta. Os nove arquivos são independentes e uma nova gravação
substitui atomicamente apenas o slot escolhido. O replay permanece somente nos
perfis antigos de stress.

Validação da interface de nove slots em 29/08/2026:

- `Ctrl+Shift+8` e `Ctrl+Shift+9` produziram dois arquivos v6 de 10.808.312
  bytes, com hashes diferentes por representarem instantes distintos;
- `Ctrl+8` e `Ctrl+9` restauraram ambos na mesma janela, sem erro ou reinício;
- os testes unitários de continuation e de retomada de thread passaram.

Após uma operação concluída, uma camada Win32 pertencente à janela exibe por
1,8 segundo `Slot N salvo` ou `Slot N carregado` no canto inferior esquerdo.
Falhas aparecem em vermelho. A camada fica acima do swapchain RT64, não recebe
mouse/teclado e não modifica framebuffer, RDRAM ou o snapshot.

## Diferença confirmada para o Project64

O Project64 mantém PC e registradores da CPU em estruturas explícitas. Seu
`CN64System::SaveState` grava PC, GPR, FPR, CP0, FPCR, HI/LO, registradores dos
dispositivos, TLB, PIF RAM, RDRAM, DMEM, IMEM e o próximo timer de VI. O load
reinicia o núcleo, restaura esses campos e reconstrói timers/plugins.

No runtime atual de WPJ2, o `recomp_context` explícito deixa de ser suficiente
assim que uma `OSThread` entra numa cadeia de chamadas C. A continuação passa a
viver na pilha de uma fiber Win32. Essa pilha contém estado opaco do host e não
pode integrar um arquivo portátil.

## Inventário do código gerado

Levantamento de 28/08/2026 sobre `src/RecompiledFuncs`:

| item | quantidade |
|---|---:|
| arquivos C gerados | 37 |
| chamadas diretas recompiladas | 8.274 |
| chamadas indiretas por `LOOKUP_FUNC` | 82 |
| rótulos `after_N` já emitidos | 8.353 |
| pausas diretas de laço próprio | 10 |

Os `after_N` existentes são uma base útil: identificam quase todos os pontos
posteriores a chamadas sem redescobrir o fluxo. Pontos `RECOMP_POLL` inseridos
em loops também precisam virar continuations porque podem entregar um evento e
preemptar a thread.

## Arquitetura escolhida

Cada thread possui uma pilha própria de frames com `function_vram`,
`callsite_vram`, temporários C recompilados, alvo de chamada indireta e uma
imagem dos GPRs/FPRs no instante da chamada.
Antes de uma chamada potencialmente bloqueante, a função registra sua frame.
Se a execução ceder, todas as chamadas C retornam ao dispatcher sem remover as
frames. No despacho seguinte, cada função consome sua frame, salta ao callsite
e reconstrói a cadeia até o ponto mais interno.

Isso preserva a velocidade do C recompilado e remove do estado persistente:

- pilha da fiber;
- endereços de código do host;
- `jmp_buf` e outras estruturas dependentes de ABI/ASLR.

O núcleo está em `runtime/continuation.c`. O teste
`src/tests/test_continuation.c` simula três funções aninhadas, abandona a pilha
C no ponto interno, copia somente o estado serializável e reconstrói a cadeia
em uma segunda execução. Resultado: `continuation snapshot roundtrip: OK`, sem
repetir o efeito executado antes da pausa.

## Transformação do código real

`src/scripts/injetar_continuacoes.py` produz
`src/RecompiledFuncsStateful/` sem modificar a saída original derivada da ROM.
Ele associa cada chamada ao endereço de seu `JAL`, `JALR`, `J` ou `JR`, cria o
switch de retomada no prólogo e envolve a chamada com push, propagação de
cessão e pop da frame.

A transformação cobriu todas as 8.356 chamadas detectadas nas 3.651 funções.
Os 37 arquivos resultantes foram compilados pelo MSVC com
`RECOMP_STATEFUL`; não houve erro de label, escopo ou ABI. Os avisos de formato
em `funcs_30.c` já existiam no código convidado e não foram introduzidos pela
transformação.

`RECOMP_POLL`, pausas e preempções agora retornam ao dispatcher stateful. O
build principal gera `RecompiledFuncsStateful` e liga `sched_stateful.c` no
`wpj2_probe.exe`; as fibers antigas não participam mais desse caminho.

## Estado gravado no arquivo

| seção | conteúdo | origem |
|---|---|---|
| identidade | magic, versão, tamanhos e hash do payload | runtime |
| memória | 8 MiB de RDRAM e arena PT-BR | runtime |
| threads | `recomp_context`, PC raiz, continuation stack v6 com GPR/FPR, estado/fila | scheduler |
| kernel | filas, eventos, timers e retrace | scheduler/HLE |
| PIF | PIF RAM, controle e Controller Pak | PIF/mempak |
| RSP | 8 KiB de SPMEM, status e fila de conclusão | RSP |
| áudio | estado lógico de AList/AI; buffers host serão recriados | áudio |
| vídeo | framebuffer na RDRAM; swapchain RT64 é recriada | vídeo/RT64 |

## Validação concluída

- testes unitários restauraram continuations aninhadas e uma thread a partir
  de arquivo em processo novo;
- abertura automática percorreu 616/3.651 funções e 9.279.159 chamadas em
  120 s, sem exceção, fault ou assert;
- roundtrip gravou/restaurou 10.808.312 bytes no corredor 3D;
- o mesmo arquivo foi carregado por outro processo e continuou por 35 s,
  produzindo 1.727 listas gráficas e 1.036 listas de áudio;
- dois F4 consecutivos no mesmo processo restauraram o estado `12/50` e a
  execução continuou até o encerramento programado;
- RT64 foi recriado depois de cada carga, em vez de cair no fallback CPU.

O primeiro formato funcional conservava somente callsite, temporários locais e
SP. Isso não bastava: ao reconstruir uma chamada bloqueante, o pai podia usar
registradores voláteis deixados pelo filho profundo. O v6 grava em cada frame
GPRs, FPRs, HI/LO, status e modo de FPU; `f_odd`, por ser ponteiro do host, é
sempre reconstruído. O limite foi ajustado para 128 frames por thread, muito
acima da profundidade observada (10), sem inflar o arquivo com 1.024 contextos
completos por OSThread.

O formato atual é interno e versionado. Mudanças incompatíveis recusam o
arquivo antigo por versão/hash em vez de aplicar estado parcial.

## Referências locais

- `tools/Project64-source/Source/Project64-core/N64System/N64System.cpp`;
- `tools/N64Recomp-source/src/cgenerator.cpp`;
- `tools/N64Recomp-source/src/recompilation.cpp`;
- `tools/N64ModernRuntime-source/ultramodern/src/threads.cpp`;
- `tools/MegaManX4Recomp/psxrecomp-v4/runtime/src/savestate.c`;
- `runtime/sched.c`.
