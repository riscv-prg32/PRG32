import urllib.error
import urllib.request
import argparse
import os
import datetime
from prg32.utilities.logging import *

def screenshot_esp32c6(args: argparse.Namespace) -> None:
    endpoint = args.url.rstrip("/") + "/api/screenshot.bmp"
    request = urllib.request.Request(endpoint, method="GET")
    
    out_path = args.out
    if os.path.isdir(out_path):
        dt = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        out_path = os.path.join(out_path, f"screenshot_{dt}.bmp")

    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read()
            with open(out_path, "wb") as f:
                f.write(data)
            log_ok(f"Screenshot saved to {out_path}")
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"Screenshot failed: HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"Screenshot failed: {exc}") from exc
