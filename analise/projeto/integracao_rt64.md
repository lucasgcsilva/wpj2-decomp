# Integração híbrida do RT64

## Resultado

Em 25/08/2026 o RT64 deixou de ser apenas uma referência externa e passou a
renderizar as tarefas gráficas reais do runtime deste projeto. A validação
interativa percorreu a abertura e o corredor 3D com:

- qualidade visual considerada equivalente ao Project64;
- legendas PT-BR e caracteres acentuados do runtime local;
- áudio RSP/cadência local, sem adotar a saída dessincronizada da referência;
- input, Controller Pak, saves e replay locais;
- processo responsivo e sem display lists rejeitadas no trecho observado.

O log canônico do perfil registra:

```text
[rt64] backend grafico nativo ativo; audio/input/PT-BR permanecem locais
```

## Fronteira da integração

`runtime/rsp.c` continua recebendo a `OSTask` completa. Tarefas de áudio
(`M_AUDTASK`) seguem para o microcódigo de áudio recompilado. Somente tarefas
gráficas (`M_GFXTASK`) entregam ao RT64:

- RDRAM compartilhada;
- DMEM e IMEM do SP;
- endereço/tamanho da display list;
- endereços de microcódigo e dados de microcódigo;
- registradores VI usados na apresentação.

A DLL `src/rt64_bridge/wpj2_rt64_bridge.cpp` hospeda `RT64::Application` na
mesma janela Win32 criada por `runtime/video.c`. `runtime/rt64_backend.c`
carrega essa ponte dinamicamente, de modo que o executável principal não fica
obrigatoriamente ligado ao RT64. Se a ponte estiver ausente ou falhar ao
inicializar, a tarefa volta ao rasterizador CPU.

## Build e execução

```bat
tools\build_probe.cmd rt64
TESTAR.bat
```

Os binários regeneráveis ficam em `build/rt64_runtime`:

- `wpj2_rt64_bridge.dll`;
- `SDL2.dll`;
- `dxcompiler.dll`;
- `dxil.dll`.

O backend foi compilado nativamente com MSVC/Ninja. O SDK Windows disponível
é anterior a GPU Upload Heaps; como o caminho validado é Vulkan, o tipo D3D12
opcional e não utilizado é compatibilizado com `D3D12_HEAP_TYPE_DEFAULT` no
CMake da ponte.

## Perfis separados

- `padrao` (ou o alias `rt64`): integração real, ROM Ryu, PT-BR e subsistemas
  deste projeto;
- `rt64_ref`: executável independente de `Vmarcelo49/wpj2-recomp`, ROM
  japonesa e áudio externo; serve apenas como oráculo visual;
- `cpu`: backend CPU anterior, mantido para comparação e fallback.

## Controles e janela validados

- F5 captura o cliente já composto pelo RT64/DWM e continua gravando
  metadados VI, RDRAM textual e a próxima AList de áudio. A captura foi
  revalidada em 800×600 depois de redimensionar a janela.
- A janela Win32 é redimensionável e conserva o cliente em 4:3 durante o
  arraste, com mínimo de 320×240. Uma proposta externa de 1000×500 foi
  corrigida pelo `WM_SIZING` para cliente 984×738.
- Enquanto F11 está pressionado, a cadência nominal passa a 8× e a fila de
  áudio hospedada é descartada. A medição real foi de 120 retraces/2 s no
  normal e 881 retraces/2 s no avanço, ou 7,34× sob a carga observada.

## Limites ainda não validados

- A abertura e o primeiro corredor foram validados, mas cenas tardias, menus e
  combinações RDP menos frequentes ainda precisam de uma passagem completa.
- A dimensão da janela agora é livre em 4:3; resolução interna, upscale e
  filtros do RT64 ainda são definidos na ponte e poderão virar opções de
  usuário numa fase posterior.
