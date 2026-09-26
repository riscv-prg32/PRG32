"""Host tests of the firmware synth decoder and tracker timing.

The real ``audio_synth.c`` and ``audio_tracker.c`` are compiled with a host C
compiler.  Small stub headers replace ESP-IDF and FreeRTOS, and the test file
records the mixer calls that the tracker makes.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile

import pytest


ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT / "components" / "prg32_audio"

STUB_HEADERS = {
    "sdkconfig.h": "",
    "freertos/FreeRTOS.h": "#include <stdio.h>\n",
    "freertos/semphr.h": "typedef void *SemaphoreHandle_t;\n",
    "freertos/task.h": "typedef void *TaskHandle_t;\n",
}

TEST_SOURCE = r"""
#include <assert.h>
#include <string.h>
#include "audio_internal.h"

prg32_audio_state_t g_prg32_audio;

/* Mixer stubs: record NOTE_ON order and the tick it happened on. */
static int note_count;
static uint8_t note_channels[64];
static uint32_t note_ticks[64];
static uint32_t current_tick;

void prg32_audio_lock(void) {}
void prg32_audio_unlock(void) {}
void prg32_audio_note_on(uint8_t channel, uint8_t instrument, uint8_t note,
                         uint8_t volume) {
    (void)instrument; (void)note; (void)volume;
    if (note_count < 64) {
        note_channels[note_count] = channel;
        note_ticks[note_count] = current_tick;
    }
    note_count++;
}
void prg32_audio_note_off(uint8_t channel) { (void)channel; }
void prg32_audio_set_channel_volume(uint8_t channel, uint8_t volume) {
    (void)channel; (void)volume;
}
void prg32_audio_set_channel_pan(uint8_t channel, int8_t pan) {
    (void)channel; (void)pan;
}
int prg32_audio_play_sample(uint16_t sample_id, uint8_t volume,
                            uint16_t pitch) {
    (void)sample_id; (void)volume; (void)pitch;
    return 0;
}

static void load_track(const prg32_audio_event_t *events, uint32_t count) {
    memset(&g_prg32_audio, 0, sizeof(g_prg32_audio));
    g_prg32_audio.tracker.tempo_bpm = 125; /* 60000 / (125 * 4) = 120 ms */
    g_prg32_audio.tracks[0].events = events;
    g_prg32_audio.tracks[0].event_count = count;
    g_prg32_audio.tracks[0].present = true;
    note_count = 0;
    current_tick = 0;
    prg32_audio_play_track(0);
}

static void test_synth_decoding(void) {
    for (unsigned wave = 0; wave < 4; wave++) {
        for (unsigned pulse = 0; pulse < 16; pulse++) {
            for (unsigned cutoff = 0; cutoff < 16; cutoff++) {
                for (unsigned res = 0; res < 4; res++) {
                    prg32_instrument_desc_t desc = {0};
                    prg32_audio_voice_t voice = {0};
                    desc.sample_id = PRG32_AUDIO_SYNTH_ID(wave, pulse,
                                                          cutoff, res);
                    prg32_audio_synth_start(&voice, &desc, 60, 22050);
                    assert(voice.synth_state.waveform == wave);
                    assert(voice.synth_state.cutoff == cutoff);
                    assert(voice.synth_state.resonance == res);
                    assert(voice.synth_state.pulse_threshold ==
                           (uint32_t)(((uint64_t)(pulse + 1u) << 32) / 17u));
                }
            }
        }
    }
    /* The example from the bug report used to decode resonance 6. */
    prg32_instrument_desc_t desc = {0};
    prg32_audio_voice_t voice = {0};
    desc.sample_id = PRG32_AUDIO_SYNTH_PULSE(8, 15, 0);
    prg32_audio_synth_start(&voice, &desc, 60, 22050);
    assert(voice.synth_state.cutoff == 15);
    assert(voice.synth_state.resonance == 0);
}

static void test_delta_zero_events_share_a_tick(void) {
    /* A three-note chord, a four-tick wait, then one more note. */
    static const prg32_audio_event_t events[] = {
        {0, PRG32_AUDIO_CMD_NOTE_ON, 0, 60},
        {0, PRG32_AUDIO_CMD_NOTE_ON, 1, 64},
        {4, PRG32_AUDIO_CMD_NOTE_ON, 2, 67},
        {0, PRG32_AUDIO_CMD_NOTE_ON, 3, 72},
        {0, PRG32_AUDIO_CMD_END, 0, 0},
    };
    load_track(events, 5);

    /* The first step plays the whole chord without waiting for a tick. */
    prg32_audio_tracker_step(1);
    assert(note_count == 3);
    for (int i = 0; i < 3; i++) {
        assert(note_channels[i] == i);
        assert(note_ticks[i] == 0);
    }

    /* The next note arrives only after exactly four 120 ms ticks. */
    for (current_tick = 1; current_tick <= 4; current_tick++) {
        assert(note_count == 3);
        prg32_audio_tracker_step(120);
    }
    assert(note_count == 4);
    assert(note_channels[3] == 3);
    assert(note_ticks[3] == 4);
    assert(!g_prg32_audio.tracker.active);
}

static void test_delta_zero_jump_loop_is_capped(void) {
    static const prg32_audio_event_t events[] = {
        {0, PRG32_AUDIO_CMD_NOTE_ON, 0, 60},
        {0, PRG32_AUDIO_CMD_JUMP, 0, 0},
    };
    load_track(events, 2);
    prg32_audio_tracker_step(1); /* must return instead of spinning */
    assert(note_count == 128); /* 256 events: NOTE_ON, JUMP, NOTE_ON, ... */
    assert(g_prg32_audio.tracker.active);
}

int main(void) {
    test_synth_decoding();
    test_delta_zero_events_share_a_tick();
    test_delta_zero_jump_loop_is_capped();
    return 0;
}
"""


def test_audio_synth_decoder_and_tracker_timing() -> None:
    compiler = shutil.which("cc")
    if compiler is None:
        pytest.skip("host C compiler unavailable")

    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        for name, text in STUB_HEADERS.items():
            header = temporary / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text(text, encoding="utf-8")
        (temporary / "test.c").write_text(TEST_SOURCE, encoding="utf-8")
        executable = temporary / "audio-engine-test"
        subprocess.run(
            [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
             "-I", str(temporary),
             "-I", str(AUDIO),
             "-I", str(AUDIO / "include"),
             str(AUDIO / "audio_synth.c"),
             str(AUDIO / "audio_tracker.c"),
             str(temporary / "test.c"), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True, stdout=subprocess.DEVNULL)
