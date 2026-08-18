# Planejamento — Wonder Project J2 (N64Recomp)

## Objetivo e escopo

Transformar localmente a ROM `Wonder Project J2 - Koruro no Mori no Jozet
(Japan) [T-En by Ryu v1.0].z64` em uma recompilação estática de código MIPS
para C e, em seguida, em um executável nativo de PC. O objetivo inicial é um
*port* executável; isto não é, por si só, uma decompilação legível que recria a
ROM byte a byte.

O N64Recomp aceita metadados de símbolos/segmentos e converte funções MIPS em
C. Ele não descobre automaticamente os nomes das funções nem implementa o
hardware do N64. A camada de runtime, o HLE de libultra, o RSP, o renderizador
e os ajustes específicos do jogo fazem parte do trabalho posterior.

## Etapas

1. **Fixar a entrada e o ambiente.** Registrar o hash da ROM, configurar o
   caminho apenas localmente e confirmar compiladores, CMake, Python e
   submódulos do N64Recomp. A ROM e arquivos derivados não devem ser
   versionados.

2. **Compilar o N64Recomp.** Construir o executável a partir de
   `E:\projetos\N64Recomp` usando CMake 3.20+ e um compilador C++20. Validar a
   execução com uma configuração mínima e registrar a revisão usada.

3. **Preparar o pipeline do projeto.** Adaptar somente a estrutura útil de
   `E:\projetos\superman-decomp`: scripts de inspeção, arquivo de ambiente,
   diretórios ignorados (`build/`, `RecompiledFuncs/`) e logs reprodutíveis.
   Nenhum endereço, símbolo ou configuração do Superman deve ser reutilizado
   como dado do Wonder Project J2.

4. **Mapear a ROM.** Delimitar código do boot, dados, microcódigo RSP e cada
   overlay; determinar o endereço VRAM de cada segmento. Para os overlays,
   confirmar cada delta ROM→VRAM com os destinos de `jal` e referências de
   relocação.

5. **Construir os metadados.** Detectar limites de funções, gerar
   `wpj2.syms.toml`, registrar `entrypoint`, seções, tamanhos e nomes
   temporários (`func_XXXXXXXX`). O critério de qualidade é que chamadas
   diretas conhecidas apontem para o início de uma função no segmento correto.

6. **Recompilar e compilar o CPU.** Criar `wpj2.toml`, executar N64Recomp,
   corrigir somente problemas mecânicos de C gerado e isolar instruções não
   suportadas em uma lista de stubs justificada. Compilar tudo em uma
   biblioteca estática antes de tentar um executável.

7. **Criar o runtime de sondagem.** Inicializar RDRAM e ROM com a mesma
   convenção de bytes esperada pelo runtime, executar o entrypoint, resolver
   saltos indiretos e registrar funções e MMIO alcançados. O resultado desta
   etapa é diagnóstico, não um port jogável.

8. **Identificar e substituir libultra.** Localizar rotinas de threads,
   filas, interrupções, PI/SI/AI/VI/SP e DMA por assinaturas e comportamento.
   Nomeá-las nos símbolos para que o N64Recomp possa direcioná-las às
   implementações nativas e implementar o HLE necessário.

9. **Habilitar vídeo, RSP, áudio e entrada.** Integrar um caminho de tarefas
   gráficas/RDP com renderizador compatível, tratar o microcódigo RSP e o
   pipeline de áudio, além de controles e persistência. Esta é a etapa de
   maior risco técnico e deve avançar com marcos de tela, entrada e áudio.

10. **Depurar até jogar e empacotar.** Comparar inicialização e cenas com um
    emulador, corrigir patches específicos do jogo, executar testes de fluxo
    (novo jogo, save/load, troca de telas) e documentar como cada pessoa usa a
    sua própria ROM verificada.

## Portões de avanço

| Portão | Evidência necessária |
|---|---|
| Mapa confiável | Todas as seções executáveis e seus VRAMs; estatística de chamadas cobertas |
| C recompilado | `wpj2.syms.toml`, `wpj2.toml`, log do N64Recomp e lista de stubs |
| Bootstrap | Biblioteca compilada, harness e traço de execução/MMIO |
| Runtime | Threads/filas/DMA progridem além da inicialização sem stubs que retornam silenciosamente |
| Primeira imagem | Uma tarefa gráfica é submetida e apresentada de modo determinístico |
| Jogável | Entrada, áudio, saves e roteiro de regressão básicos passam |

## Regras de manutenção

- Mantenha a ROM fora do repositório e use sempre o SHA-256 registrado no
  relatório para evitar misturar revisões ou patches de tradução.
- Trate cada resultado gerado como reproduzível: script, comando, log e hash
  da ferramenta devem acompanhar a conclusão de cada etapa.
- Registre incertezas em vez de marcar como concluído: um overlay com VRAM
  desconhecido invalida a recompilação daquela seção.
