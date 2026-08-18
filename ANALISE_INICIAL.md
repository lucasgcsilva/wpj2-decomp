# Análise inicial — 2026-08-08

## Ferramentas prontas

- Cópia limpa do N64Recomp com todos os submódulos em
  `tools/N64Recomp-source/`.
- Build Release concluído em `tools/N64Recomp-build-official/`.
- Executáveis confirmados: `N64Recomp.exe` e `RSPRecomp.exe`.
- Rabbitizer instalado isoladamente em `tools/python-deps/`.

## ROM observada

| Item | Resultado |
|---|---|
| Formato / tamanho | `.z64` big-endian / `0x880000` bytes |
| Entry point | `0x80000400` |
| Versão de libultra no header | `0x00001446` |
| Faixa de boot candidata | ROM `0x001000–0x0D7C00` |
| Tamanho da faixa candidata | aproximadamente 859 KiB |

A faixa de boot é uma **hipótese de trabalho**, derivada de blocos de 1 KiB
com 100% de instruções MIPS válidas até `0x0D7C00`. Ela ainda precisa de
validação por limites de função, cobertura de chamadas e inspeção do material
que começa logo após o limite.

## Chamadas que escapam do boot

Usando a hipótese acima, há 27 instruções `jal` para 9 destinos fora do boot,
todos entre `0x8400103C` e `0x84001940`. Os mais frequentes são:

| Destino VRAM | Chamadas |
|---|---:|
| `0x84001220` | 5 |
| `0x840011F8` | 5 |
| `0x84001940` | 4 |
| `0x8400103C` | 3 |
| `0x840013C8` | 3 |

Esse resultado comprova a necessidade de mapear overlays. Ainda não há uma
relação ROM→VRAM confirmada para esses destinos e, por isso, nenhum deles foi
adicionado a uma tabela de símbolos.

## Como reproduzir

```powershell
.\tools\run_initial_analysis.ps1
$env:PYTHONPATH = (Resolve-Path .\tools\python-deps).Path
& 'C:\Users\lucas\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' `
  .\tools\find_overlays.py 'E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64' `
  --boot-end 0xD7C00
```

Próximo passo: correlacionar os destinos `0x8400xxxx` com blobs da ROM e
validar o delta de cada overlay antes de gerar `wpj2.syms.toml`.
