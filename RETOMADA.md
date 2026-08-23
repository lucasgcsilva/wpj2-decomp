# Retomada — estado em 23/08/2026

Documento de handoff. A sessão de chat que produziu isto foi descartada de
propósito; **tudo o que importa está aqui e nos arquivos citados.** Comece por
este arquivo, não por histórico de conversa.

Registros por assunto:

| Assunto | Arquivo |
|---|---|
| Áudio | `ANALISE_AUDIO.md` |
| Entrada de controle | `ENTRADA_RETOMADA.md` |
| Layout do projeto e regra do `temp/` | `ESTRUTURA.md` |
| Gráficos / clipping | `analise/projeto/graficos_clipping_corredor.md` |

Ponto de entrada único para testar: `TESTAR.bat <perfil>`.
Perfis de entrada: `input`, `input_janela`, `input_tardio`, `menu`.

---

## O que está resolvido

- **Áudio** — audível e estável. Restam "pipocos" esporádicos.
- **Entrada** — botões e direcionais funcionando. A causa raiz da paralisia
  histórica está em `ENTRADA_RETOMADA.md` (o PIF só executava a fita de
  comandos na escrita; o jogo lia 30×/s uma resposta de dois segundos atrás).
- **Controller Pack** — implementado em `runtime/mempak.c`. Saves em `sav/`,
  fora do versionamento.
- Transições visuais parcialmente ajustadas.

## Corrigido nesta sessão

**Escrita dupla no Controller Pack.** A correção do PIF de 23/08 passou a
reexecutar a fita também na leitura de volta (`dir=0`). Como `pif_process()`
trata os comandos de MemPak `0x02`/`0x03`, **toda transação de save passou a
acontecer duas vezes**. O conteúdo continuava correto — mesmos 32 bytes, mesmo
endereço — então não aparecia como save corrompido, e sim como lentidão. É a
explicação mais provável para "o Controller Pack demora bastante".

`pif_process()` agora recebe `refresh`: em `dir=0` reamostra só o estado dos
controles e ignora armazenamento. O modelo por trás disso também foi
corrigido no comentário — no hardware os botões chegam novos porque o PIF
**varre os controles continuamente** em segundo plano, e não porque a fita
seja reexecutada; essa varredura nunca toca o Controller Pack.

*Falta validar com o jogo rodando: confirmar que salvar ficou mais rápido.*

---

# Revisão dos projetos de referência — achados

Fontes: `tools/libreultra`, `tools/sdk-tools`, `tools/wonder-source`.

## 1. Blend e coverage não existem no rasterizador — prioridade máxima

Uma única lacuna explica **dois** problemas relatados, e é o melhor retorno
por esforço do projeto hoje.

Evidência, em `runtime/rsp.c:109`:

```c
/* SETOTHERMODE_L e o estado de blend/coverage do RDP. ...
   registramos os poucos modos emitidos antes de implementar blend. */
```

`SETOTHERMODE_L` é apenas registrado, nunca interpretado. Uma busca por
`coverage|cvg` em todo o `rsp.c` devolve só esse comentário.

**Consequência A — o quadrado vermelho piscante do menu.** É um retângulo
translúcido sobre a opção do cursor, ou seja, alpha blending. Sem blend ele
não tem como aparecer. Não é bug de lógica de menu: é a feature que falta.

**Consequência B — o serrilhado, inclusive na abertura.** O anti-alias do N64
é *baseado em coverage*, calculado pelo RDP durante a rasterização e guardado
junto do pixel. Se o rasterizador não produz coverage, o filtro do VI não tem
o que consumir — e **nenhum pós-processamento recupera informação de borda que
nunca foi calculada**. Por isso o `video_filtrar_vi_2d` já ativo na abertura
não resolve: ele foi posto no lugar errado da cadeia.

Confirmação de que o jogo espera AA ligado: o `VI_CONTROL` observado em
execução (`0xA4400000 = 77826`) tem os bits 8–9 (`aa_mode`) em zero, que é o
modo de AA mais completo.

**Encaminhamento sugerido, nesta ordem:**

1. Interpretar `SETOTHERMODE_L` e implementar o blender de dois ciclos. Além
   de trazer o realce do menu, é pré-requisito para transparência em geral —
   provavelmente também para parte das "transições estranhas".
2. Para o serrilhado, **não** perseguir coverage fiel de imediato. Rasterizar
   em 2× ou 4× e reduzir por média na apresentação resolve a borda em todo
   lugar, não depende de coverage, e é muito menor em esforço. Coverage fiel
   fica para a fase 4.

## 2. Features de VI: o jogo declara o que quer, e nós adivinhamos

`tools/wonder-source/src/main.c:65` mostra a intenção sem ambiguidade:

```c
D_8018168C = 74;                            /* 0x4A */
osViSetSpecialFeatures(OS_VI_DITHER_FILTER_ON | OS_VI_GAMMA_DITHER_OFF | OS_VI_GAMMA_OFF);
```

`0x4A` = `DITHER_FILTER_ON | GAMMA_DITHER_OFF | GAMMA_OFF`. Confere com a
chamada.

- **Gamma: já está correto.** `video_gamma_ativo()` condiciona ao bit 3 do
  `vi_status`; o jogo desliga o gamma, o bit fica zero, a curva não é
  aplicada. Verificado, sem ação necessária.
- **Filtro de dither: acionado pelo critério errado.** Hoje
  `video_filtrar_vi_2d` roda sob `video_vi_filter_2d_ativo() && estado_jogo == 8`.
  O gatilho correto é o bit 11 (`dither_filter_enable`) do `vi_status`, que é
  o que o jogo efetivamente pede. Trocar o `estado_jogo == 8` — que é uma
  heurística — pelo bit remove um caso especial e aplica o filtro onde o jogo
  quer. Isso ataca granulação de dither, **não** o serrilhado (ver item 1).

## 3. Legendas faltando: medir antes de mexer

Sintoma: há texto no TSV que não aparece traduzido em jogo.

Existem dois caminhos de texto já instrumentados — o carregador de recurso
(`func_80096B38`) e o formatador de texto estático (`func_80090E58`). A
hipótese mais provável é que um dos dois não passe pela substituição, ou que a
chave usada na busca não seja a mesma gravada no TSV.

Método concreto, sem chutar: `TESTAR.bat` já grava `legendas_rota.tsv` com as
chaves pedidas em execução. Comparar esse arquivo com `textos/traducao_ptbr.tsv`
separa as três causas possíveis de uma vez — chave pedida e ausente do TSV,
chave presente nos dois mas não aplicada, ou chave nunca pedida (caminho de
texto não interceptado).

## 4. `tools/sdk-tools` — ainda não explorado

Contém `adpcm` e `tabledesign`, as ferramentas originais de codificação de
áudio. Com o áudio já aceitável, o valor caiu de prioridade, mas seguem sendo
a referência exata para os coeficientes de predição do ADPCM — úteis se os
"pipocos" residuais forem investigados.

## 5. Texturas mal mapeadas

Sem achado novo nesta revisão. Depende de comparar a emissão de
`SETTILE`/`SETTILESIZE` e o cálculo de coordenadas contra a referência. Fica
depois do item 1, porque implementar blend altera o caminho de textura e
tornaria qualquer medição feita agora obsoleta.
