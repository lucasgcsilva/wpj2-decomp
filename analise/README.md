# Estrutura das análises

Esta pasta contém somente resultados que já foram interpretados e ainda têm
valor para o projeto. Dados recém-gerados nunca entram aqui diretamente: eles
são produzidos em `../temp`, analisados, resumidos ou agrupados e só então
incorporados.

## Estrutura

```text
analise/
├── README.md
├── projeto/
│   ├── README.md
│   ├── audio_primeira_divergencia.md
│   ├── configuracoes_audio.md
│   ├── inventario_execucao.md
│   ├── texto/
│   ├── graficos_historico/
│   └── codigo/
│       ├── callgraph.txt
│       ├── cop0_usage.txt
│       └── hw_signatures.txt
└── oraculo/
    ├── README.md
    ├── audio/
    │   ├── deep/              # captura profunda ainda usada nas comparações
    │   ├── replay/            # amostras de estado já validadas
    │   └── validacoes/        # casos mínimos reproduzíveis
    ├── graficos/              # logs e CSVs interpretados do Project64
    └── imagens/               # referências visuais do emulador
```

## Regra de incorporação

1. Project64 e executáveis recompilados escrevem apenas em `../temp/oraculo`
   ou `../temp/projeto`.
2. A rodada é comparada com os resultados existentes.
3. A conclusão é anexada ao relatório temático apropriado. Somente a menor
   evidência necessária para reproduzi-la é preservada nesta pasta.
4. O conteúdo processado de `../temp` é removido ao fim do ciclo.

Os relatórios históricos que já são versionados continuam na raiz para não
quebrar o histórico do Git. Este índice os considera parte da documentação:

- `../ANALISE_AUDIO.md`: investigação principal do áudio;
- `../RELATORIO_DECOMPILACAO.md`: histórico técnico consolidado;
- `../PENDENCIAS.md`: problemas conhecidos ainda não resolvidos;
- `../PLANEJAMENTO.md`: planejamento geral.
