# Sonda decisiva da cadeia de cena

O script `wpj2_exec_chain.js` registra apenas quatro eventos: escritas nos
três campos de guarda e entrada em `0x80001F54` ou `0x80021ED0`. Ele não
altera a ROM nem a RAM.

## Uma única execução

1. Abra `tools\Project64-source\Bin\Win32\Release\Project64.exe`.
2. Em **Options > Configuration > Advanced**, marque **Enable debugger** e
   **Always use interpreter core**. Reinicie o Project64 se ele pedir.
3. Abra **Debugger > Scripts**, selecione `wpj2_exec_chain.js` e execute-o.
4. Inicie a ROM, não aperte botões e deixe chegar até a primeira tela/logo por
   cerca de 20 segundos. Depois feche o emulador.

O resultado estará em `temp\oraculo\pj64-rdram\wpj2_exec_chain.txt`.

## Comparação do retorno de 0x80002F20

Depois da primeira sonda, execute `wpj2_2f20_compare.js` na mesma janela de
Scripts, ainda com core Interpreter. Rode a ROM por 20 segundos, sem entrada.
O resultado será `temp\oraculo\pj64-rdram\wpj2_2f20_compare.txt`. Ele grava o valor
de `v0` logo após a chamada de `0x80002F20` em `0x80000EBC`, o eventual laço em
`0x80000EC8` e a entrada posterior em `0x80001F54`.

Interpretação objetiva:

- Há `ENTER 80001F54` e `ENTER 80021ED0`: a cadeia de agenda da ROM funciona;
  a falha fica depois dela, no runtime recompilado.
- Há `ENTER 80001F54`, mas não `ENTER 80021ED0`: os campos mostram qual das
  três condições impediu a ativação.
- Não há `ENTER 80001F54`, mas há `WRITE`: o PC registrado em `WRITE` é o
  próximo trecho exato a portar/inspecionar.
- Não há nenhum dos dois: essa hipótese é descartada e é recomendável pausar
  o trabalho, pois não há frente comprovada de baixo custo.
