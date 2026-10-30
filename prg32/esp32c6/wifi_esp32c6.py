"""Manage the Wi-Fi networks stored on a running PRG32 over the device protocol.

The password for ``wifi-set`` is read from a hidden prompt or from standard
input, never from the command line, so it does not end up in shell history.
"""

from __future__ import annotations

import argparse
import getpass
import sys

from prg32.usb_protocol import (
    ProtocolClient,
    open_stream,
    require_wifi,
    wifi_forget,
    wifi_scan,
    wifi_set,
)
from prg32.esp32c6.find_port import resolve_port
from prg32.utilities.logging import *


def _connect(args: argparse.Namespace) -> ProtocolClient:
    port = resolve_port(args.port)
    client = ProtocolClient(open_stream(port, int(args.baud), float(args.timeout)), float(args.timeout))
    try:
        require_wifi(client)
    except SystemExit:
        client.close()
        raise
    return client


def _print_status(reply: dict) -> None:
    connected = reply.get("connected") or "-"
    ip = reply.get("ip") or "-"
    print(f"connected: {connected}  ip: {ip}")
    known = reply.get("known", [])
    print("stored networks (newest first): " + (", ".join(known) if known else "none"))


def wifi_scan_esp32c6(args: argparse.Namespace) -> None:
    client = _connect(args)
    try:
        reply = wifi_scan(client)
    finally:
        client.close()
    networks = reply.get("networks", [])
    print(f"{'SSID':32} {'RSSI':>5} {'CH':>3} {'SECURITY':9} STORED")
    for network in networks:
        print(
            f"{network.get('ssid', ''):32} {network.get('rssi', 0):>5} "
            f"{network.get('channel', 0):>3} {network.get('auth', ''):9} "
            f"{'yes' if network.get('known') else ''}"
        )
    if reply.get("truncated"):
        print("(weaker networks left out to fit one reply)")
    _print_status(reply)


def wifi_set_esp32c6(args: argparse.Namespace) -> None:
    if args.password_stdin:
        password = sys.stdin.readline().rstrip("\r\n")
    else:
        password = getpass.getpass(f"Password for {args.ssid} (empty for an open network): ")
    client = _connect(args)
    try:
        reply = wifi_set(client, args.ssid, password)
    finally:
        client.close()
    log_ok(f"joined {args.ssid}")
    _print_status(reply)


def wifi_forget_esp32c6(args: argparse.Namespace) -> None:
    client = _connect(args)
    try:
        reply = wifi_forget(client, args.ssid)
    finally:
        client.close()
    log_ok(f"forgot {args.ssid}")
    _print_status(reply)
