# Wonder Project J2 — recompilação estática

Recompilação estática de **Wonder Project J2: Koruro no Mori no Jozet**
(Nintendo 64, 1996) para código nativo no Windows. O código MIPS é convertido
antecipadamente pelo N64Recomp; o runtime deste projeto implementa os serviços
do N64 e usa RT64/Vulkan como backend gráfico padrão.

> Static recompilation of *Wonder Project J2* for Windows. Game code is
> translated ahead of time, while the local runtime provides N64 services,
> PT-BR text, audio, input, Controller Pak and RT64 graphics.

| | | |
|---|---|---|
| ![Abertura ENIX pelo RT64](docs/01-logo-enix.png) | ![Título pelo RT64](docs/02-titulo.png) | ![Corredor 3D e diálogo PT-BR pelo RT64](docs/03-corredor-3d.png) |
| Abertura / Opening | Título / Title | RT64 + PT-BR |

*Capturas do runtime atual com RT64. Não são imagens do rasterizador CPU
antigo nem do Project64.*

---

## Estado atual

O protótipo já inicializa e percorre a abertura com vídeo RT64, áudio nativo,
tradução PT-BR, controles completos e Controller Pak. O backend CPU anterior
continua disponível para diagnóstico, mas RT64 é o padrão.

- vídeo RT64/Vulkan com fallback para o rasterizador CPU;
- áudio pelo microcódigo recompilado do jogo, com raros estalos residuais;
- botões digitais, analógico e C-Buttons;
- Controller Pak e persistência normal do jogo;
- tradução PT-BR dinâmica, incluindo caracteres acentuados;
- bookmark seguro por reinício/replay em F2/F4;
- janela redimensionável em 4:3 e avanço momentâneo de até 8×.

Este ainda é um projeto de desenvolvimento. É necessário fornecer a própria
ROM e ainda não existe uma validação jogável completa até o final do jogo.

---

## Progresso

As porcentagens são estimativas ponderadas de engenharia, não a proporção de
linhas de código decompiladas. Uma implementação pode participar de mais de
uma fase: a tradução, por exemplo, é modernização na Fase 4, mas a fidelidade
da paginação e do posicionamento pertence à Fase 2.

| Fase | Progresso | Objetivo |
|---|---:|---|
| 1. Protótipo funcional | **99%** | Executar o jogo de ponta a ponta, mesmo com imperfeições |
| 2. Fidelidade de execução | **85%** | Reproduzir vídeo, áudio, timing e interface como o N64 |
| 3. Extração total | **15%** | Mapear código e recursos sem regiões opacas |
| 4. Modernização para PC | **45%** | GPU, idiomas, resoluções, recursos e experiência nativa |

### Fase 1 — Protótipo funcional — `99%`

`███████████████████▓` 99%

Concluídos: boot, escalonador por fibers, PI/SI/SP/DP/VI/AI, DMA, tarefas
gráficas e de áudio, entrada, Controller Pak, persistência, RT64 e execução da
abertura/cenas já verificadas.

Para chegar a 100%:

- realizar uma campanha completa até o final;
- corrigir qualquer travamento bloqueante ou subsistema ainda não alcançado.

### Fase 2 — Fidelidade de execução — `85%`

`█████████████████░░░` 85%

RT64 resolveu as principais diferenças de materiais, iluminação,
anti-aliasing, transições e animações 2D/3D. O áudio, controles e cadência estão
funcionais e próximos da referência.

Para chegar a 100%:

- corrigir paginação, scroll e posicionamento de textos PT-BR longos;
- substituir corretamente os rótulos gráficos `Day` e `Progress`;
- eliminar os raros estalos restantes do áudio;
- comparar timing, menus e cenas tardias durante uma campanha completa;
- fechar regressões visuais ou de input descobertas nesse percurso.

### Fase 3 — Extração total — `15%`

`███░░░░░░░░░░░░░░░░░` 15%

Já existem manifesto e extrator de assets, contêiner da fonte mapeado, bancos
de texto integrados e faixas de sequências, tabelas, samples e bancos de áudio
identificadas. Símbolos, callgraph e funções recompiladas também possuem
instrumentação própria.

Para chegar a 100%:

- inventariar e extrair todas as texturas, sprites, paletas, modelos, áudio e
  scripts no formato nativo;
- associar cada asset ao loader e consumidor correspondente;
- decodificar os contêineres hoje apenas mapeados;
- identificar overlays, estruturas e símbolos restantes;
- substituir progressivamente regiões recompiladas opacas por código nativo
  compreendido e documentado;
- tornar a extração reproduzível a partir de uma ROM fornecida pelo usuário.

### Fase 4 — Modernização para PC — `45%`

`█████████░░░░░░░░░░░` 45%

Já entregues: backend GPU RT64, janela redimensionável, tradução PT-BR
carregada externamente, caracteres acentuados, captura F5, avanço F11,
controles de PC, Controller Pak e bookmark por replay.

Para chegar a 100%:

- adicionar opções de resolução interna, upscale e filtros do RT64;
- implementar widescreen e ultrawide sem deformar HUD ou câmeras;
- permitir pacotes de texturas, sprites e fontes em alta resolução;
- finalizar a revisão PT-BR e estruturar seleção de idiomas externos;
- substituir o bookmark experimental por save-states robustos, se viável;
- criar configuração de controles, áudio, vídeo e Controller Pak;
- preparar empacotamento, instalador e experiência de release para Windows.

---

## Como funciona

| Camada | Implementação atual |
|---|---|
| Código do jogo | MIPS convertido para C pelo N64Recomp |
| Libultra e hardware | Runtime próprio: threads, filas, DMA e registradores |
| RSP gráfico | Tarefas reais do jogo encaminhadas ao RT64 |
| RDP e apresentação | RT64/Vulkan por padrão; rasterizador CPU para A/B |
| RSP de áudio | Microcódigo recompilado, executado localmente |
| Entrada e saves | PIF, analógico, C-Buttons e Controller Pak locais |
| Texto | Catálogo PT-BR externo aplicado no consumidor do jogo |

O Project64 é usado como oráculo comportamental. Resultados analisados são
consolidados em `analise/`; saídas descartáveis entram somente em `temp/`.
Hipóteses rejeitadas permanecem documentadas para não serem repetidas.

---

## Requisitos

- Windows x64;
- Visual Studio 2022 ou Build Tools com compilador C/C++;
- CMake e Ninja para a ponte RT64;
- GPU e driver com suporte a Vulkan;
- Python 3;
- uma cópia legal da ROM compatível, fornecida pelo próprio usuário.

ROM, fontes recompiladas e assets extraídos não são distribuídos. As imagens
em `docs/` são apenas capturas reduzidas para documentação.

---

## Compilar e executar

```bat
:: 1. Configure os caminhos locais de ROM, MSVC e Python
tools\env.cmd

:: 2. Gere/recompile o código e construa também a ponte RT64
tools\build_probe.cmd rt64

:: 3. Execute o perfil padrão
TESTAR.bat
```

`TESTAR.bat cpu` força o rasterizador antigo para comparação. Se a ponte RT64
não carregar, o runtime também retorna automaticamente ao backend CPU.

### Controles

| Tecla | Nintendo 64 |
|---|---|
| Enter | START |
| X / Espaço | A |
| Z | B |
| C | Z trigger |
| Q / E | L / R |
| W / A / S / D | D-Pad |
| Setas | Analógico |
| I / J / K / L | C-Up / C-Left / C-Down / C-Right |

| Atalho | Função de desenvolvimento |
|---|---|
| F2 | Substitui o bookmark rápido por replay |
| F4 | Reinicia e reproduz as entradas até o bookmark |
| F5 | Captura imagem e estado de diagnóstico |
| F6 | Captura histórico gráfico |
| F11 pressionado | Avanço nominal de 8×; soltar volta ao normal |

F2/F4 não são save-states tradicionais: o retorno seguro reinicia o runtime e
reproduz entradas, evitando restaurar RDRAM sobre fibers incompatíveis.

---

## Estrutura do projeto

| Caminho | Finalidade |
|---|---|
| `runtime/` | Scheduler, HLE, áudio, vídeo, RT64, PIF e tradução |
| `src/` | Fontes recompiladas locais, scripts, testes e ponte RT64 |
| `assets/` | Manifesto publicável e extrações locais ignoradas |
| `textos/` | Catálogos derivados da ROM, mantidos apenas localmente |
| `analise/` | Evidências já interpretadas e consolidadas |
| `temp/` | Resultado descartável do próximo teste |
| `tools/` | Build, análise e projetos externos de referência |
| `sav/` | Controller Pak, saves e bookmarks locais |
| `TESTAR.bat` | Entrada principal de testes interativos |

Leia [ESTRUTURA.md](ESTRUTURA.md) antes de criar arquivos. O fluxo obrigatório
é: gerar em `temp/`, analisar, consolidar em `analise/` e limpar o temporário.

### Documentação principal

| Arquivo | Conteúdo |
|---|---|
| [RETOMADA.md](RETOMADA.md) | Histórico técnico e ponto atual de retomada |
| [PENDENCIAS.md](PENDENCIAS.md) | Lacunas confirmadas ainda abertas |
| [ANALISE_AUDIO.md](ANALISE_AUDIO.md) | Evidências e hipóteses descartadas de áudio |
| [ESTRUTURA.md](ESTRUTURA.md) | Organização e disciplina de testes |
| [PLANEJAMENTO.md](PLANEJAMENTO.md) | Planejamento geral |

---

## Projetos e referências

Dependências e referências podem existir localmente em `tools/`, mas seus
repositórios não são incorporados ao histórico deste projeto.

### Recompilação e runtime

| Projeto | Uso neste trabalho |
|---|---|
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | Recompilação estática MIPS → C |
| [RT64](https://github.com/rt64/rt64) | Backend gráfico Vulkan atualmente usado por padrão |
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) | Modelo de runtime e integração moderna |
| [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) | Referência para frontend e configuração de recompilações |
| [o1heap](https://github.com/N64Recomp/o1heap) | Referência de alocador determinístico para runtimes recompilados |
| [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) | Referência madura de vídeo, áudio, input e plataforma |

### Wonder Project J2

| Projeto | Uso neste trabalho |
|---|---|
| [wpj2-recomp](https://github.com/Vmarcelo49/wpj2-recomp) | Referência decisiva para RT64, RSPRecomp e runtime moderno |
| [wonder](https://github.com/LLONSIT-glitch/wonder) | Símbolos, funções e comportamento da ROM japonesa |
| [josette](https://github.com/Ruin0x11/josette) | Referência para formatos e extração de sprites |
| [WPJ2 English translation](https://www.seiyuu.info/wpj2/) | Patch e texto inglês usados como base do catálogo PT-BR |

### SDK e oráculos

| Projeto | Uso neste trabalho |
|---|---|
| [Project64](https://github.com/project64/project64) | Oráculo de comportamento, gráficos, áudio e memória |
| [libreultra](https://github.com/n64decomp/libreultra) | Semântica aberta de APIs e estruturas da libultra |
| [sdk-tools](https://github.com/n64decomp/sdk-tools) | Ferramentas e referências do SDK do Nintendo 64 |

Obrigado aos autores desses projetos e ao tradutor Ryu. Código ou dados de
terceiros continuam sujeitos às respectivas licenças.

---

## Método de trabalho

1. **Executar o microcódigo real quando possível.** O áudio recompilado do
   próprio jogo é mais confiável que aproximar comandos por HLE.
2. **Comparar com um oráculo.** Project64, fontes de referência e capturas no
   mesmo estado reduzem hipóteses no escuro.
3. **Medir antes de concluir.** Desempenho, áudio e cobertura são registrados
   junto da imagem percebida.
4. **Preservar resultados negativos.** Uma hipótese rejeitada documentada não
   deve voltar como tentativa sem evidência nova.

---

## Legal

Este repositório contém código e ferramentas originais, além de pequenas
capturas usadas na documentação. Não contém a ROM nem pretende distribuir
assets reutilizáveis extraídos do jogo. Para compilar e executar, use uma cópia
legalmente obtida.

Wonder Project J2 é © Enix / Givro. Este projeto não é afiliado nem endossado
pelos detentores dos direitos.
