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
