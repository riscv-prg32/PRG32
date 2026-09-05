#!/usr/bin/env python3
"""Generate the deterministic descriptor-only PRG32 AUDIO score."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


WAVE_TRIANGLE = 0
WAVE_SAW = 1
WAVE_PULSE = 2
WAVE_NOISE = 3


def synth_id(wave: int, pulse: int, cutoff: int, resonance: int) -> int:
    assert 0 <= wave <= 3
    assert 0 <= pulse <= 15
    assert 0 <= cutoff <= 15
    assert 0 <= resonance <= 3
    return 0x8000 | (resonance << 10) | (cutoff << 6) | (pulse << 2) | wave


# Five pitches per bar, encoded from a public-domain harmonic outline of BWV 846.
CHORDS = [
    (48, 52, 55, 60, 64), (48, 50, 57, 62, 65),
    (47, 50, 55, 62, 65), (48, 52, 55, 60, 64),
    (48, 52, 57, 64, 69), (48, 50, 54, 57, 62),
    (47, 50, 55, 62, 67), (47, 48, 52, 55, 60),
    (45, 48, 52, 55, 60), (50, 57, 62, 66, 72),
    (43, 47, 50, 55, 59), (48, 55, 60, 64, 71),
    (42, 45, 48, 57, 60), (43, 47, 50, 55, 59),
    (48, 55, 60, 64, 67), (48, 50, 57, 62, 65),
    (47, 50, 55, 62, 65), (48, 52, 55, 60, 64),
    (48, 52, 57, 64, 69), (50, 54, 57, 62, 66),
    (43, 50, 55, 59, 65), (48, 52, 55, 60, 64),
    (41, 48, 53, 57, 60), (41, 47, 50, 53, 59),
    (40, 43, 48, 55, 60), (40, 41, 45, 48, 53),
    (38, 41, 45, 48, 53), (43, 50, 55, 59, 65),
    (48, 52, 55, 60, 64), (48, 55, 60, 64, 67),
    (47, 48, 55, 59, 62), (48, 52, 55, 60, 64),
]

ARP = (0, 1, 2, 3, 4, 2, 1, 2, 0, 1, 2, 3, 4, 2, 1, 2)


def event(delta: int, command: str, arg0: int | None = None,
          arg1: int | None = None) -> dict[str, int | str]:
    result: dict[str, int | str] = {"delta": delta, "command": command}
    if arg0 is not None:
        result["arg0"] = arg0
    if arg1 is not None:
        result["arg1"] = arg1
    return result


def build_score() -> dict:
    instruments = [
        # triangle pedal, warm and centered
        {"sample_id": synth_id(WAVE_TRIANGLE, 8, 10, 1), "default_volume": 180,
         "default_pan": -8, "attack": 5, "decay": 26, "sustain": 190, "release": 45},
        # pulse lower pad
        {"sample_id": synth_id(WAVE_PULSE, 6, 9, 2), "default_volume": 118,
         "default_pan": -46, "attack": 15, "decay": 38, "sustain": 138, "release": 60},
        # triangle inner pad
        {"sample_id": synth_id(WAVE_TRIANGLE, 8, 12, 0), "default_volume": 112,
         "default_pan": -16, "attack": 18, "decay": 34, "sustain": 142, "release": 58},
        # filtered saw upper pad
        {"sample_id": synth_id(WAVE_SAW, 8, 7, 3), "default_volume": 100,
         "default_pan": 28, "attack": 22, "decay": 42, "sustain": 124, "release": 68},
        # narrow pulse principal arpeggio
        {"sample_id": synth_id(WAVE_PULSE, 3, 13, 1), "default_volume": 168,
         "default_pan": 42, "attack": 1, "decay": 16, "sustain": 154, "release": 18},
        # saw echo, swept by SET_PAN
        {"sample_id": synth_id(WAVE_SAW, 8, 10, 2), "default_volume": 88,
         "default_pan": -32, "attack": 2, "decay": 19, "sustain": 105, "release": 24},
        # wide pulse halo, moves opposite the echo
        {"sample_id": synth_id(WAVE_PULSE, 12, 6, 3), "default_volume": 70,
         "default_pan": 34, "attack": 28, "decay": 48, "sustain": 96, "release": 78},
        # short filtered deterministic-noise accent
        {"sample_id": synth_id(WAVE_NOISE, 8, 5, 2), "default_volume": 92,
         "default_pan": -64, "attack": 0, "decay": 5, "sustain": 0, "release": 7},
    ]

    events: list[dict[str, int | str]] = [event(0, "SET_TEMPO", 84)]
    for bar, chord in enumerate(CHORDS):
        if bar:
            for channel in (0, 1, 2, 3, 6):
                events.append(event(0, "NOTE_OFF", channel))

        # Gentle long-phrase dynamic arc.
        shape = bar if bar < 16 else 31 - bar
        events.append(event(0, "SET_VOLUME", 0, 154 + shape * 2))
        events.append(event(0, "SET_VOLUME", 4, 142 + shape))

        # Four-note pad plus halo: six live voices before echoes and accents.
        for channel, pitch in enumerate(chord[:4]):
            events.append(event(0, "NOTE_ON", channel, pitch))
        events.append(event(0, "NOTE_ON", 6, chord[4] + 12))

        for step, chord_index in enumerate(ARP):
            if step:
                events.append(event(1, "NOTE_OFF", 4))
            else:
                events.append(event(0, "NOTE_OFF", 4))

            pan = -52 + ((bar * 16 + step) * 13) % 105
            events.append(event(0, "SET_PAN", 4, pan))
            events.append(event(0, "NOTE_ON", 4, chord[chord_index] + 12))

            if step & 1:
                events.append(event(0, "NOTE_OFF", 5))
                echo_pan = 56 - ((bar * 16 + step) * 17) % 113
                events.append(event(0, "SET_PAN", 5, echo_pan))
                events.append(event(0, "NOTE_ON", 5, chord[ARP[step - 1]] + 24))

            if step in (0, 8):
                events.append(event(0, "NOTE_OFF", 7))
                events.append(event(0, "SET_PAN", 7, -64 if ((bar + step) & 1) == 0 else 63))
                events.append(event(0, "NOTE_ON", 7, 84,))
                # At this point all channels 0..7 are active, exercising 8 voices.

    for channel in range(8):
        events.append(event(2 if channel == 0 else 0, "NOTE_OFF", channel))
    events.append(event(24, "JUMP", 0, 0))
    return {"instruments": instruments, "tracks": [{"events": events}]}


def validate(score: dict) -> None:
    instruments = score["instruments"]
    events = score["tracks"][0]["events"]
    assert len(instruments) == 8
    assert {item["sample_id"] & 3 for item in instruments} == {0, 1, 2, 3}
    assert all(item["sample_id"] & 0x8000 for item in instruments)
    assert all(-64 <= item["default_pan"] <= 63 for item in instruments)
    assert {ev.get("arg0") for ev in events if ev["command"] == "NOTE_ON"} == set(range(8))
    assert any(ev["command"] == "SET_PAN" and ev.get("arg1") == -64 for ev in events)
    assert any(ev["command"] == "SET_PAN" and ev.get("arg1") == 63 for ev in events)
    assert events[-1]["command"] == "JUMP"
    for channel in range(8):
        on = sum(ev["command"] == "NOTE_ON" and ev.get("arg0") == channel for ev in events)
        off = sum(ev["command"] == "NOTE_OFF" and ev.get("arg0") == channel for ev in events)
        assert on > 0 and off >= on - 1, (channel, on, off)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    score = build_score()
    validate(score)
    rendered = json.dumps(score, indent=2) + "\n"
    if args.check and args.out.exists() and args.out.read_text() != rendered:
        raise SystemExit(f"{args.out} is stale; regenerate it")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(rendered)


if __name__ == "__main__":
    main()
