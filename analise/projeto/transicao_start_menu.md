# Transição START → menu

## Tentativa rejeitada em 24/08/2026

A troca do título (`8/26`) para o menu (`11/24`) publica o novo estado antes de
terminar a composição dos seus framebuffers. A apresentação síncrona do host
conseguia mostrar esses alvos intermediários como faixas pretas, embora o N64
continue varrendo o quadro frontal.

Foi testada uma transição sintética na apresentação, reconhecendo START e a
borda `8/26 → 11/24`. A reprodução automática parecia uniforme, mas o teste
interativo mostrou resultado intermitente: cenas 2D ainda surgiam em faixas,
enquanto o caminho 3D podia parecer correto. A solução sintética foi removida.

A próxima correção deve reproduzir a ordem nativa de `VI_ORIGIN`, conclusão do
RDP e troca de buffers, separando o pipeline 2D do hand-off 2D→3D já existente.
Não criar fades no host para esconder framebuffers parciais.

O retângulo vermelho translúcido que pisca sobre a opção é comportamento do
jogo original e permanece fora de qualquer filtro da transição.

## Causa nativa encontrada em 24/08/2026

O cruzamento da lista RDP completa com `wonder-source` e os macros de
`libreultra` mostrou que tanto o realce do menu quanto parte das transições 2D
usam quadriláteros F3DEX sem textura: `gSPTexture(G_OFF)`, `G_CC_SHADE` e cor/
alfa nos quatro vértices. O rasterizador respeitava `G_TEXTURE` somente no
estado 3D `12/50`; em qualquer cena 2D continuava amostrando o último tile.
Daí vinham as faixas com pedaços de imagem anterior e o lixo de sprite.

`runtime/rsp.c` passou a obedecer `g_texture_ligada` também no 2D. Capturas
determinísticas após a mudança mostram a sequência nativa inteira: quadro 2D
escurecendo, quadro preto, menu escuro e menu normal, sem máscara sintética do
host. A confirmação interativa pelo `TESTAR.bat` também reproduziu a sequência
corretamente.

O mesmo conserto recuperou o retângulo vermelho piscante. No menu principal
ele cobre apenas a opção; na seleção de diário cobre toda a ficha rosa. O alfa
vem dos vértices e é composto pelo render mode `0x00504A50`.

## Regressão de estado entre tarefas corrigida em 25/08/2026

A primeira implementação conservava `G_TEXTURE_OFF` globalmente. Isso era
correto dentro da lista que desenha o realce, mas incorreto para lotes 2D
seguintes que partem do estado inicial da nova `OSTask` e não repetem
`gSPTexture(G_ON)`. O resultado parecia um congelamento: a última imagem ficava
na janela, enquanto lógica, VI e áudio continuavam.

O estado `ON/OFF` agora vale normalmente depois que a tarefa atual emite o
comando `TEXTURE`; antes do primeiro comando, um lote 2D assume a unidade ativa,
como o caminho estável anterior. O 3D continua respeitando o estado explícito.

Essa mudança corrigiu a contaminação visual entre tarefas, mas não era a causa
do congelamento tardio relatado depois de `End`. A primeira validação encerrou
cedo demais e produziu esse falso positivo.

## Congelamento 5–6 segundos depois de `End`

Uma reprodução determinística mais longa mostrou que, cerca de seis segundos
depois de confirmar `End`, novas tarefas gráficas e leituras PIF cessavam,
enquanto VI e áudio continuavam. A thread principal permanecia executável em
`0x800F9A50`, presa em `func_8008ED4C`/`vsprintf` (`0x8008EDA4`). Portanto não
era apresentação, Controller Pak nem áudio: era um laço no formatador do jogo.

O A/B com PT-BR desligado era estável. Com o catálogo carregado, mas sem o
patch estático do cartucho, também era estável. Uma bisseção pelas entradas do
catálogo isolou a primeira falha na entrada 45, `It's morning...` →
`É de manhã...`, ROM `0x008006DC`.

A fonte PT-BR reutiliza pontuação ASCII para os glifos compostos; `ã` usa o
byte `%` (`COD_A_TIL`). Injetado antecipadamente no cartucho, esse byte ainda
atravessa uma ou mais chamadas de `sprintf` e é interpretado como marcador de
formato. A correção impede que traduções contendo esse glifo sejam aplicadas
estaticamente. Elas ficam reservadas ao interceptador do recurso vivo, que
atua depois da formatação.

Validação final do mesmo replay, com o catálogo completo: 4.702 tarefas
gráficas em 18 segundos, thread principal novamente aguardando normalmente em
`0x800CC98C`, sem retorno ao laço de `vsprintf`.
