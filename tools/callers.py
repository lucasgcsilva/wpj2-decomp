"""Quem chama quem. Le analise/projeto/codigo/callgraph.txt e responde nos dois sentidos.

Uso: callers.py func_800D5060 [func_...]
Mostra os chamadores diretos e, logo abaixo, o que a propria funcao chama.
"""
import sys
from pathlib import Path

graph = {}
path = Path(__file__).resolve().parent.parent / "analise" / "projeto" / "codigo" / "callgraph.txt"
for line in path.read_text(encoding="utf-8").splitlines():
    if "->" not in line:
        continue
    src, rest = line.split("->", 1)
    graph[src.strip()] = rest.split()

for target in sys.argv[1:]:
    callers = sorted(s for s, d in graph.items() if target in d)
    print("=== %s" % target)
    print("  chamado por (%d): %s" % (len(callers), " ".join(callers) or "ninguem"))
    print("  chama       (%d): %s" % (len(graph.get(target, [])),
                                      " ".join(graph.get(target, [])) or "ninguem"))
