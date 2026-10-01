"""Client helpers for the optional PRG32-QT cartridge debugger."""

from __future__ import annotations

import json
import urllib.request


SUPPORTED_SPEEDS = (0.1, 0.25, 0.5, 1.0, 2.0, 4.0)


def debugger_request(url: str, command: str | None = None, **fields: object) -> dict:
    """Read debugger state or submit one debugger command."""
    endpoint = f"{url.rstrip('/')}/api/debug"
    data = None
    headers: dict[str, str] = {}
    if command is not None:
        data = json.dumps({"command": command, **fields}).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(endpoint, data=data, headers=headers)
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.load(response)


def debug_cli(args) -> None:
    """Execute a debugger command and print its JSON response."""
    fields: dict[str, object] = {}
    if args.command == "enable":
        fields["enabled"] = True
    elif args.command == "disable":
        args.command = "enable"
        fields["enabled"] = False
    elif args.command == "speed":
        if args.speed is None:
            raise SystemExit("debug speed requires --speed")
        fields["speed"] = args.speed
    state = debugger_request(args.url, args.command if args.command != "state" else None, **fields)
    print(json.dumps(state, indent=2, sort_keys=True))
