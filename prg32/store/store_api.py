import argparse
import urllib.request
import urllib.error
import urllib.parse
from pathlib import Path
from prg32.store.utils import json_request, store_url, catalog_items, STORE_DISCOVERY_ABI

def store_discover(args: argparse.Namespace) -> None:
    try:
        from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
    except Exception as exc:
        print("zeroconf not installed. Run: pip install zeroconf")
        raise SystemExit(1) from exc

    class Listener(ServiceListener):
        def __init__(self) -> None:
            self.found: list[tuple[str, str]] = []

        def add_service(self, zc, service_type, name):
            info = zc.get_service_info(service_type, name)
            if not info or not info.addresses:
                return
            address = ".".join(str(part) for part in info.addresses[0])
            url = f"http://{address}:{info.port}"
            self.found.append((name.split("._", 1)[0].rstrip("."), url))

        def update_service(self, zc, service_type, name):
            self.add_service(zc, service_type, name)

        def remove_service(self, zc, service_type, name):
            return None

    zc = Zeroconf()
    listener = Listener()
    ServiceBrowser(zc, "_prg32store._tcp.local.", listener)
    try:
        import time
        time.sleep(args.timeout)
    finally:
        zc.close()
    for name, url in listener.found:
        abi = ""
        try:
            body = json_request(url + "/.well-known/prg32-store.json", timeout=3)
            abi = body.get("abi", "")
            name = body.get("name", name)
        except SystemExit:
            pass
        print(f"Found: {name}")
        print(f"  URL: {url}")
        print(f"  ABI: {abi or STORE_DISCOVERY_ABI}")


def store_list(args: argparse.Namespace) -> None:
    body = json_request(store_url(args) + "/api/games?limit=100")
    rows = catalog_items(body)
    print(f"{'ID':32} {'Title':24} {'Version':8} Architectures")
    for item in rows:
        archs = item.get("architectures", [])
        if args.architecture and args.architecture not in archs:
            continue
        if isinstance(archs, list):
            arch_text = ", ".join(str(a) for a in archs)
        else:
            arch_text = str(archs)
        print(
            f"{str(item.get('id', ''))[:32]:32} "
            f"{str(item.get('title', ''))[:24]:24} "
            f"{str(item.get('version', ''))[:8]:8} {arch_text}"
        )


def store_download(args: argparse.Namespace) -> None:
    query = {"architecture": args.architecture}
    if args.version:
        query["version"] = args.version
    endpoint = (
        store_url(args)
        + "/api/games/"
        + urllib.parse.quote(args.game_id, safe="")
        + "/download?"
        + urllib.parse.urlencode(query)
    )
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(endpoint, timeout=60) as response, out.open("wb") as f:
            total = int(response.headers.get("Content-Length", "0") or "0")
            done = 0
            while True:
                chunk = response.read(64 * 1024)
                if not chunk:
                    break
                f.write(chunk)
                done += len(chunk)
                if total:
                    print(f"\r{done * 100 // total:3d}% {done}/{total} bytes", end="")
            if total:
                print()
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"download failed: HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"download failed: {exc}") from exc
    print(f"saved {out}")
