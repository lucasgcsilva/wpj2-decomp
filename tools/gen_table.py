"""Gera a tabela vram -> ponteiro de funcao que o runtime usa em LOOKUP_FUNC.

O recompilador nomeia cada funcao pelo seu endereco VRAM, entao a tabela sai
direto do cabecalho gerado. Nomes que nao sigam `func_XXXXXXXX` sao reportados
em vez de silenciosamente ignorados: cada um deles e uma chamada indireta que
deixaria de resolver em tempo de execucao.
"""
import re
import sys

DECL = re.compile(r"^void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\);")
GENERATED = re.compile(r"^func_([0-9A-F]{8})$")

ENTRYPOINT_VRAM = 0x80000400


def main():
    header, out = sys.argv[1], sys.argv[2]

    entries = []
    skipped = []
    with open(header, encoding="utf-8") as f:
        for line in f:
            m = DECL.match(line.strip())
            if not m:
                continue
            name = m.group(1)
            if name == "recomp_entrypoint":
                # O entrypoint tambem precisa de entrada: e o alvo de qualquer
                # salto indireto de volta para 0x80000400.
                entries.append((ENTRYPOINT_VRAM, name))
                continue
            g = GENERATED.match(name)
            if g:
                entries.append((int(g.group(1), 16), name))
            else:
                skipped.append(name)
    entries.sort()

    if skipped:
        print("AVISO: sem endereco para %d simbolo(s): %s"
              % (len(skipped), ", ".join(skipped[:5])))

    with open(out, "w", encoding="utf-8") as f:
        f.write("// Gerado por tools/gen_table.py - nao edite.\n")
        f.write('#include "funcs.h"\n')
        f.write('#include "runtime.h"\n\n')
        f.write("const func_entry_t g_func_table[] = {\n")
        for vram, name in entries:
            f.write("    { 0x%08Xu, %s },\n" % (vram, name))
        f.write("};\n\n")
        f.write("const size_t g_func_table_size = %d;\n" % len(entries))

    print("escrevi %s com %d entradas" % (out, len(entries)))
    if entries:
        print("faixa 0x%08X .. 0x%08X" % (entries[0][0], entries[-1][0]))


if __name__ == "__main__":
    main()
