"""Send key commands to a running PRG32 over the device protocol.

Each argument is one step, pressed after the previous one has been released.
``A+B`` presses two keys together, so ``key UP UP A+B`` is three steps.
"""

from __future__ import annotations

import argparse

from prg32.usb_protocol import (
    ProtocolClient,
    key_release,
    key_sequence,
    normalize_keys,
    open_stream,
    require_keys,
)
from prg32.utilities.logging import *


def key_esp32c6(args: argparse.Namespace) -> None:
    if not args.keys and not args.release:
        raise SystemExit("name at least one key, or use --release")
    if args.gap_ms < 0:
        raise SystemExit("--gap-ms cannot be negative")
    client = ProtocolClient(open_stream(args.port, int(args.baud), float(args.timeout)), float(args.timeout))
    try:
        allowed = require_keys(client)
        steps = [normalize_keys(step.split("+"), allowed) for step in args.keys]
        if args.release:
            key_release(client, [key for step in steps for key in step])
        else:
            key_sequence(client, steps, args.hold_ms, args.gap_ms)
    finally:
        client.close()
    if args.release:
        log_ok("released " + (" ".join(args.keys) or "all keys"))
    else:
        log_ok(f"pressed {' '.join(args.keys)} (hold {args.hold_ms} ms)")
