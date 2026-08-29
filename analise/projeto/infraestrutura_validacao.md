# Infraestrutura para validação da abertura

## Controle completo — 25/08/2026

Os bits foram conferidos contra `libreultra/include/2.0I/PR/os.h`. O mapa agora
cobre A, B, Z, START, D-Pad, L/R e os quatro C-Buttons. Também foi removida a
mistura anterior em que setas e WASD acionavam simultaneamente D-Pad e
analógico.

Mapa de diagnóstico:

- WASD: D-Pad;
- setas: analógico em ±80;
- IJKL: C-Up, C-Left, C-Down e C-Right;
- Q/E: L/R; C: Z; X/Espaço: A; Z: B; Enter: START.

O replay sintético confirmou `C-Up=0x0008` chegando ao PIF na terceira leitura
e a troca do analógico para `80,-80` na quinta. O formato persistente registra
botões e eixos juntos, indexados pela contagem de `CMD_READ_BTN`.

## Bookmark seguro no lugar do checkpoint de RDRAM

O F2/F4 antigo copiava 8 MB de RDRAM e o framebuffer, mas mantinha as pilhas C
das fibers, filas, RSP/RDP, TMEM e áudio no estado posterior. Essa combinação
não representa um instante real da máquina e explica os carregamentos que
funcionavam uma ou duas vezes antes de travar.

O novo fluxo não restaura memória:

1. F2 sobrescreve `sav/bookmarks/quick.replay`, `quick.bmp` e `quick.txt`;
2. o replay inclui cada mudança de botões e analógico e o número-alvo de
   leituras do controle;
3. F4 encerra o runtime com código reservado 42;
4. `TESTAR.bat` preserva o terminal, relança o executável com `WPJ2_REPLAY` e
   remove a espera de 60 Hz até pouco antes do alvo;
5. a cena é alcançada por uma execução nova, com threads e periféricos
   coerentes, e então volta à velocidade normal.

Isso é um bookmark reproduzível, não um save state arbitrário como o do
Project64. Um save state instantâneo continua exigindo contextos guest e
continuações serializáveis para substituir as pilhas nativas das fibers.

Desde 25/08/2026, o replay F4 usa o mesmo turbo de navegação do F11: sete de
cada oito tarefas gráficas são descartadas, a apresentação RT64 e a saída de
áudio hospedada ficam suspensas, mas entradas, filas, textos e transições
continuam sendo processados. O modo termina pela contagem exata de leituras do
controle gravada no F2 e então restaura renderização, áudio e 60 Hz.

Na revisão seguinte, F4 passou ao perfil máximo: nenhuma lista gráfica é
rasterizada enquanto o replay ainda não alcançou o poll-alvo. CPU recompilada,
RSP, eventos, entradas e lógica de áudio continuam integrais, pois saltá-los
mudaria o estado a ser reconstruído. F11 permanece em sete de oito para servir
como navegação visual; o título diferencia `[8x]` de `[retornando ao F2]`.

O relançamento da janela permanece necessário na arquitetura atual. As fibers
mantêm pilhas e continuações C nativas, e RT64, filas e periféricos hospedados
também carregam estado não serializável. Um reset dentro do mesmo processo
reintroduziria a classe de travamentos do save state primitivo. Manter o HWND
visualmente estável é possível no futuro com um processo-host permanente e um
runtime-filho reiniciável, mas não por um reset seguro do executável atual.

Validação ponta a ponta: F2 gravou um bookmark no retrace 255, leitura 199 e
estado `8/1`; F4 encerrou o processo original, o supervisor abriu um novo PID,
carregou o replay e alcançou o alvo sem restaurar memória nem travar. O terminal
permaneceu aberto durante a troca e o segundo processo encerrou normalmente.

## Catálogo inicial de assets

`assets/manifest.json` introduz IDs estáveis e mapeia inicialmente:

- o contêiner `Seg_639B20`, que inclui a fonte/UI e alimenta `D_8015F880`;
- sequências, tabela, samples ADPCM e banco de instrumentos;
- o banco textual acrescentado pelo patch T-En do Ryu.

`src/scripts/extrair_assets.py` validou todas as seis faixas contra a ROM local,
extraiu 895.808 bytes sem conversão e gravou hashes SHA-256 no índice local.
Os bytes ficam em `assets/generated/` e são ignorados pelo Git. A fonte já tem
o mapeamento de consumidor mais profundo: contêiner →
`SysMem_GetPhysicalAddressFromVirtual`/`Spi_DecompressAsset` → `D_8015F880` →
`func_80094230`.

## Watchdog de congelamento completo — 26/08/2026

O congelamento relatado na loja interrompe simultaneamente imagem e áudio,
diferente das falhas anteriores em que a thread de áudio continuava. O
`TESTAR.bat` passou a habilitar um watchdog diagnóstico que observa, a cada
segundo, três contadores independentes: retraces, tarefas RSP de áudio e polls
da CPU/controle.

Se os três permanecerem inalterados por dez segundos, o runtime grava
automaticamente o mesmo relatório completo usado no encerramento: estado,
trilha recente, RDRAM, glifos e demais artefatos em `temp/projeto/padrao`.
Ele não encerra a aplicação e não depende de F5. A captura será usada para
distinguir deadlock hospedado, espera guest sem eventos e laço bloqueado antes
de alterar a lógica da loja.

### Resultado da primeira captura automática — 27/08/2026

O watchdog registrou 94.105 retraces, 83.204 tarefas RSP e 207.005 polls antes
de os três contadores pararem. Não havia conclusão RSP perdida (`pico=1/256`,
`descartadas=0`) nem trabalho restante nas filas SP/DP. A trilha da thread de
tarefas termina em `Scheduler_ScheduleTask` -> `Scheduler_IsTaskReady` ->
`osSpTaskLoad` -> `__osSpDeviceBusy`.

No runtime, `func_800CD060` conclui a cópia de SP dentro da própria chamada;
logo os bits DMA_BUSY/DMA_FULL/IO_FULL não podem continuar ocupados. O código
recompilado, porém, lia a página MMIO usada apenas como espelho e podia observar
bits obsoletos indefinidamente. Foram promovidas a substituições nativas:

- `__osSpSetPc`: aceita a escrita somente com `HALT` ativo e publica o PC;
- `__osSpDeviceBusy`: informa livre após a DMA síncrona do runtime.

Isso corrige a condição terminal medida, sem introduzir um reset periódico ou
uma limpeza de memória sem evidência. O watchdog permanece ativo para provar a
estabilidade numa execução longa e capturar uma trilha diferente se ainda
existir outro congelamento.

## Stress automatizado e auditoria de cobertura — 28/08/2026

O perfil `TESTAR.bat stress N` usa o bookmark F2 como preparação reproduzível.
O replay conserva seus controles até o poll-alvo e roda sem espera; 120 polls
depois, uma sequência determinística alterna botões, D-Pad, C-Buttons, Z,
L/R e as quatro direções analógicas. Áudio e listas RSP continuam executados,
embora janela e reprodução sonora sejam ocultadas. O watchdog continua sendo
a autoridade para congelamento de vídeo+áudio+CPU.

Foram executados 345 segundos acumulados em três calibrações. A rodada longa
da abertura terminou no limite com 10.802 retraces e 13.514 tarefas RSP. A
rodada pelo bookmark chegou à loja, recarregou seus recursos repetidamente e
terminou com 85.361 retraces, 77.832 tarefas RSP, 38.107 chamadas do heap e
zero conclusão RSP descartada. A validação final, iniciando a sequência apenas
depois do alvo exato 41.327, encerrou com 78.959 retraces, 72.663 tarefas RSP,
34.524 chamadas do heap e novamente nenhuma conclusão descartada. Nenhuma das
três disparou o watchdog, exceção ou parada antecipada.

O stress é exploração, não prova de conclusão do jogo: entradas sintéticas
podem permanecer num menu ou alternar telas já alcançadas. Seu resultado prova
estabilidade sob carga repetida das rotas exercitadas, especialmente a loja,
mas não substitui um replay gravado até os créditos.

### Rodada acelerada da loja — 28/08/2026

Uma quarta rodada usou seed 7, 240 segundos de host e retrace a 480 Hz depois
do bookmark. Ela acumulou 176.737 retraces, 160.787 tarefas RSP, 321.576
transferências de SP, 33.730 DMAs de cartucho e 112.583 chamadas do heap. Foram
processados 3.813.463 LOADBLOCKs, aproximadamente 6,49 GB de dados enviados à
TMEM, e 80.241 mensagens chegaram à fila inicial.

Mesmo sob essa carga, a fila de conclusões RSP ficou em pico 1/256, sem nenhum
descarte, nenhuma DMA PI foi recusada e não houve watchdog, exceção, falha de
heap ou encerramento antecipado. A sonda de tradução terminou somente com o
cabeçalho: nenhum provável texto inglês sem catálogo nas telas atravessadas.

A sequência permaneceu principalmente na loja (`estado 1/1`) e variou seus
recursos/portão interno; entradas sintéticas não conseguiram garantir avanço
narrativo. O teste é forte contra o antigo congelamento acumulativo da loja,
mas um replay humano mais adiante continua necessário para ampliar cobertura.

### Stress dirigido e falha intermitente do replay — 28/08/2026

`TESTAR.bat stress_loja N` usa marcos textuais reais do tutorial, não atrasos
fixos. O marcador atual ainda precede o controle livre de Bird. Nas repetições
longas, duas inicializações pararam antes do alvo em `estado 11/24`: imagem
preta, polls fixos em 18.588/18.790 e áudio/retraces ainda ativos. Outras
inicializações com o mesmo binário ultrapassaram o alvo, evidenciando uma falha
intermitente da reconstrução F4, não do roteiro posterior.

O F5 agora inclui `sprite_banco_0..2_0..3` no `.txt`, extraído dos ponteiros
nativos `D_801A8C18`, `D_801A8C24` e `D_801A8C30`. Esses registros permitem
isolar o slot de Bird pela variação de `x/y` e guardar a posição exata sobre um
objeto interativo.
