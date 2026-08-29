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

A tela seguinte não pertence à mesma classe. `Message Speed`, `Bird's Speed`,
`Fast` e `Slow` são cadeias textuais copiadas individualmente por
`func_800BD218`: por exemplo, `0x68B924 → 0x80363D50`, sempre em um slot de 48
bytes. O patch estático via apenas os 13 bytes de `Message Speed` e rejeitava
`Velocidade da Mensagem`; o recurso vivo tinha capacidade de sobra.

O hook de cópia agora informa explicitamente os 48 bytes ao interceptador e a
substituição acontece antes da rasterização nativa. Validação visual mostrou
`Velocidade da Mensagem`, `Velocidade do Bird`, `Rápido`, `Lento`, `Fim` e
`Sair`, sem sobreposição de framebuffer e com os realces originais intactos.
Somente `Day` e `Progress` continuam na classe pré-rasterizada.

## Diálogos compostos após a formatação — 25/08/2026

As capturas `f5_001` a `f5_005` mostraram uma classe diferente dos recursos
completos: uma mesma fala alternava inglês, nome dinâmico e português. A ROM
T-En guarda essas mensagens como fragmentos separados por controles, por
exemplo `"The Doctor said " + E0/02 + "-san is from a" + quebra +
"world I can't see."`. O patch estático só conseguia trocar os fragmentos que
cabiam no espaço inglês; por isso a frase aparecia parcialmente traduzida.

A correção não altera o bloco estrutural nem desenha sobre o framebuffer.
Depois de `vsprintf`, em `func_80090E58`, o runtime percorre a cadeia já com
`BEAN`, `Josette` e outros valores resolvidos, substitui pelo maior fragmento
conhecido em cada posição e publica uma única cópia PT-BR na arena acima dos
4 MB. Espaços nas bordas dos fragmentos são preservados mesmo quando a revisão
do catálogo os omitiu, evitando concatenações como `queBEAN` e `Josettee`.

Esse caminho fica deliberadamente restrito ao ponto posterior ao formatador.
Executá-lo em `func_800319B0` ou `func_80096D40` faria o byte reservado ao
glifo `ã` (`%`) voltar a ser interpretado como especificador e reproduziria o
congelamento da thread principal.

O teste `test_legendas_recursos` agora cobre deterministicamente as cinco
mensagens observadas, inclusive a fala longa de Bird, nomes, quebras, espaços
e bytes dos acentos. Build e teste passaram. Replays de menu e da animação
posterior a `Fim` terminaram ativos em `11/24` e `1/1`; o segundo registrou
4.156 tarefas gráficas sem prender a thread principal.

### Controles de ação e consolidação de PT-BR — segunda validação

As capturas seguintes revelaram comandos `E1 <cor>` / `E1 FF` ao redor de
`"Yes"`, `Blue Button`, `R-Button`, `Pad` e equivalentes. Os controles já eram
preservados em pares; o inglês persistia porque esses fragmentos não existiam
no catálogo final. Foram acrescentados `Sim`, `Bom`, `Não`, `Errado`,
`Botão Azul`, `Botão Verde`, `Botão R`, `Gatilho Z`, `Analógico 3D`,
`controle` e conectivos próximos.

Também ficou comprovado que uma mensagem longa pode chegar inteiramente em
PT-BR e ainda permanecer dividida nos limites ingleses. O índice de tradução
agora possui uma segunda busca pela representação PT-BR já codificada. Assim,
fragmentos previamente substituídos são reconhecidos e copiados para uma única
cadeia de arena, sem retradução. O teste unitário cobre uma fala totalmente
PT-BR e uma instrução com quatro pares `E1`; ambos passaram.

O perfil padrão mantém temporariamente a captura de RDRAM no F5. Ela só grava
ao pressionar a tecla e permite verificar o recurso vivo se alguma rolagem
ainda divergir.

### Cobertura dos fragmentos e reflow — 25/08/2026

As capturas seguintes provaram que a extração bruta continha `blue` e `green`
em `0x00800AAD/0x00800AB9`, mas essas chaves curtas não haviam sido promovidas
ao mapa ativo. O tamanho da cadeia não é um critério válido: opções, cores,
botões e conectivos legítimos podem ter poucos bytes. A auditoria nova
`src/scripts/auditar_cobertura_traducao.py` compara cada fonte do banco textual
declarado pelo patch Ryu (`0x00800000..0x0081FFFF`) com o TSV ativo, preservando
aspas e espaços estruturais. A primeira passagem encontrou 98 chaves; depois
da promoção de palavras, ações, nomes e variantes com espaços, o resultado é
**zero fonte extraída sem chave canônica**, com 5.886 chaves ativas.

O dump da arena provou que a fala longa já estava correta em PT-BR. A tentativa
feita nessa data atribuía o lixo a uma dupla quebra e recalculava todas as
linhas com margem de 38 glifos. A validação interativa posterior rejeitou essa
solução: ela própria gerava sobreposição e deslocava controles. A regra vigente
está documentada em “Refluxo de palavras na caixa inferior”: quebras explícitas
são imutáveis e somente uma quebra automática no meio da palavra é antecipada
para o espaço anterior, sem alterar o tamanho da cadeia.

Essa substituição deve permanecer estritamente limitada à faixa de ROM
`0x0068B8E0..0x0068BF48`. Aplicá-la genericamente em toda cópia de
`func_800BD218` alcança recursos técnicos como `SPI1` e pode inserir bytes de
glifo antes de eles atravessarem `printf`, reproduzindo o laço em `vsprintf`
após `Fim`. O replay de regressão exige simultaneamente zero traduções de
`SPI1`, avanço gráfico e os rótulos completos no menu.

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

### Proteção geral por fase — 25/08/2026

Uma captura posterior mostrou que proteger apenas o patch estático ainda era
insuficiente. A fala `Dr. Geppetto: ... listen very\ncarefully...` existia já
traduzida em três cópias da RDRAM (`0x1705D1`, `0x2B5351` e `0x346CF1`) antes
de chegar a `func_8008EDA4`. Nessas cópias, `atenção` continha o byte `0x25`
do glifo `ã`; o formatador o lia como `%` e prendia a thread principal. A
imagem congelava, enquanto a thread de áudio continuava normalmente.

As APIs de recurso vivo agora distinguem explicitamente duas fases:

- `legendas_*_antes_formatador` não escreve traduções que gerem colisão com
  comandos de formato; registra `recurso_ptbr_adiado_formato` e conserva o
  inglês estrutural;
- os ganchos posteriores a `func_8008EDA4` aplicam a mesma entrada PT-BR já
  codificada, quando `%` voltou a ser somente um glifo.

A regra foi aplicada a todas as rotas precoces conhecidas
(`func_80096B38`, `func_800319B0` e `func_80096D40`), não somente à fala que
revelou o defeito. `func_80090E58` permanece como rota tardia. A cópia de slots
do menu em `func_800BD218` também permanece tardia porque é rasterizada sem
atravessar o formatador.

O teste unitário reproduz a colisão com `It's morning...` / `É de manhã...`:
exige origem inglesa intacta na fase precoce e o byte do glifo presente na
fase tardia. No replay completo de 175 segundos, o ponto antigo parava em
7.882 tarefas gráficas e `main_func=0x8008EDA4`; depois da correção chegou a
10.127 tarefas, com a thread principal aguardando normalmente em
`0x800CC98C`. O log confirmou tanto os adiamentos quanto traduções tardias da
mesma sequência de Geppetto.

### Representação intermediária e controles longos — 25/08/2026

Os F5 `001` e `004` mostraram uma limitação da estratégia de adiamento: falas
com variável no meio (`E0`) chegavam à rota tardia já com `Josette` expandida
e deixavam de ser iguais à chave canônica. Adiar a frase inteira portanto
evitava o congelamento, mas também a mantinha em inglês.

A solução passou a ser uma representação por fase. Antes de `func_8008EDA4`,
somente o `ã` vindo de UTF-8 é codificado como `0x7F`, byte ausente do banco
ASCII Ryu. Depois do formatador, `func_80090E58` o converte para `0x25`, quando
esse byte já significa o glifo e não o operador `%`. O texto, variáveis,
pausas e animação continuam percorrendo a rota original; não há sobreposição
no framebuffer nem busca por uma frase específica. Slots reutilizados são
reescritos na forma pré-formatador antes de nova publicação. A rota precoce
também reconhece a cadeia PT-BR completa já codificada: isso cobre blocos
in-place que atravessam o formatador novamente depois de `0x25` ter sido
restaurado, evitando reexpor `%` numa segunda passagem.

O F5 `002` revelou ainda o controle de quatro bytes `E2 06 80 10`, que muda a
cadência no meio de `Don't be so sel...fish.`. O parser antigo consumia apenas
`E2 06`, encontrava `0x80` como se fosse texto e rejeitava o recurso inteiro.
Runtime e extrator agora conhecem o tamanho variável do controle, preservam os
quatro bytes e usam a chave normalizada `Don't be so sel...fish.`. A entrada
foi promovida ao catálogo como `Não seja tão ego...ísta.`. O mesmo teste
unitário exige tradução, `ã` protegido e a sequência de animação intacta.

O F5 `003` era dado incorreto no catálogo: `Doctor...` ainda apontava para si
mesmo. A tradução ativa e o catálogo canônico passaram a usar `Doutor...`.

O F5 `005` não confirmou colisão de cache. No dump, a fila circular em
`0x1879D0` aponta para `0x804100C4`, que contém corretamente `Tenho certeza
que você será gentilmente vigiado...`. A tela ainda conservava a frase
anterior em `0x2B87E0`, enquanto o diálogo estava no estado intermediário
`0x2D`; a nova mensagem ainda não havia sido copiada para o buffer visual.
Esse caso deve ser revalidado depois das correções acima, sem um remendo de
ponteiro que contrariaria o estado observado.

### Revalidação sob F11 — apresentação atrasada, não tradução repetida

O F5 seguinte pareceu repetir `Ali, uma pessoa chamada "Player-san"...`, mas
os três níveis de estado divergiam de forma conclusiva:

- a janela RT64 ainda apresentava a frase antiga;
- o buffer rasterizado em `0x2B87E0` também continha a frase antiga;
- a fila circular já apontava para `0x80410028`, com `Eh? Mas por que!?`.

A captura ocorreu em `retrace=33727`, mas havia somente 7.295 tarefas gráficas:
o F11 adiantou a máquina da ROM muito além do último quadro apresentado. Isso
explica tanto a suposta repetição quanto o F5 anterior em estado `0x2D`; alterar
catálogo, cache ou ponteiro nesse ponto seria corrigir o subsistema errado.

O F11 passou a usar frame skip explícito: a ROM recebe normalmente todas as
conclusões SP/DP, porém sete de cada oito listas deixam de ser rasterizadas e
nenhum quadro é apresentado enquanto a tecla está pressionada. Diálogos,
pausas e transições continuam avançando na máquina original; ao soltar, a
próxima tarefa completa volta à janela. O estado físico da tecla também é
consultado, evitando turbo preso quando uma reconstrução da swapchain perde o
`WM_KEYUP` durante a transição 3D/2D.

### Repetição real — limite de 4 MB do SysMem_DmaCopy

A captura seguinte, já em cadência normal, separou o defeito restante. O
buffer vivo continuava em `Ali, uma pessoa...`, enquanto a fila já havia
consumido `Eh? Mas por que!?` e apontava para `Dr. Geppetto: Peço desculpas...`.
O slot fixo `0x80410028` continha corretamente a segunda tradução: catálogo,
arena e publicação do ponteiro estavam certos.

O consumidor em `func_80030AC8` copia cada ponteiro da fila para
`D_801705B0` chamando `SysMem_DmaCopy` (`func_800BD218`). O código original
recusa explicitamente origens a partir de `0x80400000`, pois o N64 desta ROM
anuncia somente 4 MB. Nossa arena PT-BR começa exatamente ali. A cópia falhava,
a fila avançava e o framebuffer textual conservava a mensagem anterior.

O wrapper já existente de `SysMem_DmaCopy` agora reconhece exclusivamente a
arena reservada pelo runtime (`0x80400000..0x806FFFFF`) e copia dela para a
RDRAM hospedada preservando a ordem lógica dos bytes MIPS. As demais origens
continuam no corpo original. É uma correção geral para toda tradução
realocada, não uma exceção para esta fala.

### Controles de variável após quebra de linha

Os F5 de 25/08 mostraram `Josette` inserida dentro de `preocupe` e `mim`. A
palavra não fazia parte da tradução: `E0 01` é o marcador do nome da
protagonista e, nos dois recursos ingleses, fica na coluna zero da segunda
linha. O mapeamento antigo preservava apenas a proporção do deslocamento bruto;
uma primeira linha PT-BR maior empurrava o controle para dentro da frase.

O realocador agora preserva número da linha e coluna para controles que vêm
depois de uma quebra. O teste cobre diretamente `Hoh hoh... don't worry,` e
exige `\n E0 01` na saída. Isso vale para qualquer variável estrutural em
mensagens multilinha, não somente para o nome Josette.

O terceiro F5 continha `Live your life to the fullest.` com três comandos
animados `E2 06`. A cadeia estava numa região dinâmica não coberta pelo
extrator histórico; foi catalogada manualmente pelo conteúdo normalizado como
`Viva sua vida plenamente.`, conservando todos os quatro bytes de cada comando.

Foi iniciada também uma auditoria conservadora de gênero. Correções inequívocas
dirigidas à protagonista foram sincronizadas nos dois catálogos (`guiá-la`,
`vigiada`, `Bem-vinda`, `sozinha`, `preparada`, `interessada`, `assustada`,
entre outras). Formas dependentes de contexto não foram trocadas em massa,
pois o mesmo adjetivo pode pertencer a personagens masculinos em outras cenas.

### Refluxo de palavras na caixa inferior

O F5 seguinte exibiu `vig`/`iada`, embora o buffer vivo contivesse a frase
inteira sem quebra. Logo, a divisão vinha do limite automático do formatador
inglês, que corta ao ultrapassar a largura sem retornar ao espaço anterior.

A primeira tentativa estava errada: removia quebras existentes e recalculava
toda a cadeia com um limite fixo. Em execução isso gerou uma linha adicional,
texto sobreposto e deslocou `E0 01` (Josette) para dentro de outra palavra. A
tentativa foi removida.

A captura dos quatro F5 seguintes mostrou que a largura não é expressa em
quantidade de glifos: a fonte Ryu é proporcional (`i/l` avançam 2 px,
`m/w` e a maioria das maiúsculas, 8 px) e a caixa quebra perto de 232 px. Nos
quatro casos, a tradução ultrapassava essa largura antes de alcançar o `\n` do
fragmento inglês; o motor criava uma quebra automática e o `\n` logo seguinte
executava outra rolagem sobre o mesmo framebuffer. Em dois recursos, somente
espaços de preenchimento antes do delimitador já causavam o estouro.

A regra vigente usa os avanços observados no compositor nativo. Sem quebra
explícita, troca o espaço anterior ao estouro por `\n`, sem mudar o tamanho.
Em mensagens compostas com `\n`, remove somente preenchimento imediatamente
anterior ao delimitador; se a linha ainda exceder 232 px, move a mesma quebra
para o espaço anterior e transforma a posição antiga em espaço. Portanto não
é criada uma segunda quebra e a sequência dos controles E0/E1/E2 é mantida.
O teste cobre preservação de uma quebra que já cabe, quebra automática e a
fala real `Certo, vou começar!...`, que agora divide antes de `Bird`.

A captura seguinte isolou um caso diferente: a instrução começava em `y=0`,
continuava em `y=14` e a palavra final `botão!` voltava para `y=0`, cobrindo a
primeira linha. O byte `0A` no fim desse recurso não era uma terceira linha
disponível; era o terminador visual da fala. O refluxo estava deslocando esse
último delimitador para dentro do texto e, assim, solicitava uma terceira
linha numa caixa nativa de duas linhas.

O compositor agora nunca reaproveita o `0A` terminal. Se a última linha ainda
exceder 232 px, a tradução composta precisa ser condensada dentro das duas
linhas em vez de inventar paginação. Os quatro fragmentos dessa instrução
também foram corrigidos e reduzidos (`Para carregar algo, coloque Bird sobre`
e `ele e segure o Botão Amarelo!`), removendo os resíduos ingleses `it` e
`press`. O teste unitário reproduz os controles E1 e exige exatamente duas
linhas mais o terminador final.

### `Day` e `Progress`: nova classificação

A hipótese antiga de palavras integralmente pré-rasterizadas não é suficiente.
`wonder-source/src/code/code_8F1A0.c` mostra que `func_80094230` compõe objetos
e glifos no atlas CI8 antes dos TEXRECTs. A tela de diário não mantém as duas
palavras como ASCII na RDRAM, mas isso não implica que sejam uma única imagem.

O F5 passou a exportar `glifos_f5_NNN.tsv`. Uma repetição com ring de 524.288
entradas conservou os 144.184 objetos desde o início da execução; mesmo assim,
as sequências de `Day` e `Progress` não aparecem. Isso encerra a hipótese de
que o ring curto apenas as tivesse sobrescrito.

O rastreio de `Spi_DecompressAsset` isolou o recurso adicional do patch Ryu em
ROM `0x0068E100` (0x6B68 bytes comprimidos, 0x282E0 descomprimidos). A ROM
japonesa não o carrega e usa, no mesmo espaço, o banco grande de fonte JP.
Portanto os dois rótulos pertencem ao recurso gráfico introduzido pela tradução
inglesa, e não à composição normal de `func_80094230`.

O F5 normal continua a exportar `glifos_f5_NNN.tsv`, com os objetos recentes,
coordenadas, avanço, chamador e estado. O ring só é ativado por `TESTAR.bat` e
não afeta builds de apresentação. A próxima captura da tela de diário permite
identificar a sequência e o recurso produtor; a correção deve substituir os
glifos nativos, nunca desenhar uma camada sobre o framebuffer.

## Fragmentos com controles e tabelas estáticas da loja — 27/08/2026

A fala de carregar objetos não era uma única cadeia. O buffer original ainda
continha os fragmentos `yellow ` e ` button!`, separados por `E1 07`/`E1 FF`.
Traduzir o primeiro fragmento como `Botão Amarelo` alterava a geometria e a
função semântica dos controles. O catálogo agora conserva a estrutura nativa:
`amarelo ` antes do fechamento do controle e ` botão!` depois dele. O teste
unitário exige exatamente essa composição e duas linhas mais o terminador.

A loja usa outra rota: pequenas cadeias NUL dentro de blocos carregados por
SPI/PI, que não passam necessariamente pelo carregador de diálogos. O runtime
agora percorre cada bloco recém-carregado, aceita somente cadeias ASCII
completas cuja chave exata existe no catálogo e escreve a tradução no próprio
slot, usando apenas seu texto e a folga NUL contígua. Isso mantém compositor,
ordenação, cursor e animação originais; não há sobreposição no framebuffer.
O teste sintético cobre `Buy -> Comprar` e `Back -> Voltar` em dois slots.

Foram catalogados também `Money`, `Shop`, `Sell`, `Register`, `Previous`,
`Next`, a ajuda de navegação e as descrições de `Health Oil`/`Mind Oil`.
Build RT64 e `test_legendas_recursos.exe` passaram; a cobertura em todas as
telas da loja ainda depende da validação interativa.

## Sonda permanente de tradução ausente — 28/08/2026

O perfil padrão agora define `WPJ2_LEGENDAS_AUSENTES_LOG` e grava
`traducao_ausentes.tsv` na pasta temporária do teste. A observação ocorre na
entrada real de `func_80090E58`: portanto registra somente cadeias entregues ao
formatador, não qualquer ASCII casual encontrado na ROM ou na RDRAM.

A sonda remove controles E0/E1/E2 para a comparação, reconhece tanto a chave
inglesa quanto a forma PT-BR codificada, elimina os sufixos produzidos quando o
formatador avança um byte por glifo e deduplica a frase inteira. Como nomes e
variáveis podem impedir uma igualdade exata mesmo depois da tradução, uma
classificação conservadora por palavras funcionais separa inglês provável de
português já aplicado. A calibração inicialmente expôs esse falso positivo e
foi corrigida; a execução final gerou apenas o cabeçalho, isto é, nenhuma frase
inglesa sem catálogo nas cenas percorridas.

Esta sonda não enxerga palavras já rasterizadas em recursos gráficos, como a
classe histórica de `Day`/`Progress`; essas continuam pertencendo ao inventário
de assets e não ao formatador textual.

## Correspondência entre texto identificado e texto impresso — 28/08/2026

`traducao_validacao.tsv` captura a cadeia antes da substituição e o ponteiro
final entregue ao formatador. Para chaves exatas, registra também o PT-BR
esperado e classifica `exato`/`DIVERGENTE`; compostos e texto já traduzido ficam
separados. Isso cobre a divergência histórica entre “texto localizado na
memória” e “texto efetivamente impresso”.

Na primeira rodada completa: 15 exatos, 37 já PT-BR, 39 compostos observados e
uma divergência exata. Ela foi `I'll walk to the place Bird is pointing to!`:
o consumido tinha uma quebra automática antes de `apontar!`. É diferença de
layout posterior, não seleção da tradução errada. Entradas `sem_catalogo`
incluíram números e compostos PT-BR transitórios e não devem ser tratadas como
inglês ausente; essa função continua pertencendo a `traducao_ausentes.tsv`.
