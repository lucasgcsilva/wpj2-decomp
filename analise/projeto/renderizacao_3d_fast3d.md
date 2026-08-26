# Renderização 3D Fast3D/RDP

## Revisão de 25/08/2026 — corredor e trono (`12/50`)

A comparação visual com o Project64 foi cruzada com quatro referências locais:

- `tools/wonder-source`: listas do jogo, `G_TF_BILERP`, `gSPTexture(0x8000,
  0x8000)`, luzes e render modes do cenário;
- `tools/libreultra`: formato S10.5 de `Vtx::tc`, escala unsigned 0.16 de
  `gSPTexture`, ordem das luzes direcionais e da luz ambiente;
- `tools/sdk-tools`: constantes e semântica do GBI/libultra;
- `tools/Project64-source/Source/GLideN64`: transformação das direções de luz,
  filtro N64 de três pontos e cobertura de pixels.

### Causas confirmadas e corrigidas

1. `runtime/video.c` ligava por padrão uma média espacial de cinco pixels ao
   detectar `OS_VI_DITHER_FILTER_ON`. O VI real precisa dos bits ocultos de
   cobertura; a média não o reproduzia e atingia também quadros 3D, criando o
   aspecto manchado. Ela agora é somente diagnóstica (`WPJ2_VI_FILTER_2D=1`).
2. `G_TF_BILERP` usava bilinear PC de quatro texels. O RDP divide a célula em
   dois triângulos e interpola três texels; o caminho TRI1 agora replica a
   fórmula `BILINEAR_3POINT` do GLideN64.
3. As escalas de `gSPTexture` eram registradas, mas ignoradas. Elas agora são
   aplicadas como unsigned 0.16 sobre coordenadas S10.5. Na cena, `0x8000`
   explica por que o antigo divisor fixo `/64` parecia correto por coincidência.
4. A iluminação fazia o produto com a direção crua e com sinal invertido. O
   Fast3D transforma a direção pelo inverso da modelview; o runtime agora faz a
   mesma transformação e usa a última entrada de luz como ambiente.
5. O AA 2x2 descartava primeiro pixels cujo centro estivesse fora do triângulo
   e misturava cobertura com pesos que somavam mais de 100%. Isso gerava linhas
   pretas em todas as diagonais. Agora os quatro pontos são avaliados antes do
   descarte e suas cores são conservadas separadamente, permitindo que dois
   triângulos adjacentes completem o mesmo pixel.
6. A cobertura já era subpixel, mas os vértices tinham sido arredondados para
   pixels inteiros antes do teste. A projeção agora chega ao rasterizador em
   quartos de pixel. O teste e a atualização de profundidade também obedecem
   separadamente `Z_CMP`/`Z_UPD` e conservam Z por subamostra; isso evita que a
   primeira metade de uma face rejeite a cobertura complementar da segunda.
7. A janela ampliava o framebuffer com `HALFTONE` do GDI. Esse filtro borrava
   inclusive texto e textura já resolvidos. A escala exata 320×240→640×480
   agora usa pixels preservados por padrão; `WPJ2_PRESENT_SMOOTH=1` fica apenas
   como lente A/B, não como parte da emulação.
8. A janela nominal de 660×525 resultava numa área cliente de 648×486. Assim,
   mesmo `COLORONCOLOR` deformava a grade 320×240 numa escala fracionária e
   uma aresta quase vertical ganhava passos irregulares. A área cliente agora
   é exatamente 640×480.
9. O Project64/GLideN64 mantém o alvo de renderização em RGBA e resolve o MSAA
   na resolução do host. O runtime agora conserva RGB888 junto das quatro
   amostras de coverage, enquanto continua escrevendo o RGB5551 original na
   RDRAM. A apresentação 2× usa diretamente essas amostras: isso preserva a
   compatibilidade do jogo e evita quantizar iluminação/filtro antes da janela.
10. A primeira apresentação direta das subamostras também atingia a abertura
    2D. Como as logos são formadas por geometria texturizada animada, isso
    revelou dois posicionamentos subpixel e produziu o aspecto de logo dupla
    ou tremendo. Cada pixel agora registra se a cobertura veio da semântica 3D
    (`12/50`): somente esses pixels usam a saída 2×; o 2D voltou à apresentação
    nativa estável já validada.

### Logo ENIX nativa

A captura do framebuffer em `8/1`, tarefa gráfica 200, confirmou que a ENIX
não é um único bitmap linear na ROM: ela é composta por geometria texturizada
durante a animação. A composição nativa ocupa 51×60 pixels no framebuffer
320×240 e contém 99 cores RGB distintas. O recorte nativo, uma ampliação
nearest-neighbor, os pixels RGBA5551 e os metadados foram registrados em
`assets/generated/images/enix_logo/`. A composição capturada é limpa; a
duplicação observada era regressão da apresentação 2×, não defeito do asset.

### Validação automatizada

O executável recompilado alcançou `12/50` sem travar em duas execuções novas.
O caso com cobertura ativa, a 120 retraces/s apenas para reduzir o tempo de
coleta, terminou com 2.029 tarefas gráficas processadas. A última lista levou
7,126 ms e o pico foi 13,304 ms, ambos abaixo do orçamento de 16,67 ms de um
quadro a 60 Hz. A captura final não apresentou as linhas pretas da tentativa
anterior.

Depois da revisão subpixel/Z, uma execução de 30 s chegou novamente a `12/50`
e processou 1.693 tarefas gráficas. A última lista custou 5,228 ms e o pico
12,982 ms. Portanto o Z por amostra não ultrapassou o orçamento de 16,67 ms.

Com RGB888 por amostra habilitado, outra execução chegou a `12/50`, com última
lista em 10,587 ms e pico de 16,162 ms. Ainda cabe no quadro de 60 Hz, embora a
margem menor reforce que o backend de GPU é necessário para resolução maior
sem transferir todo o custo à CPU.

### O que ainda não está provado

- A correspondência de cada material/TMEM deve ser comparada no mesmo retrace
  do Project64; as tentativas antigas de forçar intercalação DXT para CI/RGBA
  continuam rejeitadas e desligadas.
- O VI dither exato depende de coverage/hidden bits que o framebuffer RGB5551
  atual não preserva. Não reativar desfoque global como substituto.
- O backend Direct3D/OpenGL tem impacto direto na apresentação limpa, MSAA e
  desempenho, mas não corrige sozinho TMEM, combiner ou render modes errados.
  O rasterizador CPU corrigido continua necessário como referência para uma
  migração material a material.

### Situação das transições posteriores

As transições 3D→2D e 2D→2D antes registradas como pendentes foram validadas
como resolvidas depois das correções gerais de framebuffer, TEXRECT e alpha.
Elas deixam de integrar `PENDENCIAS.md`; qualquer regressão futura deve ser
tratada como novo caso, sem reintroduzir composição artificial por cima do
quadro original.

## Portabilidade validada de `Vmarcelo49/wpj2-recomp`

O backend visual daquele projeto não contém ajustes particulares para o
corredor: a fidelidade vem do RT64 completo. A comparação direta com
`TextureSampler.hlsli`, porém, revelou uma semântica transferível ao
rasterizador CPU: a correção de perspectiva deve obedecer ao bit
`G_MDSFT_TEXTPERSP` (19) de `OtherMode_H`, e não apenas à presença de uma
matriz. Em `G_TP_NONE`, triângulos recebem a compensação 0,5 de coordenadas
observada no RDP; `TEXRECT` não recebe essa compensação.

`runtime/rsp.c` passou a seguir essa regra. Uma execução automática de 38 s
chegou ao estado `12/50`, atravessou a entrada do corredor e contabilizou:

- 49.714.927 pixels em `G_TP_PERSP`;
- 17.992.780 pixels no caminho afim;
- 2.157 tarefas gráficas, sem falha do rasterizador.

O código-fonte da própria ROM no repositório externo confirma a alternância:
`seg_F24E0.c` e `code_2CE70.c` configuram `G_TP_PERSP`, enquanto a rota de
sprites em `code_98FB0.c` seleciona `G_TP_NONE`. Portanto não se pode forçar
uma única interpolação global sem deformar uma das duas famílias.

Também foi confirmado que o filtro de três pontos local já implementa a mesma
equação do shader do RT64. Copiar outro filtro ou acrescentar blur de
apresentação não traria fidelidade. Os ganhos grandes ainda exclusivos do
projeto externo — Vulkan, resolução interna maior, MSAA e VI em GPU — exigem
um backend RT64 paralelo; não são patches pequenos transplantáveis para o
rasterizador CPU.

O interpretador genérico de `SETCOMBINE` também foi exercitado em um A/B até o
corredor, motivado pelas listas nomeadas do decomp externo
(`G_CC_MODULATEIA/G_CC_PASS2`). A captura com a chave ativa ficou mais escura,
mas não caiu no mesmo retrace da captura de controle; logo não há evidência
suficiente para promovê-lo ao padrão. `WPJ2_RDP_COMBINER` permanece uma lente
diagnóstica até existir comparação sincronizada, evitando regredir a
iluminação que já foi validada visualmente.
