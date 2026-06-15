import argparse
import json
from pathlib import Path
from prg32.utilities.runtime_handler import cartridge_contract
from prg32.utilities.env_variables import PRG32_IMPORT_MODEL_ABI_TABLE, PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE
from prg32.store.utils import build_from_files, parse_file, summary_dict

def make_metadata(args: argparse.Namespace, architecture: str, cartridge_file: str) -> dict:
    game_id = args.id or f"org.prg32.{args.name}"
    tags = [tag.strip() for tag in (args.tags or "").split(",") if tag.strip()]
    return {
        "abi": STORE_METADATA_ABI,
        "id": game_id,
        "title": args.name,
        "version": args.version,
        "summary": args.summary or "",
        "tags": tags,
        "assets": {
            "icon": Path(args.icon).name if args.icon else "",
            "splash": Path(args.splash).name if args.splash else "",
        },
        "architectures": [{"id": architecture, "file": cartridge_file}],
    }

def attach_metadata(args: argparse.Namespace) -> None:
    build_from_files(
        args.cartridge,
        metadata_path=args.metadata,
        icon_path=args.icon,
        out_path=args.out,
        screenshot_path=args.screenshot,
        signature_path=args.signature,
        colophon_path=args.colophon,
        architecture=args.architecture,
    )
    parsed = parse_file(args.out)
    arch = ""
    if parsed.metadata:
        runtime = parsed.metadata.get("runtime", {})
        if isinstance(runtime, dict) and runtime.get("architecture"):
            arch = f" architecture={runtime['architecture']}"
    print(
        f"wrote {args.out} legacy={len(parsed.legacy_payload)} "
        f"blocks={len(parsed.blocks)}{arch}"
    )



def inspect_metadata(args: argparse.Namespace) -> None:
    parsed = parse_file(args.cartridge)
    summary = summary_dict(parsed)
    data = Path(args.cartridge).read_bytes()
    try:
        header = cartridge_contract(data)
        if header["import_model"] == PRG32_IMPORT_MODEL_ABI_TABLE:
            header["import_model"] = "abi-table"
        elif header["import_model"] == PRG32_IMPORT_MODEL_LEGACY_ABSOLUTE:
            header["import_model"] = "legacy-absolute"
        
        if header.get("abi_hash") is not None:
            header["abi_hash"] = f"0x{header['abi_hash']:08x}"
        summary.update(header)
    except SystemExit:
        pass
    print(json.dumps(summary, indent=2, sort_keys=True))
