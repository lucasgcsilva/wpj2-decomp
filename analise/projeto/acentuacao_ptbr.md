# Acentuação PT-BR no motor de texto

## Resultado validado visualmente em 24/08/2026

O código reservado chegava corretamente ao formatador (`á` como `0x23`), mas
o jogo continuava exibindo `#`. A causa não estava no catálogo nem no
`fold_utf8`.

A correção anterior do boot foi um falso positivo: os bytes internos pareciam
certos, mas a captura final ainda mostrava `Qual di#rio voc+ deseja usar?`.
Essa evidência visual invalida a conclusão antiga e fica registrada para não
se repetir a validação por memória intermediária.

O formatador modificado pelo patch Ryu converte os bytes ASCII em EUC-JP,
Shift-JIS e, por fim, em índices de objeto truncados (`0x1xx`/`0x2xx`). O ramo
ativo de `func_80094230` não usa a célula ASCII de 12 bytes investigada antes:
ele lê doze linhas de células de 24 bytes em
`D_8015F880 + indice*24 + linha*2 - 0x1200`.

A correção recompõe o objeto vivo imediatamente antes de `func_80094230`
consumi-lo. Por exemplo, o objeto `0x193` que desenhava `#` agora é montado a
partir do `a` (`0x23C`) e recebe o agudo; o objeto `0x17B` que desenhava `+` é
montado a partir do `e` (`0x240`) e recebe o circunflexo. Os compositores de
ROM e das tabelas dormentes ficaram desligados por padrão.

## Ajustes dos glifos

Os símbolos japoneses usados inicialmente como marcas tinham métricas
horizontais incompatíveis com a fonte inglesa. O agudo saía à direita da
letra, chegando a aparecer sobre o próximo caractere; o slot `0x7E` também não
continha um til reaproveitável. As marcas passaram a usar bitmaps de duas
linhas alinhados às minúsculas de largura 6.

O `í` recebe tratamento próprio: o ponto original na linha zero é substituído
por uma diagonal curta, preservando o corpo que começa na linha dois.

Foram instalados e contabilizados os 14 destinos reservados:

- `á`, `â`, `ã`, `à`, `é`, `ê`, `í`, `ó`, `ô`, `õ`, `ú`;
- `ç`;
- `º` e `ª`.

## Evidência

- build completo de `wpj2_probe.exe` concluído sem erro;
- log do caminho vivo: objetos `0x0193 <- 0x023C` e
  `0x017B <- 0x0240` recompostos durante o desenho;
- captura **do framebuffer final** do menu exibiu `Qual diário você...`, com
  `á` e `ê` reais do catálogo no lugar dos antigos `#` e `+`;
- `build/test_legendas_recursos.exe`: `legendas_recursos: OK`;
- `git diff --check`: sem erro.

O teste interativo principal continua sendo `TESTAR.bat`. O próximo controle
humano útil é percorrer falas contendo grave, til, `ç`, `í` e os dois ordinais
para avaliar apenas a estética pixel a pixel; agudo e circunflexo já foram
vistos no menu e a rota de dados está corrigida.
