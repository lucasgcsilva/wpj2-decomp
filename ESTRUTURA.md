# Estrutura e fluxo de trabalho

Este documento descreve a organização vigente do projeto e o procedimento que
deve ser seguido em novas rodadas de teste. O objetivo é separar fontes,
artefatos transitórios, evidências já interpretadas e dependências externas.

## Árvore principal

```text
project-wonder-j2-decomp/
├── analise/                   # resultados já interpretados e consolidados
│   ├── oraculo/               # conclusões obtidas no Project64/original
│   └── projeto/               # conclusões obtidas no recompilado
├── build/                     # objetos e executáveis regeneráveis (local)
├── docs/                      # imagens selecionadas para a documentação
├── runtime/                   # runtime nativo: HLE, RSP/RDP, áudio, vídeo, PIF
├── src/                       # fontes auxiliares pertencentes ao projeto
│   ├── config/                # configurações e arquivos de link
│   ├── gerado/                # fontes geradas por ferramentas
│   ├── RecompiledFuncs/       # saída C do N64Recomp
│   ├── RecompiledFuncsTraced/ # variante instrumentada para sondas
│   ├── scripts/               # scripts próprios de análise e preparação
│   └── tests/                 # testes unitários do runtime
├── temp/                      # única entrada para resultados ainda não analisados
├── textos/                    # catálogos locais derivados da ROM
├── tools/                     # ferramentas próprias e projetos de referência
├── TESTAR.bat                 # entrada principal para testes interativos
├── RODAR.bat                  # sonda automatizada histórica
├── RODAR_PROTOTYPE.bat        # executa a versão estável do protótipo
├── ORACULO_PROJECT64.bat      # inicia a coleta geral no emulador
├── SONDAR_AUDIO_PROJECT64.bat # coleta especializada de áudio no emulador
└── COMPARAR_AUDIO.bat         # comparação especializada de áudio
```

## Pastas e responsabilidades

### `runtime/`

Contém somente a implementação nativa necessária à execução: inicialização,
escalonador, substituições de libultra, RSP/RDP, áudio, vídeo, entrada/PIF e
integração opcional das legendas. Alterações aqui precisam de validação
proporcional ao risco e de registro em `analise/projeto/`.

### `src/`

Reúne material próprio que não pertence diretamente ao runtime:

- `config/`: arquivos de configuração e resposta de linker;
- `scripts/`: extratores, auditores e comparadores reutilizáveis;
- `tests/`: testes pequenos e determinísticos;
- `RecompiledFuncs/`: C gerado diretamente pelo N64Recomp;
- `RecompiledFuncsTraced/`: cópia gerada com instrumentação;
- `gerado/`: outras saídas geradas, como o microcódigo de áudio recompilado.

Os diretórios gerados não devem ser editados manualmente nem versionados.
Arquivos `.obj` pertencem a `build/`, nunca à raiz do projeto.

### `tools/`

Contém utilitários de build e cópias locais de referências externas, entre
elas N64Recomp, N64ModernRuntime, Project64, Zelda64Recomp, libreultra,
sdk-tools, josette e `LLONSIT-glitch/wonder`. Projetos externos não fazem parte
do repositório principal e só devem ser alterados quando um caminho local do
nosso fluxo exigir isso.

### `textos/`

É local e ignorada pelo Git porque contém conteúdo derivado da ROM. Sua
estrutura canônica é:

```text
textos/
├── traducao_ptbr.tsv          # mapa carregado pelo runtime
├── recursos_completos_en.tsv  # catálogo atual de tradução/revisão
├── apoio/                     # correções e limites do runtime
└── legado/                    # extrações antigas baseadas em fragmentos
```

Novas traduções devem partir de `recursos_completos_en.tsv` e terminar em
`traducao_ptbr.tsv`. Não reativar o pipeline antigo de fragmentos ASCII.

### `analise/`

Recebe somente resultados que já foram lidos e interpretados:

- `analise/oraculo/`: comportamento medido no Project64 ou no jogo original;
- `analise/projeto/`: comportamento medido no executável recompilado.

Preferir poucos documentos canônicos, atualizados por seção ou data. Não criar
um relatório novo para cada execução quando a conclusão puder ser anexada a um
arquivo existente. Capturas pesadas, WAV, PCM, dumps de RDRAM e logs brutos não
devem ser promovidos automaticamente; preservar apenas a menor evidência
necessária e respeitar as regras de conteúdo derivado da ROM.

### `temp/`

É uma caixa de entrada descartável. Toda sonda nova, seja do recompilado ou do
Project64, deve escrever primeiro aqui:

```text
temp/
├── README.md
├── projeto/<nome_da_sonda>/
├── oraculo/<nome_da_sonda>/
└── lm_local/<nome_da_tarefa>/
```

Ao final de cada ciclo, `temp/` deve voltar a conter somente `README.md`.

## Procedimento obrigatório de teste

1. Ler o documento canônico do assunto (`ANALISE_AUDIO.md`,
   `analise/projeto/integracao_legendas.md`, `PENDENCIAS.md` etc.) para não
   repetir hipóteses já rejeitadas.
2. Ajustar o perfil dentro de `TESTAR.bat`. Ele é a entrada principal; não
   criar um novo `.bat` para cada hipótese.
3. Executar `TESTAR.bat`, selecionar o perfil por argumento apenas quando
   necessário e usar F5/F6 conforme a sonda ativa.
4. Confirmar que toda saída nova foi escrita sob `temp/projeto/`. Sondas do
   Project64 devem usar `temp/oraculo/`; tarefas da LM, `temp/lm_local/`.
5. Analisar logs, métricas, capturas e dumps antes de concluir. Comparações
   precisam registrar perfil, duração, hashes/alinhamento quando aplicável e
   as variáveis realmente alteradas.
6. Incorporar somente a conclusão e a evidência mínima no documento adequado
   de `analise/projeto/` ou `analise/oraculo/`. Atualizar `PENDENCIAS.md` quando
   o problema ficar deliberadamente adiado.
7. Excluir os resultados brutos já consumidos. Verificar que resta apenas
   `temp/README.md` antes de encerrar o ciclo.

## Uso de `TESTAR.bat`

- É o substituto único do antigo `JOGAR.bat`.
- Sem argumento, executa o perfil atual definido no próprio arquivo.
- Perfis especializados podem ser selecionados por argumento, por exemplo
  `TESTAR.bat legendas` ou `TESTAR.bat audio_rsp_exato`.
- A janela não possui limite artificial de tempo; fechá-la encerra o teste e o
  terminal associado.
- O script limpa os artefatos antigos do perfil antes da execução, mas a
  limpeza final de toda a pasta `temp` ocorre somente depois da análise.
- Quando uma hipótese exigir outra configuração, modificar este arquivo e
  documentar a linha anterior em comentário quando ela ainda tiver valor de
  comparação.

`RODAR_PROTOTYPE.bat` é reservado ao protótipo estável. Não recompilar nem
substituir uma versão estável automaticamente; gerar nova versão somente por
pedido explícito. `RODAR.bat` e as sondas do Project64 permanecem como entradas
especializadas, não como alternativas ao fluxo interativo principal.

## Validação antes de commit

Executar, no mínimo:

1. `git diff --check`;
2. compilação sintática dos scripts Python alterados;
3. testes unitários relacionados, como `build/test_legendas_recursos.exe`;
4. build ou relink do executável diagnóstico quando o runtime mudar;
5. revisão de `git status --short`, garantindo que ROM, dumps, catálogos
   locais, `.obj`, `.exe`, áudio e capturas não serão versionados.

O README deve creditar projetos externos usados como referência. O repositório
principal versiona nosso runtime, scripts, testes e conclusões; não versiona a
ROM, código recompilado gerado dela, catálogos extraídos nem dependências
terceiras clonadas em `tools/`.

## Estado atual relevante

- `TESTAR.bat` é a entrada principal e seu perfil padrão atual é `legendas`.
- A integração PT-BR faz pareamento pela cadeia inglesa completa; `id` e
  `rom_offset` servem à auditoria, não à seleção em runtime.
- O catálogo conhecido foi processado, mas ainda há revisão humana, recursos
  dinâmicos não observados e traduções maiores que o bloco inglês.
- O áudio ainda possui chiado intermitente e deve seguir as evidências de
  `ANALISE_AUDIO.md`, sem reiniciar hipóteses já descartadas.
- Lacunas visuais adiadas estão em `PENDENCIAS.md`.
