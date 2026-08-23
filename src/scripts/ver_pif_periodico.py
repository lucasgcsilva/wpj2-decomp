# Extrai a serie temporal do relatorio periodico do PIF de um execucao.log.
#
# Existe porque o shell do host desmonta aspas em linha de comando; um arquivo
# evita ficar reescrevendo o mesmo one-liner a cada consulta.
import io, sys

caminho = sys.argv[1]
with io.open(caminho, encoding="utf-8", errors="replace") as f:
    for linha in f:
        if "[pif-per]" in linha:
            print(linha.rstrip())
