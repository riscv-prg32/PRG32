import urllib.error
import urllib.request
import argparse
from prg32.utilities.logging import *

def get_performance_esp32c6(args: argparse.Namespace) -> None:
    endpoint = args.url.rstrip("/") + "/api/performance.json"
    request = urllib.request.Request(endpoint, method="GET")
    
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read()
            with open("prg32_performance.json", "wb") as f:
                f.write(data)
            log_ok("Performance data saved to prg32_performance.json")
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"Performance data fetch failed: HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"Performance data fetch failed: {exc}") from exc
