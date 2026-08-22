# Estrutura de fontes auxiliares

Esta pasta reúne fontes e configurações locais necessárias ao trabalho, mas
que não pertencem aos projetos externos de `../tools`.

```text
src/
├── README.md
├── config/
│   └── oraculo_project64.md
├── scripts/
│   ├── analisar_primeira_divergencia.py
│   ├── comparar_audio_deep_local.py
│   └── comparar_entrada_audio_suspeita.py
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
- projetos de terceiros permanecem em `../tools`.

`RecompiledFuncs` é regenerado por `wpj2.toml`. `RecompiledFuncsTraced` é
regenerado pelos builds de sonda e nunca deve ser editado manualmente.

Scripts já versionados em `../tools` não são duplicados aqui. Somente novos
scripts próprios devem nascer em `src/scripts`; quando estabilizados, podem
ser promovidos ao conjunto versionado de ferramentas.

As tarefas permitidas para modelos locais, incluindo tradução e triagem de
áudio, estão consolidadas em `../DIRETIVAS_LM_LOCAL.md`.
