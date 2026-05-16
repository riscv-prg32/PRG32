#!/usr/bin/env python3
"""Convert WAV samples or MIDI note tracks into PRG32 C or assembly assets."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import wave


def emit_c_samples(symbol: str, samples: list[int], sample_rate: int) -> str:
    lines = [
        "#include <stdint.h>",
        f"#define {symbol.upper()}_RATE {sample_rate}",
        f"#define {symbol.upper()}_COUNT {len(samples)}",
        f"const uint8_t {symbol}[] = {{",
    ]
    for i in range(0, len(samples), 16):
        lines.append("    " + ", ".join(str(v) for v in samples[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def emit_asm_samples(symbol: str, samples: list[int], sample_rate: int) -> str:
    lines = [
        f".equ {symbol.upper()}_RATE, {sample_rate}",
        f".equ {symbol.upper()}_COUNT, {len(samples)}",
        ".section .rodata",
        f".global {symbol}",
        f"{symbol}:",
    ]
    for i in range(0, len(samples), 16):
        lines.append("    .byte " + ", ".join(str(v) for v in samples[i:i + 16]))
    return "\n".join(lines) + "\n"


def read_wav(path: Path, target_rate: int | None) -> tuple[list[int], int]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        width = wav.getsampwidth()
        rate = wav.getframerate()
        frames = wav.readframes(wav.getnframes())

    samples: list[int] = []
    step = channels * width
    for i in range(0, len(frames), step):
        raw = frames[i:i + width]
        if width == 1:
            value = raw[0]
        elif width == 2:
            value16 = int.from_bytes(raw, "little", signed=True)
            value = (value16 + 32768) >> 8
        else:
            raise SystemExit("only 8-bit and 16-bit WAV files are supported")
        samples.append(value)

    if target_rate and target_rate != rate:
        ratio = rate / target_rate
        count = int(len(samples) / ratio)
        samples = [samples[min(int(i * ratio), len(samples) - 1)] for i in range(count)]
        rate = target_rate
    return samples, rate


def midi_note_to_hz(note: int) -> int:
    table = [262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494]
    freq = table[note % 12]
    octave = note // 12 - 1
    while octave > 4:
        freq *= 2
        octave -= 1
    while octave < 4 and freq > 1:
        freq //= 2
        octave += 1
    return freq


def read_midi(path: Path) -> list[tuple[int, int]]:
    try:
        import mido
    except ImportError as exc:
        raise SystemExit("MIDI conversion requires mido: python3 -m pip install mido") from exc

    midi = mido.MidiFile(path)
    notes: list[tuple[int, int]] = []
    active: dict[int, int] = {}
    elapsed = 0
    for msg in midi:
        elapsed += int(msg.time * 1000)
        if msg.type == "note_on" and msg.velocity > 0:
            active[msg.note] = elapsed
        if msg.type in ("note_off", "note_on") and msg.note in active:
            if msg.type == "note_off" or msg.velocity == 0:
                start = active.pop(msg.note)
                duration = max(20, elapsed - start)
                notes.append((midi_note_to_hz(msg.note), duration))
    return notes


def emit_c_notes(symbol: str, notes: list[tuple[int, int]]) -> str:
    lines = [
        '#include "prg32.h"',
        f"#define {symbol.upper()}_COUNT {len(notes)}",
        f"const prg32_note_t {symbol}[] = {{",
    ]
    for hz, ms in notes:
        lines.append(f"    {{{hz}, {ms}}},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def emit_asm_notes(symbol: str, notes: list[tuple[int, int]]) -> str:
    lines = [
        f".equ {symbol.upper()}_COUNT, {len(notes)}",
        ".section .rodata",
        f".global {symbol}",
        f"{symbol}:",
    ]
    for hz, ms in notes:
        lines.append(f"    .half {hz}, {ms}")
    return "\n".join(lines) + "\n"


def convert(args: argparse.Namespace) -> None:
    suffix = args.input.suffix.lower()
    if suffix in (".wav", ".wave"):
        samples, rate = read_wav(args.input, args.sample_rate)
        text = emit_c_samples(args.symbol, samples, rate)
        if args.format == "asm":
            text = emit_asm_samples(args.symbol, samples, rate)
    elif suffix in (".mid", ".midi"):
        notes = read_midi(args.input)
        text = emit_c_notes(args.symbol, notes)
        if args.format == "asm":
            text = emit_asm_notes(args.symbol, notes)
    else:
        raise SystemExit("supported inputs: .wav, .mid, .midi")

    if args.out:
        args.out.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--symbol", default="audio_asset")
    parser.add_argument("--format", choices=["c", "asm"], default="c")
    parser.add_argument("--sample-rate", type=int)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    convert(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
