"""Varredura completa do segmento boot, distribuida por todos os nucleos.

Produz de uma so passada tudo o que a proxima etapa precisa:

  callgraph.txt       quem chama quem (via `jal`)
  hw_signatures.txt   os registradores de MMIO exatos que cada funcao dirige
  cop0_usage.txt      quais funcoes leem/escrevem COP0 ou usam eret/syscall

O decodificador aqui e proposital e deliberadamente pequeno: reconhece so as
formas que importam para estas perguntas (lui/addiu/lw/sw, jal, mfc0/mtc0,
eret, syscall, cache). Um desassemblador completo nao acrescentaria nada e
custaria a dependencia do rabbitizer em cada processo filho.
"""
from __future__ import annotations

import os
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROM_START = 0x001000
VRAM_START = 0x80000400

COP0_NAMES = {
    0: "Index", 1: "Random", 2: "EntryLo0", 3: "EntryLo1", 4: "Context",
    5: "PageMask", 6: "Wired", 8: "BadVAddr", 9: "Count", 10: "EntryHi",
    11: "Compare", 12: "Status", 13: "Cause", 14: "EPC", 15: "PRId",
    16: "Config", 17: "LLAddr", 18: "WatchLo", 19: "WatchHi", 20: "XContext",
    26: "PErr", 27: "CacheErr", 28: "TagLo", 29: "TagHi", 30: "ErrorEPC",
}

# Nome de cada registrador mapeado em memoria, por bloco e deslocamento.
MMIO = {
    0xA3F00000: ["RDRAM_CONFIG", "RDRAM_DEVICE_ID", "RDRAM_DELAY", "RDRAM_MODE",
                 "RDRAM_REF_INTERVAL", "RDRAM_REF_ROW", "RDRAM_RAS_INTERVAL",
                 "RDRAM_MIN_INTERVAL", "RDRAM_ADDR_SELECT", "RDRAM_DEVICE_MANUF"],
    0xA4040000: ["SP_MEM_ADDR", "SP_DRAM_ADDR", "SP_RD_LEN", "SP_WR_LEN",
                 "SP_STATUS", "SP_DMA_FULL", "SP_DMA_BUSY", "SP_SEMAPHORE"],
    0xA4080000: ["SP_PC", "SP_IBIST"],
    0xA4100000: ["DPC_START", "DPC_END", "DPC_CURRENT", "DPC_STATUS",
                 "DPC_CLOCK", "DPC_BUFBUSY", "DPC_PIPEBUSY", "DPC_TMEM"],
    0xA4200000: ["DPS_TBIST", "DPS_TEST_MODE", "DPS_BUFTEST_ADDR", "DPS_BUFTEST_DATA"],
    0xA4300000: ["MI_MODE", "MI_VERSION", "MI_INTR", "MI_INTR_MASK"],
    0xA4400000: ["VI_STATUS", "VI_ORIGIN", "VI_WIDTH", "VI_INTR", "VI_CURRENT",
                 "VI_BURST", "VI_V_SYNC", "VI_H_SYNC", "VI_LEAP", "VI_H_START",
                 "VI_V_START", "VI_V_BURST", "VI_X_SCALE", "VI_Y_SCALE"],
    0xA4500000: ["AI_DRAM_ADDR", "AI_LEN", "AI_CONTROL", "AI_STATUS",
                 "AI_DACRATE", "AI_BITRATE"],
    0xA4600000: ["PI_DRAM_ADDR", "PI_CART_ADDR", "PI_RD_LEN", "PI_WR_LEN",
                 "PI_STATUS", "PI_DOM1_LAT", "PI_DOM1_PWD", "PI_DOM1_PGS",
                 "PI_DOM1_RLS", "PI_DOM2_LAT", "PI_DOM2_PWD", "PI_DOM2_PGS",
                 "PI_DOM2_RLS"],
    0xA4700000: ["RI_MODE", "RI_CONFIG", "RI_CURRENT_LOAD", "RI_SELECT",
                 "RI_REFRESH", "RI_LATENCY", "RI_RERROR", "RI_WERROR"],
    0xA4800000: ["SI_DRAM_ADDR", "SI_PIF_ADDR_RD64B", "SI_reserved1",
                 "SI_reserved2", "SI_PIF_ADDR_WR64B", "SI_reserved3", "SI_STATUS"],
}


def reg_name(addr: int) -> str | None:
    # Os blocos sao esparsos e alguns ficam a menos de 0x100000 um do outro, entao
    # vale o bloco mais proximo abaixo do endereco - nao o primeiro que couber.
    if not 0xA3F00000 <= addr < 0xA4900000:
        return None
    base = max((b for b in MMIO if b <= addr), default=None)
    if base is None:
        return None
    index = (addr - base) // 4
    names = MMIO[base]
    if 0 <= index < len(names):
        return names[index]
    if 0xA4000000 <= addr < 0xA4040000:
        return "SP_DMEM/IMEM"
    return "0x%08X" % addr


LOADS = {0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37}   # lb lh lw lbu lhu lwu ld
STORES = {0x28, 0x29, 0x2B, 0x3F}                    # sb sh sw sd


def s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def scan(job):
    """Analisa uma funcao. Recebe e devolve so tipos simples, para atravessar a
    fronteira de processo sem custo de serializacao."""
    vram, size, blob = job
    calls, regs, cop0 = set(), {}, set()
    special = set()
    known = {}                       # registrador -> valor constante conhecido

    for i in range(0, min(size, len(blob)), 4):
        w = struct.unpack_from(">I", blob, i)[0]
        op = w >> 26
        rs, rt = (w >> 21) & 0x1F, (w >> 16) & 0x1F
        imm = w & 0xFFFF

        if op == 0x0F:                               # lui
            known[rt] = imm << 16
        elif op == 0x09 and rs in known:             # addiu a partir de base conhecida
            known[rt] = (known[rs] + s16(imm)) & 0xFFFFFFFF
        elif op == 0x03:                             # jal
            calls.add(0x80000000 | ((w & 0x03FFFFFF) << 2))
        elif op in LOADS or op in STORES:
            if rs in known:
                name = reg_name((known[rs] + s16(imm)) & 0xFFFFFFFF)
                if name:
                    regs.setdefault(name, set()).add("r" if op in LOADS else "w")
        elif op == 0x10:                             # cop0
            # Em mfc0/mtc0 o registrador do COP0 e `rd` (bits 15-11), nao `rt`.
            rd = (w >> 11) & 0x1F
            if rs == 0x00:
                cop0.add("mfc0 " + COP0_NAMES.get(rd, str(rd)))
            elif rs == 0x04:
                cop0.add("mtc0 " + COP0_NAMES.get(rd, str(rd)))
            elif w & 0x3F == 0x18:
                special.add("eret")
        elif op == 0x2F:
            special.add("cache")
        elif op == 0x00 and (w & 0x3F) == 0x0C:
            special.add("syscall")

        if op in (0x02, 0x03) or (op == 0x00 and (w & 0x3F) in (0x08, 0x09)):
            known.clear()            # depois de um salto nada e garantido

    return (vram, sorted(calls),
            {k: "".join(sorted(v)) for k, v in regs.items()},
            sorted(cop0), sorted(special))


def load_functions(syms: Path) -> list[tuple[int, int]]:
    funcs, vram, named = [], None, False
    for line in syms.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith('name = "func_'):
            named = True
        elif line.startswith("name ="):
            named = False
        elif line.startswith("vram = 0x") and named:
            vram = int(line.split("0x")[1], 16)
        elif line.startswith("size = 0x") and vram is not None:
            funcs.append((vram, int(line.split("0x")[1], 16)))
            vram = None
    return funcs


def main() -> int:
    rom = Path(sys.argv[1]).read_bytes()
    funcs = load_functions(Path(sys.argv[2]))
    outdir = Path(sys.argv[3])
    outdir.mkdir(parents=True, exist_ok=True)

    jobs = [(v, s, rom[ROM_START + v - VRAM_START: ROM_START + v - VRAM_START + s])
            for v, s in funcs]

    workers = os.cpu_count() or 4
    print("analisando %d funcoes em %d processos" % (len(jobs), workers))
    with ProcessPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(scan, jobs, chunksize=64))

    callers: dict[int, list[int]] = {}
    with (outdir / "callgraph.txt").open("w", encoding="utf-8", newline="\n") as f:
        f.write("# funcao -> funcoes que ela chama por jal\n")
        for vram, calls, _, _, _ in results:
            for c in calls:
                callers.setdefault(c, []).append(vram)
            if calls:
                f.write("func_%08X -> %s\n" % (
                    vram, " ".join("func_%08X" % c for c in calls)))

    hw = [(v, r) for v, _, r, _, _ in results if r]
    with (outdir / "hw_signatures.txt").open("w", encoding="utf-8", newline="\n") as f:
        f.write("# funcao        chamadores  registradores dirigidos (r=le, w=escreve)\n")
        for vram, regs in hw:
            f.write("func_%08X  %-3d  %s\n" % (
                vram, len(callers.get(vram, [])),
                "  ".join("%s:%s" % (k, v) for k, v in sorted(regs.items()))))

    cop = [(v, c, s) for v, _, _, c, s in results if c or s]
    with (outdir / "cop0_usage.txt").open("w", encoding="utf-8", newline="\n") as f:
        f.write("# funcao        chamadores  COP0 e instrucoes privilegiadas\n")
        for vram, c, s in cop:
            f.write("func_%08X  %-3d  %s\n" % (
                vram, len(callers.get(vram, [])), " ".join(c + s)))

    orphans = [v for v, _ in funcs if v not in callers and v != VRAM_START]
    print("funcoes analisadas      : %d" % len(results))
    print("com acesso a MMIO       : %d" % len(hw))
    print("com COP0/eret/cache     : %d" % len(cop))
    print("sem nenhum chamador jal : %d" % len(orphans))
    print("saidas em %s" % outdir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
