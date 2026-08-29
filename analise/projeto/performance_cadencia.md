# Cadência do runtime e RT64

## Medição de 29/08/2026

O relógio emulado não estava lento na média: 1.500 retraces em 25 segundos e
1.200 em 20 segundos, exatamente 60 Hz. O engasgo vinha da apresentação.

O retorno de `hle_deliver_events` misturava duas situações: um VI verdadeiro e
uma thread acordada por RSP/SI antes do prazo do VI. `recomp_poll` interpretava
as duas como quadro e chamava o RT64. A primeira medição mostrou 65–77
apresentações/s, intervalos de até 117 ms, embora o relógio permanecesse em 60
Hz. Ao apenas separar os códigos apareceu o defeito complementar: o scheduler
ocioso entregava VI sem apresentar, produzindo efeito de slide show.

A correção final centraliza `video_present_pos_retrace` no próprio evento VI.
Conclusões de RSP/SI continuam acordando a thread, mas não apresentam; tanto o
scheduler ativo quanto o ocioso apresentam uma vez por VI. Em execução com
áudio e RT64, foram medidos:

- 1.200 retraces em 20 s;
- 1.150 apresentações depois da inicialização do backend;
- 61,31 apresentações/s no intervalo observado (a diferença inicial é o tempo
  anterior à criação do RT64);
- custo médio de `RT64::updateScreen`: 0,032 ms, máximo 0,144 ms.

Portanto a GPU/apresentação não é o gargalo. Restam picos do trabalho
cooperativo da CPU/RSP e, em menor grau, da fila WinMM: com som foram 42
intervalos acima de 33 ms em 1.149; sem enviar som ao Windows foram 22 em 851.
A fila de áudio explica parte dos picos, mas removê-la sacrificaria o áudio já
validado e não eliminaria o restante. A sondagem `WPJ2_VI_PERF=1` e
`WPJ2_RT64_PERF=1` fica disponível para localizar os próximos pontos sem custo
no perfil normal.

Outra carga removida do perfil normal foi o tracing completo: antes, cada uma
das dezenas de milhares de chamadas recompiladas por segundo fazia consulta ao
scheduler, atualizava trilha e executava busca binária na tabela de 3.651
funções. Agora `WPJ2_TRACE_DETAIL=0` conserva somente o último endereço para
falhas. `WPJ2_TRACE_DETAIL=1` reativa contagens e trilha em sondagens dirigidas.

## Isolamento do engasgo no corredor (slot 1)

O mesmo estado foi medido com e sem envio de áudio ao Windows. Ambos tiveram
exatamente 46 intervalos de apresentação acima de 33 ms em aproximadamente 25
segundos. Logo, o áudio pode alterar a percepção, mas não é a origem do engasgo
visual desse trecho.

A medição ao redor de `RT64::processDisplayLists` encontrou custo médio inicial
de 7,795 ms por tarefa gráfica (o pico de 468,544 ms era a primeira
inicialização). Essa carga ocupa uma parcela grande dos 16,67 ms disponíveis
para cada quadro e roda no mesmo caminho cooperativo da CPU convidada.

O `N64ModernRuntime` de referência separa esse trabalho numa thread gráfica e
publica SP/DP em momentos distintos. Uma primeira integração desse modelo foi
rejeitada: após somente duas tarefas gráficas o gerenciador do jogo deixou de
produzir trabalho e a cena congelou. Avisar SP/DP fora da ordem exata não é
equivalente a apenas mover `processDisplayLists` para outra thread.

O modo estável foi restaurado integralmente. Na confirmação final pelo slot 1,
em cerca de 11,5 segundos depois da inicialização, o jogo executou 590 tarefas
gráficas e 345 de áudio, apresentou 689 quadros a 60,05 Hz e não congelou. O
custo gráfico médio ficou em 4,660 ms (1,242 ms na última lista); ainda houve
21 intervalos acima de 33 ms. Portanto:

- o travamento experimental está corrigido;
- áudio e frequência média de VI não explicam os engasgos restantes;
- a próxima otimização correta exige modelar o contrato completo da fila
  gráfica/SP/DP, incluindo a continuidade do gerenciador de tarefas, antes de
  reativar assincronia.

## Comparativo de API gráfica e correção adotada

A instrumentação por tarefa mostrou que o custo não era proporcional ao
tamanho da lista: uma lista de apenas 984 bytes bloqueava repetidamente por
60–100 ms. O bloqueio vinha da fila interna do RT64/backend, não da quantidade
de geometria interpretada.

Foram comparadas as APIs sobre o mesmo `slot1.wpstate`, com resolução interna
2x e os mesmos recursos visuais:

| API | tarefas | abaixo de 2 ms | 33 ms ou mais | média |
|---|---:|---:|---:|---:|
| D3D12 automático | 1.132 | 708 | 61 | 5,868 ms |
| Vulkan | 1.074 | 999 | 10 | 4,018 ms |

O número Vulkan inclui o aquecimento da primeira lista (2,31 s numa rodada
sem cache). Depois dele, a grande maioria das listas ficou entre 1 e 2 ms. Em
uma validação final com áudio ligado e o cache já aquecido, 611 de 699 tarefas
ficaram abaixo de 2 ms, somente quatro passaram de 33 ms e a apresentação ficou
em 60,05 Hz.

Medidas aplicadas:

- Vulkan passou a ser o padrão do executável e do `TESTAR.bat`; D3D12 continua
  disponível com `WPJ2_RT64_API=d3d12` para diagnóstico;
- os pipelines genéricos do RT64 terminam de preparar antes de liberar o jogo;
- depois da primeira display list, o prazo de VI é resincronizado, pois o
  aquecimento do backend é tempo do host e não deve causar uma rajada de
  quadros atrasados no jogo;
- a sonda `WPJ2_RT64_PERF=1` agora produz histograma e identifica listas acima
  de 8 ms, sem custo de log no perfil normal.

Isso reduz objetivamente os engasgos, mas não os zera: o passo futuro continua
sendo uma fila SP/DP assíncrona fiel, especialmente para máquinas ou cenas em
que o backend não consiga manter as tarefas abaixo do orçamento do quadro.

## Perfil normal sem sondas contínuas — 29/08/2026

O usuário confirmou que, depois de reiniciar o computador, o engasgo visual do
corredor deixou de ser perceptível em duas execuções. Isso aponta também para
contenção externa ou estado residual do driver/processo; não havia evidência
para alterar novamente a ordem SP/DP já estabilizada.

A revisão encontrou, porém, três custos reais que permaneciam no perfil normal:

- a reprodução do áudio gravava simultaneamente um WAV e convertia todas as
  amostras no fluxo da simulação;
- a tradução abria logs de rota/validação e fazia `fflush` durante o jogo;
- 42 `printf` antigos ainda existiam nas rotinas de carregamento B202C/B23C4,
  4F3E8 e 5E19C, produzindo I/O exatamente nas trocas de recursos/cenas.

O padrão agora apenas reproduz áudio. WAV é opt-in nos perfis `audio`,
`audio_rsp`, `audio_fonte`, `divergencia` e `voz`; os logs detalhados da
tradução pertencem ao perfil `legendas`. Os `printf` gerados só voltam numa
build explicitamente compilada com `WPJ2_GENERATED_DEBUG`.

O CPU recompilado stateful passou de `/O1` para `/O2`. O ganho isolado de CPU
foi modesto (cerca de 3%), mas cria margem para concorrência sem modificar o
formato v6 dos estados. Em validação de 45 s no slot 1, com RT64/Vulkan, áudio e
PT-BR ativos:

- não houve travamento;
- não foi criado WAV oculto;
- uso de CPU foi 7,891 s em 45 s de parede e pico de memória 372,8 MiB;
- a apresentação observada internamente permaneceu em 60,000 Hz;
- os quatro atrasos acima de 33 ms ficaram no aquecimento/reconstrução inicial;
  nenhum novo pico dessa classe apareceu depois de estabilizar;
- os testes `continuation snapshot roundtrip` e `stateful thread file restart`
  continuaram aprovados.

Limite da medição: `RT64::updateScreen` mede submissão, não garante sozinho o
quadro composto que o usuário viu. Uma tentativa com `gdigrab` recebeu uma
imagem estática da swapchain Vulkan e foi descartada como evidência. A
avaliação perceptiva do usuário continua sendo o critério final até a ponte
expor os timestamps da thread real de `PresentQueue`.

## Menu do slot 2 e interpolação de apresentação — 29/08/2026

O menu foi medido a partir do `slot2.wpstate`, com uma sequência idêntica de
direcionais. Desligar integralmente a tradução manteve o custo de CPU, o estado
atingido e a cadência gráfica praticamente iguais; portanto a consulta PT-BR
não é a origem do engasgo informado nesse ponto.

A distinção importante é entre **apresentar a janela** e **produzir uma imagem
nova**. O runtime chama a apresentação em aproximadamente 60 Hz, mas nesse
menu as tarefas gráficas novas aparecem principalmente em intervalos de 33 ms
e, diversas vezes, 50 ms. O custo de processar cada lista no RT64 já está em
geral abaixo de 2 ms depois do aquecimento. Assim, reduzir ainda mais esse custo
não preenche os quadros em que o próprio jogo não produziu uma imagem nova.

O RT64 já contém correspondência entre frames, movimento de transformações e
tiles, e uma fila de quadros interpolados. A ponte mantinha a configuração
`RefreshRate::Original`, que desliga esse caminho. O padrão passou para
`RefreshRate::Display`: a lógica, input, áudio e tarefas originais continuam na
cadência do jogo, enquanto o RT64 pode gerar apresentações intermediárias até
a taxa real do monitor. `WPJ2_RT64_REFRESH=original` conserva uma comparação
A/B sem interpolação.

Validação automática: o slot 2 correto foi restaurado, recebeu movimentos nos
quatro direcionais por 15 s, permaneceu responsivo e encerrou sem falha. A
validação definitiva continua sendo visual, porque os contadores do runtime
medem submissão e não o movimento efetivamente percebido na swapchain Vulkan.

O teste visual posterior continuou engasgado. Uma sonda dentro da fila nativa
explicou por quê: `swap=60`, `target=60`, mas `original=0`. O RT64 só gera
quadros intermediários quando a taxa original é válida e menor que o alvo;
portanto nenhum frame foi interpolado. O perfil padrão voltou a `Original` e a
hipótese fica descartada até a ponte conseguir fornecer uma taxa nominal fiel.

Na mesma execução apareceu um gargalo independente e concreto: `TESTAR.bat`
não definia `WPJ2_DEBUG=0`. A build de sondagem então percorria cada display
list uma segunda vez para estatísticas, mantinha tracing extenso, imprimia quase
mil linhas em oito segundos e exportava arquivos automaticamente. O perfil
`padrao` e a comparação `sem_legendas` agora são release reais; telemetria
continua ligada somente nos perfis especializados.

### Poll não bloqueante, VI real e áudio sem espera — 29/08/2026

A timeline conjunta revelou que `hle_deliver_events` dormia até o próximo VI
tanto no dispatcher ocioso quanto dentro de `RECOMP_POLL` de uma OSThread ainda
executável. A CPU do N64 era, portanto, parada entre retraces. Cenas com mais
callbacks perdiam proporcionalmente mais trabalho, embora o contador VI ainda
marcasse 60 Hz. As duas rotas foram separadas: somente o scheduler sem thread
pronta espera; o poll ativo apenas entrega eventos cujo prazo já venceu.

| Slot | GFX antes | GFX depois | Áudio antes | Áudio depois |
|---|---:|---:|---:|---:|
| 1 — corredor 3D | 51,97/s | 60,00/s | 30,03/s | 30,00/s |
| 2 — menu | 25,31/s | 48,78/s | 29,32/s | 29,92/s |
| 3 — menu pesado | 21,24/s | 51,77/s | 25,82/s | 29,95/s |

Os intervalos de tarefa gráfica acima de 50 ms caíram para zero nos três
testes. A medição seguinte de `VI_ORIGIN` mostrou que os três slots alternam o
framebuffer em 60,00 Hz; contar display lists e informar 30 fps ao RT64 era uma
classificação errada. A ponte agora informa 60 Hz.

O segundo bloqueio estava no WinMM: quando os oito slots enchiam, a thread do
jogo esperava 5--12 ms. A correção adaptativa que alongava PCM foi removida,
pois era ela que mantinha a fila cheia depois do scheduler estabilizar. A saída
usa quatro blocos iniciais, doze slots físicos e nunca espera. Na validação
final do slot 3: VI 60,04 Hz, GFX 60,04/s, `VI_ORIGIN` 59,93 Hz, áudio 29,96/s,
zero underflows, zero descartes e zero esperas.

Também foi removido I/O de alta frequência que ignorava `WPJ2_DEBUG=0`: cada
movimento do cursor imprimia e dava `fflush` tanto na janela quanto no PIF. Os
perfis de diagnóstico continuam preservando esses logs.
