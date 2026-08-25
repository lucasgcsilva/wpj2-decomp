# Integração das legendas PT-BR

## Resultado validado em 2026-08-22

- O carregador do catálogo rejeitava silenciosamente o cabeçalho TSV com CRLF.
  A comparação agora remove `CR` e `LF` antes de validar o cabeçalho.
- O interceptador de `func_80090E58` passou a manter cursores independentes
  para a fonte inglesa e para a tradução PT-BR.
- Traduções maiores usam um buffer expansível na metade alta da RDRAM do host;
  elas não são mais truncadas ao comprimento inglês.
- A aplicação direta ao cartucho em memória só ocorre quando a tradução cabe
  no recurso original. Traduções maiores ficam para o interceptador dinâmico.
- Um teste isolado confirmou expansão, retomada da digitação em mais de uma
  chamada, término correto e ausência de sobrescrita do recurso seguinte.
- A área expansível aceita tanto a RDRAM de 8 MB já mapeada pelo executável
  quanto uma região apenas reservada em testes, sem tentar recometer um
  `MapViewOfFile` existente.
- O smoke test do executável carregou o catálogo e encontrou 2.889 ocorrências
  que cabem diretamente e 2.199 ocorrências maiores tratadas dinamicamente.

## Artefato de validação

`build/wpj2_legendas_check.exe` é diagnóstico e não substitui os protótipos
estáveis. `TESTAR.bat` seleciona temporariamente o perfil `legendas` para a
validação visual pelo usuário.

## Limitação atual

Os acentos são dobrados para ASCII até o atlas receber glifos PT-BR. A próxima
validação deve confirmar em jogo textos curtos (patch direto), longos (buffer
dinâmico) e a digitação progressiva.

## Correção de rota após a primeira validação visual

A execução de 2026-08-22 permaneceu em inglês. O log provou que o wrapper de
`func_80090E58` recebeu somente contadores da abertura (`5000`, `0`, `12079`),
e não as falas da caixa de diálogo. A frase observada, `Messala: Yes, that is
correct...`, também não existe como ASCII na ROM nem no catálogo extraído.

Conclusão: o catálogo atual está completo apenas em relação às cadeias ASCII
extraídas, mas não cobre ainda o script codificado usado pela tradução de Ryu.
O perfil de legendas passa a gerar, no F5, uma imagem lógica dos 8 MB de RDRAM
para localizar o fluxo já decodificado e seu consumidor. Até essa rota ser
interceptada, a integração visual das falas não deve ser considerada validada.

## Rota real localizada nas capturas F5

As capturas 1 e 2 de 2026-08-22 localizaram a fala inglesa já descompactada:

- ponteiro global ativo: `0x80157864`;
- primeiro buffer observado: `0x803554A0`;
- segundo buffer observado: `0x8033EBC0`;
- controle de cor/nome: bytes `E2 02`;
- texto terminado em NUL, com `0A` como quebra de linha.

O runtime agora consulta esse ponteiro a cada retrace, remove apenas os
controles para pesquisar o catálogo, monta a versão PT-BR em `0x807F4000`,
reinsere os controles do nome e redireciona o ponteiro global. Um teste sobre
o dump real produziu corretamente:

```text
<E2><02>Siliconiano XIII<E2><02>: Entao qualquer sonho
poderia se realizar?
```

As duas falas presentes nas capturas foram adicionadas ao catálogo. Falas
novas sem entrada passam a ser registradas uma única vez como
`dialogo_sem_traducao`, preservando `\\n` no TSV de diagnóstico.

### Ajuste de sincronização

A primeira validação desta rota registrou `dialogo_traduzido`, mas a tela
continuou inglesa. Isso prova que reconhecimento, catálogo e buffer estavam
corretos, porém o redirecionamento no retrace ocorria depois da composição da
textura. A atualização foi movida também para `recomp_trace`, na entrada de
cada função recompilada. Assim o ponteiro é trocado entre a rotina que monta a
fala descompactada e a função seguinte que a consome; o retrace permanece
somente como fallback.

### Resultado do segundo teste de sincronização

As capturas `f5_001` e `f5_002` mostraram as duas falas em inglês, enquanto o
mesmo processo registrou `dialogo_traduzido` para a segunda fala. Portanto a
cadeia é publicada e consumida dentro da mesma função recompilada; não existe
uma fronteira de `recomp_trace` em que apenas redirecionar o ponteiro seja
suficiente.

Para obter uma validação visual concreta sem interferir no RDP/3D, o perfil de
diagnóstico agora mantém a detecção pelo ponteiro real e compõe a tradução no
framebuffer host, imediatamente antes da apresentação. Somente o interior da
caixa é substituído; moldura, cenário e estado da RDRAM permanecem intactos.
Esta é uma ponte provisória: confirma o catálogo e as falas em jogo, mas a
integração nativa definitiva ainda deve localizar a rotina que rasteriza os
glifos ou substituir seu atlas.

O executável `build/wpj2_legendas_check.exe` foi recompilado em 2026-08-22 às
16:53 com essa composição. As duas falas atualmente reconhecidas devem aparecer
como `Siliconiano XIII`; falas ainda não mapeadas continuam em inglês e são
registradas para ampliar a cobertura do script real.

### Primeira validação visível e correção de vida útil

O segundo F5 da execução seguinte mostrou a frase PT-BR sobre a caixa. Isso
valida detecção, catálogo e composição host. A primeira implementação, porém,
redirecionava o ponteiro global para o scratch mesmo depois de passar a usar o
framebuffer; por isso a frase ficava presa sobre diálogos posteriores.

O redirecionamento dessa rota foi removido. O ponteiro inglês permanece sob
controle do jogo e cada nova fala pode substituir ou limpar a sobreposição.
Também foi removido o preenchimento marrom sólido: agora apenas a máscara dos
glifos ingleses é reparada usando pixels da própria caixa. A fonte Fixedsys é
somente uma aproximação temporária. A fidelidade final será tratada com um dump
do atlas CI8 no Project64 durante a caixa de diálogo.

### Encerramento da ponte de framebuffer

A validação seguinte mostrou que mesmo a máscara seletiva deixava sombra dos
glifos ingleses, usava métricas incompatíveis e cortava a tradução em três
linhas. Esse resultado encerra a ponte host: ela provou a associação entre fala
e catálogo, mas foi removida do executável de teste por não constituir uma
substituição real.

Com o ponteiro global novamente livre, a mesma rodada capturou três novas falas
reais (`Messala: Yes...`, `Where can this "J"...` e `On a small island...`).
Elas foram traduzidas e anexadas ao catálogo. A sonda
`src/scripts/wpj2_text_route_oracle.js` agora observa no Project64 a escrita em
`0x80157864` e as entradas `80090E58`, `80095E78`, `80095F9C` e `80094230`.
O objetivo é escolher o ponto nativo anterior ao atlas, substituindo os índices
de glifo em vez de redesenhar o framebuffer.

A primeira execução da sonda localizou `0x800199A4`, mas a instrução apenas
publica o bloco de `0x28000` bytes recém-alocado. O texto é escrito depois no
início desse bloco. Uma segunda sonda dinâmica passou a observar somente os
primeiros `0x200` bytes da alocação para identificar o escritor real sem o
custo de monitorar toda a RDRAM.

## Integração nativa pelo carregador de recurso

O watchpoint definitivo localizou `func_80096B38`: a chamada interna de cópia
retorna por `0x80096BF4` e entrega o bloco textual alocado pelo quinto
argumento. O runtime agora envolve essa função e, imediatamente após a cópia:

1. lê a cadeia no bloco alocado pelo próprio jogo;
2. remove apenas os controles `E2 02` para pesquisar o catálogo;
3. converte a tradução para o conjunto ASCII atualmente suportado;
4. reinsere os controles ao redor do nome do falante;
5. sobrescreve a cadeia no mesmo endereço, antes de qualquer glifo.

Não existe mais composição de texto no framebuffer nem redirecionamento para
a metade alta da RDRAM. Para impedir sobrescrita de heap, a substituição só é
feita quando a cadeia codificada cabe no recurso original. As seis falas reais
da primeira cutscene foram editadas para respeitar esse limite sem perder o
sentido.

Um teste unitário com o formato capturado no Project64 confirmou a troca:

```text
E2 02 Messala E2 02 : Yes, that is correct...
E2 02 Messala E2 02 : Sim, esta correto...
```

O artefato `build/wpj2_legendas_check.exe` foi recompilado com esse gancho. A
validação visual continua necessária para confirmar a vida útil do bloco e a
digitação na cena completa.

### Primeira validação do gancho `80096B38`

O log confirmou `recurso_ptbr_nativo` para cinco falas consecutivas, mas a
tela permaneceu inglesa. O dump F5 esclareceu a divergência:

- as cópias publicadas em `0x8033EBC0` e `0x803554A0` estavam em PT-BR;
- as fontes em `0x802B48AC` e a cópia em `0x801705B0` permaneciam em inglês.

Logo `func_80096B38` possui duas saídas relevantes: o bloco alocado publicado
pelo quinto argumento e a fonte resolvida retornada em `v0`. O chamador pode
consumir a segunda diretamente, contornando a primeira. O wrapper de teste foi
ampliado para substituir ambas, sempre no mesmo endereço e somente quando a
tradução cabe no recurso original.

### Validação visual das duas saídas

A execução seguinte exibiu corretamente as falas PT-BR usando a fonte, sombra,
caixa e digitação do jogo. O log registrou duas ocorrências consecutivas de
`recurso_ptbr_nativo` para cada fala, correspondentes ao bloco alocado e à
fonte retornada. A rota nativa da primeira cutscene está, portanto, validada;
as falas posteriores que ainda não pertencem ao catálogo real continuam sendo
registradas para incorporação incremental.

A decompilação pública `LLONSIT-glitch/wonder` confirmou depois essa interface
em C: `func_80096B38` grava `sp28` em `*arg4` e retorna `sp2C` separadamente.

### Simplificação após confirmação por fonte

O gancho antigo de cursor de `func_80090E58` foi removido. Ele operava sobre o cursor já
avançado e gerava pesquisas como `Hum...`, `um...`, `m...` a cada caractere.
Também foram removidas as varreduras de diálogo executadas a cada entrada de
função e a cada retrace. A única rota ativa agora é o recurso completo entregue
por `func_80096B38`, antes da rasterização.

Depois, as capturas F5 provaram que cartões e créditos podem chegar diretamente
a `func_80090E58`, sem passar por `80096B38`. Foi criado um fallback diferente:
ele apenas tenta substituir a cadeia completa no endereço original antes da
formatação. A busca usa hash de quatro bytes e não mantém cursor/scratch.

O pareamento em runtime é automático pela cadeia inglesa completa:
`texto inglês decodificado -> coluna 1 do TSV -> coluna 2 PT-BR`. `id` e
`rom_offset` continuam úteis para extração, auditoria e localização na ROM,
mas não são identificadores de diálogo consumidos pelo jogo. Uma intervenção
manual só é necessária quando a cadeia completa ainda não existe no catálogo
ou quando a tradução codificada não cabe no espaço do recurso inglês.

### Consolidação da rodada manual anterior

A execução validada pelo usuário durou 82,202 s de parede, produziu 79,265 s
de PCM (razão 0,964), 4.891 retraces, 2.403 tarefas gráficas e 2.427 tarefas
de áudio. Ela exibiu as primeiras falas em português e revelou quatro cadeias
completas posteriores ainda ausentes; todas foram anexadas ao TSV nesta rodada.
O WAV e os logs brutos foram descartados depois da consolidação, conforme a
regra da pasta `temp`.

### Correção da unidade de extração

O catálogo histórico varria blocos ASCII e quebrava recursos nos controles
`E0/E1/E2` e nas divisões do banco. O dump F5 contém 2.288 ocorrências válidas
a partir de `0x29FE00`, agrupadas em 2.137 fontes completas. Apenas 54 tinham
uma chave completa já traduzida; portanto a tradução de fragmentos não media a
cobertura real em runtime. A nova fila canônica é
`textos/recursos_completos_en.tsv`.

## Tradução integral dos recursos conhecidos — 22/08/2026

O catálogo consolidado contém 2.137 fontes completas conhecidas: 2.136 do
banco estável reproduzido em três dumps do Project64 e o cartão dinâmico
`The Siliconian Empire`. Das fontes completas, 54 já possuíam tradução
validada e 2.083 entraram no novo pipeline local.

O processamento foi retomável e dividido em tradução bruta, revisão isolada e
auditoria. A tentativa de fornecer falas vizinhas como contexto foi rejeitada,
pois os modelos locais misturavam conteúdos entre índices. A revisão definitiva
trata cada recurso isoladamente. O resultado final ficou em:

- 2.056 recursos com revisão da LM local;
- 27 correções manuais de inglês residual, nomes e contaminação;
- 54 traduções anteriores preservadas;
- zero pendências na auditoria de inglês, mojibake, glossário, nomes e expansão
  semântica suspeita.

As 2.083 chaves novas foram incorporadas a `traducao_ptbr.tsv`, que passou a
5.745 entradas. `MAX_ENTRIES` foi ampliado de 4.096 para 8.192, o teste
`test_legendas_recursos.exe` passou e `build/wpj2_legendas_check.exe` foi
recompilado.

### Limite de integração ainda aberto

694 traduções completas ocupam mais bytes que o recurso inglês correspondente.
Elas estão traduzidas e revisadas, mas o interceptador atual recusa a troca
in-place para não sobrescrever o recurso seguinte. Esse número é uma pendência
do alocador/redirecionamento do runtime, não da tradução. As demais fontes que
cabem podem ser substituídas pelo caminho atual.

O runtime agora ignora os controles ao pesquisar a fonte, preserva cada código
e argumento e os reinsere na posição correspondente da tradução. Foram
validados em teste: texto estático, cor do nome do falante e controle interno
`E0 01`. As três capturas desta rodada receberam chaves completas PT-BR.

### Validação ampla pelo usuário — 22/08/2026

O teste interativo seguinte confirmou aumento perceptível da quantidade de
textos em português ao longo das cutscenes. Ele também confirmou duas classes
de trabalho ainda abertas: recursos que continuam em inglês durante rotas não
cobertas e traduções automáticas com formulação inadequada. Portanto, “2.137
recursos conhecidos processados” mede o catálogo observado, não equivale a
revisão humana integral nem a cobertura de todas as rotas do jogo.

Os três pontos F5 dessa execução chegaram ao caminho de texto nativo. O fim do
log contém tanto `recurso_ptbr_nativo` quanto `recurso_ptbr_longo`; este último
confirma que parte do inglês visível é explicada pelo limite seguro da troca
in-place. A execução durou 365,216 s, com 190,244 s de WAV, 21.860 retraces,
10.795 tarefas gráficas e 10.904 tarefas de áudio. Os dumps, WAV e capturas
brutos foram descartados após esta consolidação.

## Rótulos rasterizados dos menus — tentativa rejeitada em 24/08/2026

O formatador textual não recebe `Day` nem `Progress`. A sonda RDP confirmou
que a tela de seleção de diário chega como uma imagem CI8 dinâmica de
`256x168`, já com as palavras inglesas rasterizadas. Portanto esses rótulos
não podem ser resolvidos pelo pareamento normal do TSV.

Foi testado reconhecer a tela pelas três fichas coloridas, apagar os pixels
ingleses e redesenhar `Dia`/`Avanço` no framebuffer. O teste interativo
invalidou a abordagem: ficaram resíduos de sprites e o cursor passou a disputar
os mesmos pixels. A alteração foi removida por completo. Esses rótulos devem
ser substituídos no recurso nativo ou no escritor CPU que o constrói, nunca por
uma edição tardia do framebuffer.

Na mesma rodada, a aparente duplicação do cursor e parte do "lixo de sprite"
foram separados do problema textual: o quad translúcido de seleção tinha
`G_TEXTURE` desligado, mas o rasterizador 2D amostrava o tile anterior. A
correção global de `G_TEXTURE_OFF` recuperou o realce e a ordem visual sem
redesenhar nada. `Day` e `Progress` continuam sendo pixels de uma imagem CI8
dinâmica em torno de `0x80358F50`; traduzi-los nativamente permanece pendente.

A tela seguinte (`Message Speed`/`Bird's Speed`) também é gráfica. Ambas
permanecem pendentes até o hook no escritor da imagem dinâmica.

## Segurança do patch estático e glifos que colidem com `printf`

O congelamento observado 5–6 segundos depois de confirmar `End` foi isolado na
entrada `It's morning...` → `É de manhã...` (`0x008006DC`). O byte reservado
para o glifo `ã` é `%`; no cartucho essa cadeia ainda atravessa `sprintf` e o
glifo passa a ser interpretado como formato. O áudio continuava porque sua
thread não dependia da thread principal presa em `vsprintf`.

Traduções que geram essa colisão não são mais gravadas no cartucho. Elas devem
ser aplicadas somente pelo interceptador do recurso vivo, depois da etapa de
formatação. Não resolver isso duplicando `%`: recursos que passam por mais de
um formatador voltariam a expor um `%` na passagem seguinte.
