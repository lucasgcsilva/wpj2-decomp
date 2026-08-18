# Sonda atual: bloqueio de inicializacao de recursos

Data: 2026-08-10

## Cadeia confirmada no recompilado

`80002F20 -> 8005E19C -> 8004F3E8 -> 800B202C -> 800B23C4 -> 800B407C`

As chamadas preparatorias de `800B202C` retornam. A ROM em `0x000FCD38`
inicia por `04 7F`, o indice esperado pelo primeiro `800B23C4`; antes da
correcao, o runtime lia `80 32`. A causa era a copia curta de ROM para RDRAM
na ordem fisica dos bytes. Como ambos os mapeamentos usam palavras trocadas no
host, uma copia de 2 bytes precisava aplicar `^3` em cada byte.

Foi adicionado um HLE restrito em `800BD218` para fonte fisica de ROM e destino
em RDRAM; enderecos fora dessa forma seguem para o corpo recompilado. A nova
copia entregou `0x047F`, depois `0x102B6C` e `0x12ADC4`, todos iguais ao
Project64. `800B407C`, `800B23C4`, `800B202C` e `8004F3E8` passaram a retornar.

O renderer passou a executar milhares de `TEXRECT` e `LOADBLOCK`, preenchendo
o framebuffer com `47.166` pixels nao pretos. A verificacao posterior mostrou
que esses retangulos desenham apenas o fundo: a primeira logo e uma malha
F3DEX com vertices e `TRI1`, nao um sprite-retangulo.

Na execucao limpa de 18 segundos, `80002F20` retornou com
`flags=1421`, estado `8/1` e pendente `8/1`, igual ao ponto medido no
Project64. Foram alcançadas `502` de `3651` funcoes recompiladas (13,7%).

## Renderizacao visual

Foi implementado o cache de vertices F3DEX e o rasterizador afim minimo de
`TRI1` para o viewport ortografico do boot. A codificacao de indices da ROM
foi medida diretamente: `00/1E/0A` representa vertices `0/3/1` (passos de
dez). Com isso o executavel passou a gerar a logo ENIX texturizada, em vez do
retangulo cinza anterior. A leitura de coordenadas de vertice foi corrigida
de 10.5 para 5.10 e limitada ao `SETTILESIZE` CI8 de 32x64; isso removeu os
recortes da logo. O exportador tambem passou a respeitar a janela efetiva do
VI (`237` linhas, calculadas de `VI_V_START` e `VI_Y_SCALE`), eliminando a
linha residual que nao era apresentada pelo hardware. A captura verificavel e
`lab/vi_crop_t2.png`.

Na tela seguinte foi identificado e corrigido um segundo erro de interpretacao
RDP: `LOADTLUT` usa em `w1[23:14]` a contagem da paleta menos um, e nao uma
coordenada 10.2. O runtime dividia esse campo por quatro e carregava so 64
entradas. A Givro e CI8 e precisava das 256 entradas: os indices acima de 63
viravam pretos, deixando apenas fragmentos verdes/brancos. Com a contagem
integral, a segunda logo e renderizada por completo, inclusive o azul. A
captura e `lab/giro_tlut256_t2.png`.

Foi acrescentado o transformador minimo F3DEX: `G_MTX` carrega projecao e
modelview, `MOVEMEM` atualiza o viewport e os vertices passam por
perspectiva antes da rasterizacao. A sonda temporal de 16 quadros confirma a
animacao da abertura: ENIX aparece superampliada, estabiliza e sai antes da
Givro. A folha cronologica esta em `lab/animacao_mtx_contato.png`.

Uma revisao visual encontrou um erro adicional no `Vp` do F3DEX: X e Y eram
lidos em ordem invertida. Assim, o centro do viewport `(160,120)` virava
`(120,160)` e a logo estabilizada ficava deslocada para a esquerda e para
baixo. A leitura foi corrigida para `vscale[x,y]` e `vtrans[x,y]` nos offsets
corretos. A nova captura `lab/viewport_corrigido2_t6_frame.png` mostra a ENIX
centralizada; a posicao e resultado direto das matrizes/viewport da ROM, sem
compensacao especifica para essa logo.

O estado F3DEX que ainda faltava tambem foi fechado: `G_TEXTURE` seleciona o
tile usado por `TRI1` e `G_POPMTX` reduz a pilha modelview. A sonda passou de
1.742 para zero comandos graficos ignorados na abertura. A 300 Hz, usado
somente como acelerador diagnostico, a ROM alcancou a terceira logo J2 e a
faixa de texto, registrando 521 de 3651 funcoes (14,3%). A captura e
`lab/terceira_hz300_t7_frame.png`.

Depois dessa transicao apareceu o proximo bloqueio de CPU: a thread 10 chama
`osDestroyThread(NULL)` e chegava a `func_800CBFC0` com ponteiro de lista
nulo. A causa foi isolada na substituicao nativa de `__osEnqueueThread`: ela
inseria a thread na lista, mas omitia a escrita de `OSThread.queue` (offset
`+0x08`) presente no delay-slot da rotina original. O mesmo contrato faltava
no caminho HLE que acorda uma thread por evento. Ambos agora espelham a fila
no objeto da thread.

Com isso, as threads `0x80112368` e `0x8011A670` foram removidas sem falha,
a execucao chegou a 533 de 3651 funcoes (14,6%) e a tela seguinte ao conjunto
de logos foi renderizada. A captura
`lab/queuefull_start2_t2_frame.png` mostra o cenario azul com a inscricao
"The Siliconian Empire". Uma pressao de START foi entregue na 11a leitura
real do controle e aceita pelo jogo; a navegacao posterior ainda estaciona em
filas de eventos/temporizadores, portanto nao foi declarada funcional.

Ainda faltam clipping, combiners e mistura alfa completos. Portanto as imagens
validam o caminho visual de abertura, mas ainda nao sao uma reproducao
pixel-a-pixel nem validam a navegacao pelos menus.

## Proxima verificacao

Com a cadeia de recursos, a transicao de threads e a tela de titulo
desbloqueadas, a proxima verificacao e rastrear a fila/evento que deve reativar
a leitura de controle depois do START. A tentativa de retomar o scheduler a
forca foi revertida: ela parava a cadeia normal em 502 funcoes, abaixo das 533
obtidas pelo caminho correto.

## Status

Progresso estimado: **92%**. A carga de recursos, o estado de inicializacao,
<!-- descricao anterior substituida pelo marco abaixo.
as tres logos — inclusive a animacao de escala/rotacao — foram validados.
-->
as tres logos, a transicao de threads e a primeira tela apos as logos foram
validados. Ainda faltam a reativacao de entrada apos START, clipping e RDP
completo, e a navegacao completa pelos menus.

Imagem util gerada pelo decompilador: **sim**,
`lab/queuefull_start2_t2_frame.png` (tela de titulo apos as logos,
renderizada pelo pipeline F3DEX).
