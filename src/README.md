# Estrutura de fontes auxiliares

Esta pasta reúne fontes e configurações locais necessárias ao trabalho, mas
que não pertencem aos projetos externos de `../tools`.

```text
src/
├── README.md
├── config/
│   ├── oraculo_project64.md
│   └── wpj2_legendas_link.rsp
├── scripts/
│   ├── analisar_primeira_divergencia.py
│   ├── aplicar_correcoes_recursos.py
│   ├── auditar_revisao_recursos.py
│   ├── comparar_audio_deep_local.py
│   ├── comparar_entrada_audio_suspeita.py
│   ├── extrair_assets.py
│   ├── extrair_recursos_texto_rdram.py
│   ├── consolidar_recursos_texto.py
│   ├── processar_recursos_completos_lm.py
│   ├── wpj2_text_route_oracle.js
│   └── wpj2_text_writer_oracle.js
├── tests/
│   └── test_legendas_recursos.c
├── rt64_bridge/              # DLL Windows: M_GFXTASK/VI do runtime -> RT64
│   ├── CMakeLists.txt
│   └── wpj2_rt64_bridge.cpp
├── RecompiledFuncs/          # saída C direta do N64Recomp
├── RecompiledFuncsTraced/    # cópia instrumentada para sondas
└── gerado/
    └── rsp_audio/
        ├── rsp_audio_recompiled.cpp
        ├── rsp_audio.toml
        └── *.bin
```

## Exceções intencionais

- `../runtime` continua no local versionado esperado pelos builds.
- `../textos` contém dados extraídos da ROM e o catálogo local de tradução;
  permanece separado para respeitar as regras de distribuição do projeto.
- `../assets` contém o manifesto versionável; suas extrações locais ficam em
  `../assets/generated` e permanecem ignoradas pelo Git.
- projetos de terceiros permanecem em `../tools`.

`rt64_bridge` é código próprio e liga estaticamente o RT64 localizado em
`../tools/wpj2-recomp/lib/RT64`. `tools/build_probe.cmd rt64` gera a DLL e as
dependências redistribuíveis em `../build/rt64_runtime`; nenhum binário gráfico
deve ser deixado na raiz.

`RecompiledFuncs` é regenerado por `wpj2.toml`. `RecompiledFuncsTraced` é
regenerado pelos builds de sonda e nunca deve ser editado manualmente.

Scripts já versionados em `../tools` não são duplicados aqui. Somente novos
scripts próprios devem nascer em `src/scripts`; quando estabilizados, podem
ser promovidos ao conjunto versionado de ferramentas.

O pipeline antigo, baseado em fragmentos ASCII e em um catálogo suplementar,
foi removido. A unidade canônica agora é sempre o recurso completo entregue ao
renderizador, processado por `processar_recursos_completos_lm.py` e consolidado
por `consolidar_recursos_texto.py`.

As tarefas permitidas para modelos locais, incluindo tradução e triagem de
áudio, estão consolidadas em `../DIRETIVAS_LM_LOCAL.md`.
