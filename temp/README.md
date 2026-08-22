# Área temporária

Toda nova execução deve escrever em uma destas pastas:

```text
temp/
├── projeto/   # TESTAR, JOGAR, protótipos e sondas locais
└── oraculo/   # Project64 e demais referências externas
```

Ao fim de cada ciclo, o conteúdo é analisado, a conclusão relevante é
incorporada em `../analise` e os resultados processados são removidos. Este
README é o único arquivo permanente da pasta.
