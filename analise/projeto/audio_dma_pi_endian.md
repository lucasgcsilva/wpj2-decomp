# Correção do DMA PI usado pelo cache de áudio

Atualizado em 23/08/2026.

## Evidência no código original

O sintetizador configura `func_800B937C` como `ALDMANew`; ela devolve
`func_800B9068` como `ALDMAproc`. A função implementa o cache de amostras de
2 KiB usado por `alLoad` e, em uma falta de cache, faz exatamente isto:

1. conserva o bit 0 do endereço solicitado;
2. alinha a origem apenas em 2 bytes (`addr - (addr & 1)`);
3. envia 0x800 bytes por `osPiStartDma` para um buffer alinhado da RDRAM;
4. devolve o endereço do buffer acrescido daquele bit.

Esse comportamento aparece em `src/RecompiledFuncs/funcs_31.c`, na faixa
recompilada `0x800B9068..0x800B9378`, e é compatível com a interface
`ALDMAproc` de `tools/libreultra` e com o `alLoad` de
`tools/wonder-source/src/libultra/audio/load.c`.

## Defeito no runtime

A ROM e a RDRAM emuladas são armazenadas no host com cada palavra MIPS de 32
bits invertida. O runtime copiava todo PI DMA com `memcpy`. Essa cópia só
preserva a sequência lógica de bytes se origem e destino tiverem o mesmo
alinhamento módulo 4.

O cache de áudio garante somente alinhamento módulo 2 na origem e usa destino
alinhado. Logo, endereços de amostra `2 (mod 4)` tinham os pares de bytes ADPCM
trocados. Isso explica simultaneamente:

- música reconhecível;
- passagens inteiramente limpas;
- chiado forte apenas em determinados blocos/vozes;
- AList correta, mas estado ADPCM divergente depois de `LOADBUFF`.

## Correção

`runtime/hle.c` passou a usar `copy_pi_logical` em `osPiRawStartDma` e
`__osEPiRawStartDma`:

- mantém `memcpy` quando os alinhamentos módulo 4 coincidem;
- nos demais casos, copia cada byte pela posição lógica MIPS (`endereço ^ 3`);
- aplica a mesma regra no sentido RDRAM → dispositivo;
- conta transferências realinhadas no relatório final.

## Validação local

Duas execuções de aproximadamente 40 segundos foram feitas com RSP de áudio
nativo, cadência virtual do AI e sem reprodução pelo dispositivo do Windows.

| medida | executável anterior | PI lógico corrigido | Project64 documentado |
|---|---:|---:|---:|
| DC médio | +202,0 | **-16,8** | ~ -20 |
| RMS | 5.538 | **4.782** | — |
| pico | 32.766 | **28.761** | — |
| saturações | 0 | 0 | — |

Na execução corrigida, **78 transferências / 159.744 bytes** precisaram do
caminho lógico. O DC por sete trechos de quatro segundos passou de
`+137 +7 +138 +359 +138 +2 +2` para
`-9 -18 -25 -1 -18 -26 -23`.

O resultado não foi obtido por ganho, silêncio, corte de reverb ou remoção de
vozes: música completa e caminho wet permaneceram ativos. O teste auditivo do
usuário confirmou que o chiado praticamente desapareceu; restam estalos raros
e espaçados. A rota passou a ser o padrão do `TESTAR.bat`.
