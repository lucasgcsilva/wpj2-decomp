# Auditoria da referência `Vmarcelo49/wpj2-recomp`

Data da auditoria: 25/08/2026  
Snapshot analisado: `ea26330` (07/08/2026)  
Cópia local: `tools/wpj2-recomp/`

## Conclusão executiva

O projeto é uma referência importante e muda a prioridade do nosso roadmap,
principalmente por demonstrar uma ligação curta entre o código recompilado,
`N64ModernRuntime` e o RT64. Ele não é, porém, uma versão simplesmente mais
completa do nosso projeto.

As duas implementações escolheram arquiteturas diferentes:

- `wpj2-recomp` recompila o ELF da ROM japonesa, usa `librecomp`/
  `ultramodern`, recompila `aspMain` com RSPRecomp e entrega display lists GBI
  ao RT64/Vulkan;
- nosso projeto executa a ROM inglesa do Ryu com runtime próprio no Windows,
  rasterizador RDP em CPU, tradução PT-BR dinâmica, sondas, replay,
  Controller Pak pelo Joybus e várias correções observadas no jogo real.

O componente de maior valor é o backend RT64. Ele oferece o caminho correto
para a pendência de GPU e deve ser prototipado como backend alternativo, sem
remover de imediato o rasterizador CPU que hoje funciona como referência e
contém correções específicas de WPJ2.

## O que foi verificado no código

### Recompilação da CPU

O `wpj2.j1.toml` usa o ELF produzido pelo submódulo `LLONSIT-glitch/wonder`,
entrypoint `0x80000400`, modo MIPS3 de ponto flutuante e saída dividida em
arquivos de 50 funções.

A contagem estática do snapshot encontrou:

- 2.608 corpos `RECOMP_FUNC` nos 53 arquivos `funcs_*.c`;
- 2.666 declarações no `funcs.h`;
- 160 entradas manuais na configuração atual;
- 105 funções substituídas por corpos vazios em `manual_stubs.cpp`.

Isso não confirma a alegação de “8.547 funções” do README. Para comparação,
nosso `runtime/func_table.c` contém 3.651 endereços entre `0x80000400` e
`0x800D6B20`. As métricas não são perfeitamente equivalentes por causa dos
limites derivados do ELF e das funções manuais, mas impedem concluir que a
cobertura de CPU do projeto externo seja maior apenas pela cifra anunciada.

Os 105 stubs vazios se concentram sobretudo na faixa `0x800C82DC`–
`0x800EF790`. A execução por mais de 60 segundos não prova que esses corpos
sejam irrelevantes durante o restante do jogo.

### Gráficos e RT64

`src/graphics/rt64_renderer.cpp` realiza uma integração real, não apenas uma
fachada:

1. entrega RDRAM, DMEM e registradores VI ao `RT64::Application`;
2. cria dispositivo e swapchain Vulkan;
3. habilita RDRAM estendida;
4. identifica o microcódigo de cada tarefa por `loadUCodeGBI`;
5. envia a display list em modo HLE GBI com `processDisplayLists(..., true)`;
6. apresenta pelo pipeline VI do RT64.

Isso é diretamente relevante para nossos problemas de serrilhado, gradientes,
texturas, fog, combiner, coverage, framebuffer e desempenho. O RT64 já possui
TMEM, render modes, color combiner, VI, depth, fog, dither, framebuffer
em GPU, three-point filtering, resolução interna e MSAA.

Limites constatados:

- os registradores DPC e IMEM fornecidos pelo wrapper são artificiais;
- a conclusão de interrupção é deixada ao `ultramodern`, não ao RT64;
- `get_resolution_scale()` retorna sempre `1.0`, embora a configuração interna
  padrão do RT64 use multiplicador 2×;
- o wrapper só oferece escala inteira ou esticada;
- MSAA **não está ligado por padrão**. O RT64 inicia com `Antialiasing::None`;
- o ganho padrão vem de resolução interna 2×, filtro
  `AntiAliasedPixelScaling`, three-point filtering e pipeline VI;
- não há capturas visuais no repositório para comparar fidelidade com Ares,
  Project64 ou nosso corredor;
- os sete itens da própria checklist em `docs/09-validation.md` permanecem
  desmarcados, inclusive áudio, Controller Pak, determinismo e ausência de
  asserts.

Portanto o backend é promissor, mas “RT64 funciona” não equivale ainda a
“abertura fiel”. Ele precisa ser validado quadro a quadro com a ROM e as
referências que já temos.

### RSP e áudio

O projeto recompila `aspMain` diretamente com RSPRecomp:

- ROM offset `0xD9BB0`;
- IMEM base `0x04001080`;
- tamanho `0xE20`;
- todos os 904 alvos alinhados possíveis são declarados para a tabela de
  dispatch indireto.

Tarefas `M_AUDTASK` usam esse microcódigo; tarefas gráficas retornam
imediatamente porque o RT64 interpreta GBI. Essa arquitetura confirma a rota
que já adotamos em `runtime/rsp_native.cpp` e
`src/gerado/rsp_audio/rsp_audio_recompiled.cpp`.

O backend SDL2 de host corrige explicitamente um erro importante: `count` é o
número total de amostras `int16_t`, não de frames estéreo, logo o tamanho
enfileirado é `count * sizeof(int16_t)`, sem multiplicar novamente pelos dois
canais. Ele mantém até 150 ms e descarta áudio antigo quando a fila excede o
limite. Nosso chiado principal já foi resolvido pela rota RSP exata, mas esse
contrato é útil para a futura troca de WinMM por SDL2 ou WASAPI.

Não convém copiar o descarte de áudio antigo sem teste: retirar dados do começo
da fila cria uma descontinuidade. Nosso backend atual deliberadamente espera
por um slot para não produzir esse tipo de estalo.

### Entrada

Há suporte SDL2 para teclado, controle, hot-plug, analógico e rumble. Porém:

- A, B, Z, Start e D-pad são mapeados;
- analógico esquerdo é mapeado;
- **nenhum C-Button é mapeado**, apesar do comentário citar seus bits;
- L e R do controle físico viram Z, não os botões L/R do N64;
- o teclado não gera analógico;
- o próprio decomp mostra uso real dos quatro C-Buttons pelo jogo.

Nosso mapeamento atual já cobre os C-Buttons e os ombros no teclado. O projeto
externo é útil principalmente como modelo para SDL2/gamepads, não como tabela
de botões correta.

### Controller Pak

`controller_pak.cpp` está compilado e suas funções `_recomp` são chamadas pelo
código gerado. Ele implementa uma imagem de 32 KiB, diretório, inodes,
alocação, leitura, escrita, exclusão, CRC e persistência em arquivo.

Ainda assim:

- o README declara a função não testada;
- a API de dispositivo reporta `Pak::None`;
- a implementação substitui rotinas PFS de alto nível, enquanto nosso runtime
  modela os comandos Joybus 0x02/0x03 e preserva a interação original;
- não há evidência de um save criado e recarregado com sucesso.

É uma boa referência para auditar formatação, diretório e inode do nosso MPK,
mas não há motivo para trocar a implementação atual já validada.

### Patch inglês e textos

O executável é gerado a partir do código japonês. Para inglês, o projeto aplica
o IPS do Ryu aos dados da ROM e tenta aplicar ao ELF apenas alterações de código
consideradas seguras. Os scripts documentam problemas reais:

- o IPS altera bytes dentro de instruções e não pode ser copiado cegamente
  para o ELF;
- a área do Fast3D é anulada pelo patch para emuladores HLE e precisa ser
  preservada no fluxo recompilado;
- houve criação descontrolada de threads ao misturar código e dados de versões
  diferentes;
- existem várias versões experimentais do aplicador (`safe`, `smart`,
  `headerfix`, `audiofix`).

Nosso projeto evita parte desse problema porque trabalha diretamente com a ROM
inglesa e possui tradução PT-BR em runtime. Não devemos substituir esse
pipeline pelo aplicador do projeto externo. Os scripts são evidência útil para
explicar divergências entre a ROM japonesa do `wonder-source` e a ROM Ryu.

### Save state, assets e tradução

O projeto externo não implementa save state, replay determinístico, catálogo
de assets, extração de imagens, tradução PT-BR nem substituição dinâmica de
texto. Nessas áreas nosso projeto está adiante.

## Comparação resumida

| Área | Nosso projeto | `wpj2-recomp` | Melhor referência atual |
|---|---|---|---|
| Plataforma testada | Windows | Linux/Wayland/Vulkan | depende do alvo |
| CPU recompilada verificável | 3.651 entradas | 2.608 corpos + 105 stubs vazios | nosso, por cobertura observável |
| Gráficos | RDP CPU específico | RT64 GPU/HLE GBI | externo, como backend futuro |
| RSP de áudio | RSPRecomp ativo | RSPRecomp ativo | equivalentes em arquitetura |
| Saída de áudio | WinMM temporizado | SDL2 queue | externo para backend multiplataforma |
| C-Buttons | mapeados | ausentes | nosso |
| Controller Pak | Joybus/MPK validado | PFS alto nível, não testado | nosso |
| PT-BR/acento | implementado | ausente | nosso |
| Checkpoint | replay F2/F4 | ausente | nosso |
| Validação visual | capturas e Project64 | sem capturas/checklist aberta | nosso |

## Plano de aproveitamento recomendado

### Prioridade 1 — backend RT64 experimental

Criar um backend gráfico alternativo, selecionável em build ou runtime, que
receba as mesmas tarefas gráficas hoje entregues ao nosso RSP/RDP. O primeiro
marco deve ser a abertura completa sem tradução aplicada por sobre o quadro e
com captura determinística dos mesmos pontos do Project64.

Não remover o rasterizador CPU. Ele continua sendo necessário para:

- comparar comandos e estados antes da GPU;
- manter o executável atual funcional;
- isolar regressões de RT64 e de integração;
- preservar as correções específicas já descobertas.

Ordem sugerida:

1. compilar RT64/N64ModernRuntime para Windows em alvo separado;
2. alimentar RDRAM, DMEM, VI e display lists reais;
3. validar ENIX/GIVRO/J2 e transições 2D;
4. validar o corredor com resolução interna 1× e 2×;
5. testar three-point filtering e `Upscale2D` separadamente;
6. somente depois habilitar MSAA 2×/4× e comparar bordas, texto e sprites;
7. manter fallback CPU no `TESTAR.bat`.

### Prioridade 2 — usar RT64 como oráculo de renderização

Mesmo antes de integrá-lo ao executável principal, gerar capturas do projeto
externo nos mesmos quadros do Project64 e do nosso runtime. Isso permitirá
distinguir:

- erro na display list/dados da ROM;
- erro no nosso interpretador Fast3D;
- erro no rasterizador RDP;
- simples diferença de VI/upscale/anti-alias.

### Prioridade 3 — backend SDL2 de host

Planejar SDL2 ou WASAPI para substituir WinMM apenas depois do backend gráfico.
Preservar a síntese RSP atual e alterar somente a fila de apresentação. Medir
submissões, frames pendentes, underruns e estalos; não adotar automaticamente
o descarte do buffer mais antigo.

### Prioridade 4 — auditorias pontuais

- comparar o formato inicial do MPK externo com `runtime/mempak.c`;
- importar para documentação os offsets e a configuração do `aspMain`;
- usar o decomp `wonder` atualizado para nomes, structs e fluxo de cenas;
- mapear SDL2/gamepads quando o frontend deixar de ser Win32;
- não importar os 105 stubs vazios nem o pipeline IPS experimental.

## Veredito

O repositório encontrado é útil o bastante para alterar nossa estratégia de
gráficos: em vez de continuar indefinidamente adicionando aproximações de
anti-alias e VI ao rasterizador CPU, devemos preparar uma rota RT64/GPU
paralela. Ele não demonstra, por si só, maior completude jogável nem fidelidade
visual superior em WPJ2; faltam playthrough, capturas comparativas e validação
dos próprios itens declarados.

Em resumo: usar sua arquitetura gráfica e de frontend como referência;
preservar nosso código de jogo, tradução, entrada, save, testes e runtime
validado até que a rota GPU passe pelas mesmas evidências.

### Implementação aproveitada no runtime local

Em 25/08/2026, a semântica `OtherMode::textPersp()` do RT64 foi portada para o
rasterizador CPU local. A ROM alterna efetivamente perspectiva e interpolação
afim durante a abertura; a execução medida contou aproximadamente 49,7 M e
18,0 M pixels em cada rota, respectivamente. A compilação e uma execução até
o corredor foram concluídas sem falhas. Detalhes e números completos estão em
`renderizacao_3d_fast3d.md`.

### Validação executável paralela do RT64

Em 25/08/2026, a referência foi configurada e compilada integralmente no
Debian WSL2, alvo `wpj2`, com RT64/Vulkan e N64ModernRuntime. O dump japonês
fornecido como `Wonder Project J2 - Japan.n64` está em ordem V64 apesar da
extensão; após a troca de cada par de bytes, o Z64 resultante apresentou MD5
`0FF1F8628D8FE69582DB54572D2BEA79`, exatamente o valor documentado pelo
projeto externo.

Duas falhas no snapshot externo precisaram ser corrigidas para a execução:

1. `src/main.cpp` documentava o XXH3 correto (`068094CAA8D79C24`), mas
   registrava a constante incorreta `67E5F70F31E1E6D7`, fazendo o librecomp
   apagar e rejeitar a ROM correta;
2. o frontend chama `recomp::start_game` antes de `recomp::start`. Assim, a
   thread de VI entendia que o jogo já estava ativo e não inicializava o modo
   VI provisório, dereferenciando `OSViMode` nulo. A inicialização explícita
   de `set_dummy_vi(false)` antes da thread eliminou o crash.

O Mesa 22 do Debian ainda caiu dentro de `libvulkan_lvp` ao criar pipelines
do RT64. A atualização para Mesa 25.0.7 dos backports corrigiu essa falha. O
WSL desta máquina continua expondo `llvmpipe`, portanto o teste mede fidelidade
e integração, não desempenho representativo de GPU física.

Resultado visual validado pelo usuário no corredor: qualidade praticamente
igual à do Project64, com gradientes limpos, bordas estáveis, texturas e
iluminação coerentes. Isso demonstra que os dados/display lists da cena são
suficientes e localiza a diferença principal no backend RDP/VI CPU do nosso
runtime, não nos assets ou na geometria da ROM.

O áudio da referência ficou perceptivelmente dessincronizado e inferior ao
do runtime local. Como o RT64 está sobre `llvmpipe`, a carga do renderizador
em CPU pode agravar a cadência do SDL2; independentemente da causa final, o
backend externo não deve substituir nossa síntese RSP nem a cadência virtual
já validada. O alvo de integração passa a ser RT64 para vídeo, preservando
nosso áudio, tradução, entrada, Controller Pak e replay.

O perfil persistente `TESTAR.bat rt64_ref` abre essa rota em paralelo com a
ROM japonesa e sem tradução PT-BR. O backend padrão permanece inalterado.
