# Conta as rotas de legenda de um legendas_rota.tsv.
#
# Separa os tres desfechos que interessam: traduzida no espaco original,
# traduzida so por causa do enchimento (ganho da medicao de folga) e recusada
# por nao caber de jeito nenhum.
import io, sys, collections

caminho = sys.argv[1]
contagem = collections.Counter()
with io.open(caminho, encoding="utf-8", errors="replace") as f:
    for linha in f:
        rota = linha.split("\t", 1)[0].strip()
        if rota:
            contagem[rota] += 1

for rota, n in contagem.most_common():
    print(f"{n:6d}  {rota}")
