# Clipping das paredes do corredor 3D

Atualizado em 23/08/2026.

## Regressão identificada

O rasterizador já continha recorte homogêneo contra o plano próximo e os
limites X/Y do frustum. O `TESTAR.bat` anterior ativava esse caminho com
`WPJ2_F3D_W_CLIP=1`; a reestruturação do BAT removeu a variável. Na ausência
dela, `runtime/rsp.c` selecionava o comportamento legado.

O comportamento legado descarta o triângulo inteiro quando qualquer vértice
fica atrás da câmera. Durante a aproximação do trono, faces grandes das paredes
cruzam esse plano: elas desaparecem em uma tarefa e reaparecem quando todos os
vértices voltam ao lado aceito.

## Confirmação nos fontes

- `tools/wonder-source/src/code/code_15870.c` emite quatro comandos
  `G_MW_CLIP` por meio de `gSPCustomClipRatio(..., FRUSTRATIO_2)`;
- os `gbi.h` de `wonder-source` e `libreultra` documentam o clipping próximo
  do F3DEX;
- a cena usa `G_ZBUFFER`, `G_CULL_BACK`, perspectiva e matrizes de câmera;
- o histórico do projeto já registrava que o recorte homogêneo eliminava o
  sumiço das paredes sem introduzir polígonos espúrios.

## Correção

O recorte passou a ser o padrão do runtime na cena 12/50. A variável
`WPJ2_F3D_W_CLIP=0` ainda permite voltar ao modo legado para diagnóstico, e o
`TESTAR.bat` declara explicitamente o valor fiel.

O escopo continua restrito à cena validada: ampliar o recorte para todas as
cenas exigirá comparar outras geometrias contra o Project64.

