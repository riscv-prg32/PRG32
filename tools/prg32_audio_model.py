"""Small host-side reference model for PRG32 audio unit tests."""

from __future__ import annotations

from dataclasses import dataclass


PAN_LEFT = -64
PAN_RIGHT = 63
SYNTH_MARKER = 0x8000
SYNTH_TRIANGLE = 0
SYNTH_SAW = 1
SYNTH_PULSE = 2
SYNTH_NOISE = 3
LFSR_MASK = 0x7FFFFF
LFSR_SEED = 0x7FFFF8


def synth_id(waveform: int, pulse_width: int, cutoff: int, resonance: int) -> int:
    return (SYNTH_MARKER | ((resonance & 3) << 10) | ((cutoff & 15) << 6)
            | ((pulse_width & 15) << 2) | (waveform & 3))


def note_phase_increment(note: int, sample_rate: int) -> int:
    midi0_q16 = [
        535808, 567673, 601430, 637194, 675084, 715221,
        757750, 802808, 850542, 901120, 954692, 1011457,
    ]
    frequency_q16 = min(0xFFFFFFFF, midi0_q16[note % 12] << (note // 12))
    return min(0xFFFFFFFF, (frequency_q16 << 16) // sample_rate)


def envelope_samples(value: int, sample_rate: int) -> int:
    if value == 0:
        return 0
    milliseconds = 1 + value * value * 2000 // 65025
    return max(1, sample_rate * milliseconds // 1000)


@dataclass
class SynthFilter:
    cutoff: int
    resonance: int
    low: int = 0
    band: int = 0

    def process(self, value: int) -> int:
        cutoff_q15 = [384, 512, 704, 960, 1280, 1696, 2208, 2848,
                      3616, 4512, 5536, 6688, 7936, 9280, 10752, 12288]
        damping_q15 = [32767, 24576, 16384, 8192]
        limit = 1 << 22
        self.low = max(-limit, min(limit,
            self.low + (cutoff_q15[self.cutoff & 15] * self.band >> 15)))
        high = max(-limit, min(limit, value - self.low -
            (damping_q15[self.resonance & 3] * self.band >> 15)))
        self.band = max(-limit, min(limit,
            self.band + (cutoff_q15[self.cutoff & 15] * high >> 15)))
        return max(-32768, min(32767, self.low))


@dataclass
class SynthVoice:
    waveform: int
    note: int = 69
    sample_rate: int = 22050
    pulse_width: int = 8
    attack: int = 0
    decay: int = 0
    sustain: int = 255
    release: int = 0
    phase: int = 0
    lfsr: int = LFSR_SEED
    envelope: int = 0
    state: str = "attack"
    active: bool = True

    def __post_init__(self) -> None:
        self.increment = note_phase_increment(self.note, self.sample_rate)
        self.threshold = ((self.pulse_width + 1) << 32) // 17
        if self.attack == 0:
            self.envelope = 65535
            self._begin_decay()

    def _begin_decay(self) -> None:
        self.state = "decay"
        target = self.sustain * 257
        samples = envelope_samples(self.decay, self.sample_rate)
        if samples == 0 or self.envelope <= target:
            self.envelope = target
            self.state = "sustain"
        else:
            self.step = max(1, (self.envelope - target + samples - 1) // samples)

    def note_off(self) -> None:
        samples = envelope_samples(self.release, self.sample_rate)
        if samples == 0 or self.envelope == 0:
            self.envelope = 0
            self.active = False
            self.state = "off"
        else:
            self.state = "release"
            self.step = max(1, (self.envelope + samples - 1) // samples)

    def _advance_envelope(self) -> None:
        if self.state == "attack":
            samples = envelope_samples(self.attack, self.sample_rate)
            step = max(1, (65535 + samples - 1) // samples)
            self.envelope = min(65535, self.envelope + step)
            if self.envelope == 65535:
                self._begin_decay()
        elif self.state == "decay":
            target = self.sustain * 257
            self.envelope = max(target, self.envelope - self.step)
            if self.envelope == target:
                self.state = "sustain"
        elif self.state == "release":
            self.envelope = max(0, self.envelope - self.step)
            if self.envelope == 0:
                self.active = False
                self.state = "off"

    def next_raw(self) -> int:
        old_phase = self.phase
        self.phase = (self.phase + self.increment) & 0xFFFFFFFF
        if self.waveform == SYNTH_NOISE and self.phase < old_phase:
            feedback = ((self.lfsr >> 22) ^ (self.lfsr >> 17)) & 1
            self.lfsr = ((self.lfsr << 1) & LFSR_MASK) | feedback
            if self.lfsr == 0:
                self.lfsr = LFSR_SEED
        if self.waveform == SYNTH_TRIANGLE:
            ramp = self.phase & 0x7FFFFFFF
            if self.phase & 0x80000000:
                ramp = 0x7FFFFFFF - ramp
            raw = (ramp >> 15) - 32768
        elif self.waveform == SYNTH_SAW:
            raw = (self.phase >> 16) - 32768
        elif self.waveform == SYNTH_PULSE:
            raw = 32767 if self.phase < self.threshold else -32768
        else:
            raw = ((self.lfsr >> 7) & 0xFFFF) - 32768
        self._advance_envelope()
        return raw * self.envelope >> 16


def pan_left_gain(pan: int) -> int:
    pan = max(PAN_LEFT, min(PAN_RIGHT, pan))
    if pan <= 0:
        return 255
    return 255 - pan * 255 // PAN_RIGHT


def pan_right_gain(pan: int) -> int:
    pan = max(PAN_LEFT, min(PAN_RIGHT, pan))
    if pan >= 0:
        return 255
    return 255 - (-pan) * 255 // (-PAN_LEFT)


def clamp16(value: int) -> int:
    return max(-32768, min(32767, value))


@dataclass
class Voice:
    sample: bytes
    position_fp: int = 0
    step_fp: int = 1 << 16
    volume: int = 255
    pan: int = 0
    loop: bool = False
    loop_start: int = 0
    loop_end: int = 0
    active: bool = True

    def next_pcm(self) -> int:
        index = self.position_fp >> 16
        if index >= len(self.sample):
            if not self.loop or self.loop_end <= self.loop_start:
                self.active = False
                return 0
            index = self.loop_start
            self.position_fp = index << 16
        value = (self.sample[index] - 128) << 8
        value = value * self.volume // 255
        self.position_fp += self.step_fp
        if self.loop and (self.position_fp >> 16) >= self.loop_end:
            self.position_fp = self.loop_start << 16
        return value


def mix_mono(voices: list[Voice], frames: int) -> list[int]:
    out: list[int] = []
    for _ in range(frames):
        out.append(clamp16(sum(v.next_pcm() for v in voices if v.active)))
    return out


def mix_stereo(voices: list[Voice], frames: int) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for _ in range(frames):
        left = 0
        right = 0
        for voice in voices:
            if not voice.active:
                continue
            pcm = voice.next_pcm()
            left += pcm * pan_left_gain(voice.pan) // 255
            right += pcm * pan_right_gain(voice.pan) // 255
        out.append((clamp16(left), clamp16(right)))
    return out
