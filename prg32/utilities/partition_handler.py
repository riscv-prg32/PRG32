from pathlib import Path

def parse_partition_size(token: str) -> int:
    text = token.strip().lower()
    if not text:
        raise ValueError("empty partition size")
    mult = 1
    if text.endswith("k"):
        mult = 1024
        text = text[:-1]
    elif text.endswith("m"):
        mult = 1024 * 1024
        text = text[:-1]
    base = 16 if text.startswith("0x") else 10
    return int(text, base) * mult

def read_partition_slot(path: Path, slot: str) -> tuple[int, int]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise SystemExit(f"failed to read partition table {path}: {exc}") from exc

    for raw in lines:
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            continue
        if cols[0] != slot:
            continue
        try:
            offset = parse_partition_size(cols[3])
            size = parse_partition_size(cols[4])
        except ValueError as exc:
            raise SystemExit(
                f"invalid partition values for {slot} in {path}: {exc}"
            ) from exc
        return (offset, size)

    raise SystemExit(f"partition slot '{slot}' not found in {path}")
