# Assets locais

Esta pasta recebe recursos extraídos da ROM fornecida pelo próprio usuário.
Os binários em `generated/` são locais e ignorados pelo Git; não devem ser
distribuídos com o projeto.

`manifest.json` é o mapa versionável: registra identificador, faixa da ROM,
formato nativo, símbolo/endereço conhecido e consumidores no jogo. O manifesto
não contém pixels, áudio ou outro conteúdo protegido.

Para extrair todos os itens já mapeados:

```powershell
python src/scripts/extrair_assets.py `
  "E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64"
```

A saída é:

```text
assets/generated/
├── index.json       # hashes e veredito da extração local
└── raw/             # bytes nativos, sem conversão destrutiva
```

Regras:

- preservar sempre o formato N64 original;
- PNG, WAV e glTF são representações editáveis, nunca substitutos do bruto;
- todo recurso novo precisa de offset, tamanho e consumidor conhecido ou de
  uma marca explícita de que ainda está sob investigação;
- melhorias de alta resolução devem usar o mesmo `id` do recurso original,
  com uma variante separada, para o runtime poder alternar entre ambos.

