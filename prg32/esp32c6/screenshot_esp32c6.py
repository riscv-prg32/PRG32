import urllib.error
import urllib.request
import argparse
from prg32.utilities.logging import *

def screenshot_esp32c6(args: argparse.Namespace) -> None:
    endpoint = args.url.rstrip("/") + "/api/screenshot.bmp"
    request = urllib.request.Request(endpoint, method="GET")
    
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read()
            with open("screenshot.bmp", "wb") as f:
                f.write(data)
            log_ok("Screenshot saved to screenshot.bmp")
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        raise SystemExit(f"Screenshot failed: HTTP {exc.code}: {body}") from exc
    except urllib.error.URLError as exc:
        raise SystemExit(f"Screenshot failed: {exc}") from exc
