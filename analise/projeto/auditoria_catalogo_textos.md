# Auditoria de cobertura do catálogo textual

Atualizado em 22/08/2026.

## Resultado confirmado

`textos_suplementares_en.tsv` não é um inventário completo. Ele contém apenas
cadeias ASCII legítimas fora do banco principal e deliberadamente exclui fontes
já cobertas por outros catálogos. Por isso `The Siliconian Empire` não aparece
nele.

Três dumps limpos do Project64, nas tarefas 1, 300 e 471, produziram o mesmo
banco estável: 2.289 ocorrências e 2.136 fontes completas a partir de
`0x29FE00`. A igualdade entre momentos diferentes demonstra que esse banco é
uma base reproduzível para a tradução em massa.

`The Siliconian Empire` foi observado separadamente em `0x29FE40` durante a
cena e já consta de `recursos_completos_en.tsv` e `traducao_ptbr.tsv`. Trata-se
de um recurso dinâmico adicional; ele não existe no banco estável dos três
dumps de áudio.

## Correção de contaminação

O catálogo anterior havia sido reconstruído de uma execução na qual a camada
PT-BR já modificava a memória. Cinco fontes nos endereços `0x2FC7A6`,
`0x2FC7C6`, `0x2FC7E2`, `0x2FDB3E` e `0x2FE11C` estavam alteradas ou truncadas.
Elas foram substituídas pelas fontes inglesas íntegras do Project64.

## Limite da afirmação de cobertura

O conjunto consolidado possui 2.137 fontes conhecidas: 2.136 do banco estável
e um recurso dinâmico confirmado. Isso cobre o banco de diálogos disponível
em memória, mas ainda não prova que todos os cartões ou recursos carregados
sob demanda durante todas as rotas do jogo tenham sido observados.

O pipeline usa `source_en` como chave persistente. Novos recursos dinâmicos
podem ser anexados posteriormente e entram apenas na fila incremental; não é
necessário refazer as 2.137 traduções já processadas.
