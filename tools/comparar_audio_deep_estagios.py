"""Compara estados pós-RSP do Project64 contra o HLE local, por comando."""
from __future__ import annotations

import math
import re
import sys
from pathlib import Path


POST = re.compile(r"^(?P<kind>[a-z_]+)_cmd\d+.*_(?P<address>[0-9A-Fa-f]{8})\.bin$")


def samples(blob: bytes) -> list[int]:
    return [int.from_bytes(blob[i:i + 2], "big", signed=True) for i in range(0, len(blob) - 1, 2)]


def main() -> int:
    task_dir = Path(sys.argv[1])
    hle_rdram = Path(sys.argv[2]).read_bytes()
    rows: list[tuple[str, int, int, float, int]] = []
    for file in sorted(task_dir.glob("*_after_cmd*.bin")):
        match = POST.match(file.name)
        if not match:
            continue
        address = int(match["address"], 16)
        expected = file.read_bytes()
        actual = hle_rdram[address:address + len(expected)]
        changed = sum(a != b for a, b in zip(actual, expected))
        a, b = samples(actual), samples(expected)
        rms = math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a)) if a else 0.0
        peak = max((abs(x - y) for x, y in zip(a, b)), default=0)
        rows.append((match["kind"], address, changed, rms, peak))
    rows.sort(key=lambda row: (row[2] == 0, -row[3], row[0]))
    print("kind,address,changed_bytes,rms_delta,peak_delta")
    for kind, address, changed, rms, peak in rows:
        print(f"{kind},0x{address:08X},{changed},{rms:.2f},{peak}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
