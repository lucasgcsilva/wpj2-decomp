"""Traduz e revisa os recursos textuais completos de WPJ2 via LM Studio.

O arquivo de recursos funciona como checkpoint. ``raw_lm`` identifica a
tradução bruta e ``reviewed_lm`` a revisão contextual já limitada ao espaço
que o recurso ocupa no jogo. O modo ``all`` executa as duas etapas e, somente
quando não restar trabalho, incorpora os recursos ao catálogo do runtime.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import csv
import json
import re
import time
import urllib.request
from pathlib import Path
from typing import Callable


FIELDS = ("source_en", "pt_br", "rdram_offsets", "occurrences",
          "min_raw_len", "controls", "status")
GLOSSARY = (
    "Use Silconiano para Silconian e Silconianos para Silconians; use "
    "Siliconiano para Siliconian e Siliconianos para Siliconians. Preserve "
    "Josette, Corlo, Messala, Magiteka, Gijin, Proton, Seaba, Bird, J2, "
    "Pokko, Karen, Gante, Gourmen, Pearl, Katze, Harben e o sufixo -san."
)


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if not rows or tuple(rows[0]) != FIELDS:
        raise SystemExit(f"Cabeçalho inesperado em {path}")
    return rows


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    temporary = path.with_suffix(path.suffix + ".new")
    with temporary.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t",
                                lineterminator="\n", quoting=csv.QUOTE_MINIMAL)
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(path)


def visible_capacity(row: dict[str, str]) -> int:
    controls = len(row["controls"].split()) if row["controls"].strip() else 0
    return max(1, int(row["min_raw_len"]) - controls * 2)


def runtime_length(text: str) -> int:
    decoded = text.replace("\\n", "\n").replace("\\r", "\r")
    return len(decoded)


def clean_translation(value: str) -> str:
    value = value.replace("\r\n", "\n").replace("\r", "\n")
    value = value.replace("\t", " ").replace("\n", "\\n")
    value = re.sub(r"[ ]{2,}", " ", value)
    return value.strip()


def request_items(endpoint: str, model: str, prompt: str,
                  batch: list[dict[str, object]]) -> dict[int, str]:
    schema = {
        "type": "json_schema",
        "json_schema": {
            "name": "wpj2_localization",
            "strict": True,
            "schema": {
                "type": "object",
                "properties": {
                    "items": {
                        "type": "array",
                        "items": {
                            "type": "object",
                            "properties": {
                                "i": {"type": "integer"},
                                "pt": {"type": "string"},
                            },
                            "required": ["i", "pt"],
                            "additionalProperties": False,
                        },
                    }
                },
                "required": ["items"],
                "additionalProperties": False,
            },
        },
    }
    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": "Responda somente em JSON, sem raciocínio."},
            {"role": "user", "content": "/no_think " + prompt + "\n\n" +
             json.dumps(batch, ensure_ascii=False)},
        ],
        "temperature": 0.05,
        "max_tokens": max(1400, len(batch) * 220),
        "reasoning_effort": "none",
        "response_format": schema,
    }
    request = urllib.request.Request(
        endpoint.rstrip("/") + "/v1/chat/completions",
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=600) as response:
        body = json.loads(response.read().decode("utf-8"))
    parsed = json.loads(body["choices"][0]["message"]["content"])
    result = {int(item["i"]): clean_translation(str(item["pt"]))
              for item in parsed["items"]}
    expected = set(range(len(batch)))
    if set(result) != expected or any(not result[i] for i in expected):
        raise ValueError(f"resposta incompleta: {len(result)}/{len(expected)}")
    return result


def brute_prompt() -> str:
    return (
        "Traduza literalmente para português brasileiro cada recurso completo "
        "de Wonder Project J2. Não resuma, não invente contexto e não omita "
        "texto. Preserve exatamente a sequência literal \\n, nomes e pontuação. "
        "O campo max_chars é o limite desejável, mas nesta etapa priorize não "
        "perder significado. " + GLOSSARY + " Retorne todos os índices."
    )


def review_prompt() -> str:
    return (
        "Revise cada tradução de Wonder Project J2 para PT-BR natural. Cada item "
        "é completamente independente: nunca copie, una ou antecipe texto de outro "
        "índice. A tradução atual pode estar contaminada; compare-a com en e remova "
        "qualquer informação ausente no inglês do próprio item. Preserve a sequência "
        "literal \\n. Tente respeitar max_chars sem perder sentido. Corrija inglês "
        "residual, nomes, gênero, gramática e frases artificiais. " + GLOSSARY +
        " Retorne todos os índices."
    )


def make_brute_batch(rows: list[dict[str, str]], indices: list[int]) -> list[dict[str, object]]:
    return [{"i": i, "en": rows[index]["source_en"],
             "max_chars": visible_capacity(rows[index])}
            for i, index in enumerate(indices)]


def make_review_batch(rows: list[dict[str, str]], indices: list[int]) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for i, index in enumerate(indices):
        result.append({"i": i, "en": rows[index]["source_en"],
                       "current_pt": rows[index]["pt_br"],
                       "max_chars": visible_capacity(rows[index])})
    return result


def update_status(path: Path, stage: str, rows: list[dict[str, str]],
                  failures: int = 0, finished: bool = False) -> None:
    counts: dict[str, int] = {}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    data = {"stage": stage, "counts": counts, "failures": failures,
            "finished": finished, "updated_epoch": time.time()}
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def run_stage(*, rows: list[dict[str, str]], resources: Path, status_path: Path,
              endpoint: str, model: str, batch_size: int, workers: int,
              stage: str, eligible: Callable[[dict[str, str]], bool],
              payload_factory: Callable[[list[dict[str, str]], list[int]], list[dict[str, object]]],
              prompt: str, next_status: str) -> int:
    pending = [index for index, row in enumerate(rows) if eligible(row)]
    total = len(pending)
    if not pending:
        update_status(status_path, stage, rows, finished=True)
        print(f"{stage}: nada pendente", flush=True)
        return 0
    batches = [pending[pos:pos + batch_size]
               for pos in range(0, len(pending), batch_size)]
    failures = 0
    completed = 0

    def process(indices: list[int]) -> dict[int, str]:
        def request_segment(segment: list[int]) -> dict[int, str]:
            payload = payload_factory(rows, segment)
            last_error: Exception | None = None
            for attempt in range(1, 4):
                try:
                    local = request_items(endpoint, model, prompt, payload)
                    return {index: local[pos] for pos, index in enumerate(segment)}
                except Exception as exc:  # processo retomável: divide lote ruim
                    last_error = exc
                    time.sleep(attempt * 2)
            if len(segment) > 1:
                middle = len(segment) // 2
                recovered = request_segment(segment[:middle])
                recovered.update(request_segment(segment[middle:]))
                return recovered
            raise RuntimeError(str(last_error))

        return request_segment(indices)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        future_map = {executor.submit(process, batch): batch for batch in batches}
        for future in concurrent.futures.as_completed(future_map):
            indices = future_map[future]
            try:
                answer = future.result()
                for index, translated in answer.items():
                    rows[index]["pt_br"] = translated
                    rows[index]["status"] = next_status
                completed += len(answer)
                write_rows(resources, rows)
            except Exception as exc:
                failures += len(indices)
                print(f"{stage}: lote falhou ({len(indices)} itens): {exc}", flush=True)
            update_status(status_path, stage, rows, failures=failures)
            print(f"{stage}: processados={completed}/{total}; falhas={failures}", flush=True)
    update_status(status_path, stage, rows, failures=failures, finished=failures == 0)
    return failures


def merge_runtime(rows: list[dict[str, str]], canonical: Path) -> int:
    lines = canonical.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != "source_en\tpt_br":
        raise SystemExit(f"Cabeçalho inesperado em {canonical}")
    order: list[str] = []
    values: dict[str, str] = {}
    for line in lines[1:]:
        if not line or "\t" not in line:
            continue
        source, value = line.split("\t", 1)
        order.append(source)
        values[source] = value
    changed = 0
    for row in rows:
        if row["status"] not in {"translated", "reviewed_lm", "reviewed_manual",
                                 "composed_reviewed"} or not row["pt_br"]:
            continue
        source = row["source_en"]
        if source not in values:
            order.append(source)
        if values.get(source) != row["pt_br"]:
            values[source] = row["pt_br"]
            changed += 1
    canonical.write_text("source_en\tpt_br\n" + "\n".join(
        f"{source}\t{values[source]}" for source in order) + "\n", encoding="utf-8")
    return changed


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", choices=("brute", "review", "all"), default="all")
    parser.add_argument("--resources", type=Path, default=Path("textos/recursos_completos_en.tsv"))
    parser.add_argument("--canonical", type=Path, default=Path("textos/traducao_ptbr.tsv"))
    parser.add_argument("--status", type=Path,
                        default=Path("temp/lm_local/traducao_lm_status.json"))
    parser.add_argument("--endpoint", default="http://127.0.0.1:1234")
    parser.add_argument("--model", default="qwen2.5-coder-7b-instruct")
    parser.add_argument("--batch-size", type=int, default=10)
    parser.add_argument("--workers", type=int, choices=(1, 2, 3, 4), default=1)
    parser.add_argument("--force-review-all", action="store_true",
                        help="Reabre todas as revisões da LM antes da etapa review")
    args = parser.parse_args()

    rows = read_rows(args.resources)
    if args.force_review_all:
        for row in rows:
            if row["status"] == "reviewed_lm":
                row["status"] = "raw_lm"
        write_rows(args.resources, rows)
    if args.stage in {"brute", "all"}:
        failed = run_stage(
            rows=rows, resources=args.resources, status_path=args.status,
            endpoint=args.endpoint, model=args.model, batch_size=args.batch_size,
            workers=args.workers, stage="brute",
            eligible=lambda row: row["status"] == "missing" or not row["pt_br"],
            payload_factory=make_brute_batch, prompt=brute_prompt(), next_status="raw_lm")
        if failed:
            raise SystemExit(f"tradução bruta deixou {failed} itens; execute novamente para retomar")
    if args.stage in {"review", "all"}:
        failed = run_stage(
            rows=rows, resources=args.resources, status_path=args.status,
            endpoint=args.endpoint, model=args.model, batch_size=args.batch_size,
            workers=args.workers, stage="review",
            eligible=lambda row: row["status"] == "raw_lm",
            payload_factory=make_review_batch, prompt=review_prompt(), next_status="reviewed_lm")
        if failed:
            raise SystemExit(f"revisão deixou {failed} itens; execute novamente para retomar")
    remaining = sum(row["status"] in {"missing", "raw_lm"} or not row["pt_br"] for row in rows)
    if args.stage == "all" and remaining == 0:
        changed = merge_runtime(rows, args.canonical)
        update_status(args.status, "complete", rows, finished=True)
        print(f"pipeline concluído; recursos incorporados/atualizados={changed}", flush=True)


if __name__ == "__main__":
    main()
