"""A fronteira da execucao: o que o codigo alcancado chama e nunca foi alcancado.

Cruza `executadas.txt` (gravado pelo runtime ao final de cada corrida) com
`analise/projeto/codigo/callgraph.txt`. O resultado e a lista de funcoes que estao a exatamente
uma chamada de distancia do que ja roda — ordenada por quantos chamadores
executados apontam para elas.

E o inverso util do traco: em vez de "por onde passou", responde "o que estava
prestes a acontecer e nao aconteceu".
"""
from __future__ import annotations

import sys
from pathlib import Path

proj = Path(__file__).resolve().parent.parent

executed_path = proj / "temp" / "projeto" / "laboratorio" / "executadas.txt"
graph_path = proj / "temp" / "projeto" / "laboratorio" / "analise_estatica" / "callgraph.txt"
if not graph_path.exists():
    graph_path = proj / "analise" / "projeto" / "codigo" / "callgraph.txt"

executed = {}
for line in executed_path.read_text(encoding="utf-8").splitlines():
    if line.startswith("#") or not line.strip():
        continue
    vram, calls = line.split()
    executed[int(vram, 16)] = int(calls)

graph = {}
for line in graph_path.read_text(encoding="utf-8").splitlines():
    if line.startswith("#") or "->" not in line:
        continue
    src, rest = line.split("->", 1)
    graph[int(src.strip()[5:], 16)] = [int(d[5:], 16) for d in rest.split()]

# As substituicoes nativas nao tem hook de traco - o corpo recompilado foi
# renomeado - entao nunca aparecem como executadas. Sem excluir, elas poluem a
# fronteira com funcoes que na verdade rodam o tempo todo.
native = set()
ov = proj / "native_overrides.txt"
if ov.exists():
    for line in ov.read_text(encoding="utf-8").splitlines():
        line = line.split("#")[0].strip()
        if line.startswith("func_"):
            native.add(int(line[5:], 16))

# Quem chama cada funcao nao executada, contando so chamadores que executaram.
frontier = {}
for src, dests in graph.items():
    if src not in executed:
        continue
    for d in dests:
        if d not in executed and d not in native:
            frontier.setdefault(d, []).append(src)

rows = sorted(frontier.items(), key=lambda kv: (-len(kv[1]), kv[0]))
limit = int(sys.argv[1]) if len(sys.argv) > 1 else 25

print("executadas: %d   nao executadas mas chamaveis: %d\n"
      % (len(executed), len(frontier)))
print("as %d mais apontadas:" % min(limit, len(rows)))
for dest, callers in rows[:limit]:
    quem = " ".join("func_%08X" % c for c in sorted(callers)[:4])
    mais = "" if len(callers) <= 4 else " (+%d)" % (len(callers) - 4)
    print("  func_%08X  <- %d chamador(es) executado(s): %s%s"
          % (dest, len(callers), quem, mais))

# O outro lado da mesma pergunta: quem ja roda mas quase nao se ramifica. Uma
# funcao executada com muitos destinos nunca alcancados costuma ser um
# despachante parado num unico caso - e e ai que o jogo deixa de avancar.
print("\nfuncoes executadas que mais deixam destinos sem alcancar:")
stuck = []
for src, dests in graph.items():
    if src not in executed:
        continue
    faltando = [d for d in dests if d not in executed and d not in native]
    if faltando:
        stuck.append((len(faltando), len(dests), src, faltando))
stuck.sort(reverse=True)
for nf, nt, src, faltando in stuck[:12]:
    print("  func_%08X  %d de %d destinos nunca alcancados (%d chamadas)"
          % (src, nf, nt, executed[src]))
    print("      %s" % " ".join("func_%08X" % d for d in sorted(faltando)[:8]))
