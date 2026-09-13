#!/usr/bin/env python3
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
score = json.loads((ROOT / "assets/audio.json").read_text())
instruments = score["instruments"]
events = score["tracks"][0]["events"]

assert len(instruments) == 8
assert {instrument["sample_id"] & 3 for instrument in instruments} == {0, 1, 2, 3}
assert all(instrument["sample_id"] >= 0x8000 for instrument in instruments)
assert all(0 <= event["delta"] <= 255 for event in events)
assert all(-64 <= event.get("arg1", 0) <= 255 for event in events)
assert sum(event["command"] == "NOTE_ON" for event in events) > 700
assert sum(event["command"] == "SET_PAN" for event in events) > 500
assert {event.get("arg0") for event in events if event["command"] == "NOTE_ON"} == set(range(8))
assert events[-1] == {"delta": 24, "command": "JUMP", "arg0": 0, "arg1": 0}

metadata = json.loads((ROOT / "metadata/metadata.json").read_text())
assert metadata["abi"] == "prg32-metadata-1.0"
assert metadata["license"] == "MIT"
assert "public-domain" in metadata["summary"]
print(f"Validated {len(events)} events, {len(instruments)} instruments, all stereo/synth features.")
