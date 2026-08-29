import urllib.error
import urllib.request
import argparse
import os
import datetime
from prg32.utilities.logging import *

def get_performance_esp32c6(args: argparse.Namespace) -> None:
    endpoint = args.url.rstrip("/") + "/api/performance.json"
    request = urllib.request.Request(endpoint, method="GET")
    
    out_path = args.out
    if os.path.isdir(out_path):
        dt = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        out_path = os.path.join(out_path, f"performance_{dt}.json")

    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read()
            with open(out_path, "wb") as f:
                f.write(data)
            log_ok(f"Performance data saved to {out_path}")
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"Performance data fetch failed: HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"Performance data fetch failed: {exc}") from exc
