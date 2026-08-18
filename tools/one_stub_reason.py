"""Roda o N64Recomp com uma unica funcao fora da lista de stubs e mostra tudo
o que ele imprime. Usado para descobrir por que uma funcao precisou virar stub
quando a mensagem nao e um 'Unhandled instruction'."""
import subprocess
import sys
from pathlib import Path

proj = Path(__file__).resolve().parent.parent
text = (proj / "wpj2.toml").read_text(encoding="utf-8")
tmp = proj / "wpj2_tmp.toml"

for target in sys.argv[1:]:
    tmp.write_text(text.replace('    "%s",\n' % target, ""), encoding="utf-8", newline="\n")
    r = subprocess.run([str(proj / "tools/N64Recomp-build-official/N64Recomp.exe"), str(tmp)],
                       cwd=proj, text=True, errors="replace", capture_output=True)
    out = (r.stdout + r.stderr).strip().splitlines()
    detail = [l for l in out if "Unhandled" in l or "Unsupported" in l or "Error" in l]
    print("=== %s (rc=%d)" % (target, r.returncode))
    for l in detail:
        print("   ", l.strip())
tmp.unlink(missing_ok=True)
