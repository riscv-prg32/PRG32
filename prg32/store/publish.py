import argparse
import tempfile
import zipfile
import json
from pathlib import Path
from prg32.store.utils import infer_architecture, post_multipart, store_url, store_token
from prg32.store.metadata import make_metadata
from prg32.cartridge.build_cartridge import build_cartridge_cli


def publish(args: argparse.Namespace) -> None:
    architecture = args.architecture or infer_architecture(args.firmware_elf)
    with tempfile.TemporaryDirectory(prefix="prg32-publish-") as tmp_s:
        tmp = Path(tmp_s)
        cart = tmp / f"{args.name}-{architecture}.prg32"
        build_args = argparse.Namespace(**vars(args))
        build_args.out = str(cart)
        build_args.runtime_url = None
        build_args.audio_block = None
        build_args.multiplayer = False
        build_args.march = "rv32imc_zicsr_zifencei"
        build_args.mabi = "ilp32"
        if not hasattr(build_args, "target"):
            build_args.target = None
        build_cartridge_cli(build_args)
        manifest = tmp / "manifest.json"
        metadata = make_metadata(args, architecture, cart.name)
        manifest.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        bundle = tmp / f"{args.name}.zip"
        with zipfile.ZipFile(bundle, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            zf.write(manifest, "manifest.json")
            zf.write(cart, cart.name)
            if getattr(args, "icon", None):
                zf.write(args.icon, Path(args.icon).name)
            if getattr(args, "splash", None):
                zf.write(args.splash, Path(args.splash).name)
            if getattr(args, "colophon", None):
                zf.write(args.colophon, Path(args.colophon).name)
        response = post_multipart(
            store_url(args) + "/api/publish/bundle",
            {},
            {"bundle": (bundle.name, bundle.read_bytes(), "application/zip")},
            store_token(args),
        )
    print(json.dumps(response, indent=2, sort_keys=True))
    if response.get("status") == "pending" or response.get("review_required"):
        print("Submitted for review")
    else:
        print("Published")

def pack_bundle(args: argparse.Namespace) -> None:
    manifest = Path(args.manifest)
    data = json.loads(manifest.read_text(encoding="utf-8"))
    base = manifest.parent
    files = {"manifest.json": manifest}
    assets = data.get("assets", {})
    if isinstance(assets, dict):
        for filename in assets.values():
            if filename:
                files[Path(filename).name] = base / filename
    for arch in data.get("architectures", []):
        filename = arch.get("file") if isinstance(arch, dict) else None
        if filename:
            files[Path(filename).name] = base / filename
    for path in files.values():
        if not path.exists():
            raise SystemExit(f"bundle file not found: {path}")
    out = Path(args.out)
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for arcname, path in files.items():
            zf.write(path, arcname)
    total = 0
    print(f"Bundle: {out}")
    for arcname, path in files.items():
        size = path.stat().st_size
        total += size
        print(f"  {arcname:28} {size / 1024:6.1f} KB")
    print(f"Total: {total / 1024:.1f} KB")


def publish_bundle(args: argparse.Namespace) -> None:
    bundle = Path(args.bundle)
    with bundle.open("rb") as f:
        if f.read(4) != b"PK\x03\x04":
            raise SystemExit(f"{bundle} is not a zip bundle")
    response = post_multipart(
        store_url(args) + "/api/publish/bundle",
        {},
        {"bundle": (bundle.name, bundle.read_bytes(), "application/zip")},
        store_token(args),
    )
    published = response.get("published") or response.get("submitted") or response
    print(json.dumps(published, indent=2, sort_keys=True))
    if response.get("status") == "pending" or response.get("review_required"):
        print("Submitted for review")
