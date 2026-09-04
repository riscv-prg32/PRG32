from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


audio_model = load_module("prg32_audio_model", ROOT / "tools" / "prg32_audio_model.py")
audio_pack = load_module("prg32audio_pack", ROOT / "tools" / "prg32audio_pack.py")


class AudioPanTests(unittest.TestCase):
    def test_center_pan_sends_full_signal_to_both_channels(self) -> None:
        self.assertEqual(audio_model.pan_left_gain(0), 255)
        self.assertEqual(audio_model.pan_right_gain(0), 255)

    def test_extreme_pan_silences_opposite_channel(self) -> None:
        self.assertEqual(audio_model.pan_left_gain(63), 0)
        self.assertEqual(audio_model.pan_right_gain(-64), 0)


class AudioMixerTests(unittest.TestCase):
    def test_mono_mix_saturates(self) -> None:
        voices = [
            audio_model.Voice(bytes([255]), volume=255),
            audio_model.Voice(bytes([255]), volume=255),
        ]
        self.assertEqual(audio_model.mix_mono(voices, 1), [32767])

    def test_sample_position_stepping(self) -> None:
        voice = audio_model.Voice(bytes([128, 255, 0]), step_fp=1 << 16)
        audio_model.mix_mono([voice], 2)
        self.assertEqual(voice.position_fp, 2 << 16)

    def test_stereo_pan_split(self) -> None:
        left = audio_model.Voice(bytes([255]), pan=-64)
        right = audio_model.Voice(bytes([255]), pan=63)
        mixed = audio_model.mix_stereo([left, right], 1)
        self.assertEqual(mixed[0][0], (255 - 128) << 8)
        self.assertEqual(mixed[0][1], (255 - 128) << 8)


class AudioSynthTests(unittest.TestCase):
    def test_synth_id_encoding_stays_outside_pcm_slots(self) -> None:
        encoded = audio_model.synth_id(audio_model.SYNTH_PULSE, 5, 9, 2)
        self.assertEqual(encoded, 0x8A56)
        self.assertGreaterEqual(encoded, 0x8000)
        self.assertLess(63, encoded)

    def test_reference_pitch_and_octave(self) -> None:
        a4 = audio_model.note_phase_increment(69, 22050)
        a5 = audio_model.note_phase_increment(81, 22050)
        self.assertLess(abs(a4 - round(440 * (1 << 32) / 22050)), 100)
        self.assertLessEqual(abs(a5 - a4 * 2), 1)

    def test_oscillators_are_deterministic_and_bipolar(self) -> None:
        for waveform in (audio_model.SYNTH_TRIANGLE, audio_model.SYNTH_SAW,
                         audio_model.SYNTH_PULSE):
            first = audio_model.SynthVoice(waveform)
            second = audio_model.SynthVoice(waveform)
            values = [first.next_raw() for _ in range(128)]
            self.assertEqual(values, [second.next_raw() for _ in range(128)])
            self.assertLess(min(values), 0)
            self.assertGreater(max(values), 0)

    def test_all_pulse_widths_have_both_polarities(self) -> None:
        for width in range(16):
            voice = audio_model.SynthVoice(audio_model.SYNTH_PULSE,
                                           note=120, pulse_width=width)
            values = [voice.next_raw() for _ in range(256)]
            self.assertIn(32766, values)
            self.assertIn(-32768, values)

    def test_noise_sequence_and_nonzero_lfsr(self) -> None:
        first = audio_model.SynthVoice(audio_model.SYNTH_NOISE, note=120)
        second = audio_model.SynthVoice(audio_model.SYNTH_NOISE, note=120)
        values = [first.next_raw() for _ in range(128)]
        self.assertEqual(values, [second.next_raw() for _ in range(128)])
        self.assertNotEqual(len(set(values)), 1)
        self.assertNotEqual(first.lfsr, 0)

    def test_adsr_reaches_sustain_and_release_deactivates(self) -> None:
        voice = audio_model.SynthVoice(audio_model.SYNTH_SAW, sample_rate=1000,
                                       attack=4, decay=4, sustain=96, release=4)
        for _ in range(200):
            voice.next_raw()
        self.assertEqual(voice.state, "sustain")
        self.assertEqual(voice.envelope, 96 * 257)
        voice.note_off()
        for _ in range(200):
            voice.next_raw()
            if not voice.active:
                break
        self.assertFalse(voice.active)
        self.assertEqual(voice.envelope, 0)

    def test_zero_release_stops_immediately(self) -> None:
        voice = audio_model.SynthVoice(audio_model.SYNTH_TRIANGLE, release=0)
        voice.note_off()
        self.assertFalse(voice.active)

    def test_filter_is_bounded_and_parameters_change_response(self) -> None:
        outputs = []
        for cutoff in range(16):
            for resonance in range(4):
                filt = audio_model.SynthFilter(cutoff, resonance)
                response = [filt.process(32767 if i == 0 else 0)
                            for i in range(512)]
                self.assertTrue(all(-32768 <= value <= 32767
                                    for value in response))
                outputs.append(tuple(response[:32]))
        self.assertGreater(len(set(outputs)), 40)

    def test_filter_silence_remains_silent(self) -> None:
        filt = audio_model.SynthFilter(15, 3)
        self.assertEqual([filt.process(0) for _ in range(32)], [0] * 32)


class AudioPackTests(unittest.TestCase):
    LEGACY_PCM_BLOCK = bytes.fromhex(
        "41554430010028000100010001000000280000003c000000440000004c000000"
        "580000005c000000000000000400000001000000040000003c0001000000c8f4"
        "0708090a00000000030000000001003c0402000000ff00000080ff80"
    )

    def test_audio_pack_header_and_descriptors(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            (tmp_path / "click.raw").write_bytes(bytes([128, 255, 128]))
            config = {
                "samples": [{"file": "click.raw", "base_note": 60}],
                "instruments": [{"sample_id": 0, "default_volume": 200}],
                "tracks": [
                    {
                        "events": [
                            {"delta": 0, "command": "PLAY_SAMPLE", "arg0": 0, "arg1": 255},
                            {"delta": 4, "command": "END"},
                        ]
                    }
                ],
            }
            block = audio_pack.pack_audio(config, tmp_path)

        header = struct.unpack_from("<4sHHHHHHIIIIII", block, 0)
        self.assertEqual(header[0], b"AUD0")
        self.assertEqual(header[3], 1)
        self.assertEqual(header[4], 1)
        self.assertEqual(header[5], 1)
        self.assertEqual(header[-1], len(block))

    def test_abi_layouts_and_synth_tracker_block(self) -> None:
        self.assertEqual(audio_pack.SAMPLE_DESC.size, 20)
        self.assertEqual(audio_pack.INSTRUMENT_DESC.size, 8)
        self.assertEqual(audio_pack.EVENT.size, 4)
        self.assertEqual(audio_pack.TRACK_DESC.size, 8)
        self.assertEqual(audio_pack.HEADER.size, 40)
        synth = audio_model.synth_id(audio_model.SYNTH_SAW, 8, 12, 2)
        config = {
            "instruments": [{
                "sample_id": synth, "default_volume": 220, "default_pan": -20,
                "attack": 4, "decay": 16, "sustain": 180, "release": 24,
            }],
            "tracks": [{"events": [
                {"command": "NOTE_ON", "arg0": 0, "arg1": 60},
                {"delta": 4, "command": "SET_PAN", "arg0": 0, "arg1": 32},
                {"delta": 8, "command": "NOTE_OFF", "arg0": 0},
                {"command": "END"},
            ]}],
        }
        block = audio_pack.pack_audio(config, Path("."))
        header = audio_pack.HEADER.unpack_from(block)
        instrument_offset = header[8]
        event_offset = header[10]
        self.assertEqual(audio_pack.INSTRUMENT_DESC.unpack_from(
            block, instrument_offset)[0], synth)
        commands = [audio_pack.EVENT.unpack_from(block, event_offset + i * 4)[1]
                    for i in range(4)]
        self.assertEqual(commands, [1, 4, 2, 255])

    def test_legacy_pcm_audio_block_is_byte_for_byte_unchanged(self) -> None:
        """Freeze the AUD0 v1 representation used before synth support."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            (tmp_path / "tone.raw").write_bytes(bytes([0, 128, 255, 128]))
            config = {
                "samples": [{
                    "file": "tone.raw", "base_note": 60, "loop": True,
                    "loop_start": 1, "loop_end": 4,
                }],
                "instruments": [{
                    "sample_id": 0, "default_volume": 200,
                    "default_pan": -12, "attack": 7, "decay": 8,
                    "sustain": 9, "release": 10,
                }],
                "tracks": [{"events": [
                    {"command": "NOTE_ON", "arg0": 0, "arg1": 60},
                    {"delta": 4, "command": "NOTE_OFF", "arg0": 0},
                    {"command": "END"},
                ]}],
            }
            rebuilt = audio_pack.pack_audio(config, tmp_path)
        self.assertEqual(rebuilt, self.LEGACY_PCM_BLOCK)

        header = audio_pack.HEADER.unpack_from(self.LEGACY_PCM_BLOCK)
        self.assertEqual(header[0], b"AUD0")
        self.assertEqual(header[1], 1)
        self.assertEqual(header[-1], len(self.LEGACY_PCM_BLOCK))


if __name__ == "__main__":
    unittest.main()
