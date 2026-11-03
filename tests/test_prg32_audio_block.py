"""Host tests of the firmware AUDIO block parser.

The real ``audio_assets.c`` is compiled with a host C compiler.  Small stub
headers replace ESP-IDF and FreeRTOS, and the test file provides the few audio
runtime functions that the parser calls.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


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

void prg32_audio_lock(void) {}
void prg32_audio_unlock(void) {}
void prg32_audio_restore_defaults(void) {}

/* A track offset that the old wrapped capacity accepted: four times this value
 * is 0x10000000 bytes, far outside any cartridge image. */
#define FAR_EVENT_OFFSET 0x4000000u

/* Layout: header, one track descriptor, two events, then the data section. */
typedef struct {
    prg32_audio_block_header_t header;
    prg32_audio_track_desc_t track;
    prg32_audio_event_t events[2];
} test_block_t;

static void make_block(test_block_t *block) {
    memset(block, 0, sizeof(*block));
    memcpy(block->header.magic, PRG32_AUDIO_BLOCK_MAGIC, 4);
    block->header.version = PRG32_AUDIO_BLOCK_VERSION;
    block->header.header_size = sizeof(block->header);
    block->header.track_count = 1;
    block->header.samples_offset = sizeof(block->header);
    block->header.instruments_offset = sizeof(block->header);
    block->header.tracks_offset = sizeof(block->header);
    block->header.events_offset = offsetof(test_block_t, events);
    block->header.data_offset = sizeof(*block);
    block->header.block_size = sizeof(*block);
    block->track.event_offset = 0;
    block->track.event_count = 2;
}

static void test_well_formed_block_loads(void) {
    test_block_t block;
    make_block(&block);
    assert(prg32_audio_load_block(&block, sizeof(block)) == 0);
    assert(g_prg32_audio.tracks[0].present);
    assert(g_prg32_audio.tracks[0].events == block.events);
    assert(g_prg32_audio.tracks[0].event_count == 2);
}

static void test_empty_event_section_loads(void) {
    /* Equal offsets are what the packer writes for a block with no events. */
    test_block_t block;
    make_block(&block);
    block.header.events_offset = block.header.data_offset;
    block.track.event_count = 0;
    assert(prg32_audio_load_block(&block, sizeof(block)) == 0);
    assert(!g_prg32_audio.tracks[0].present);
}

static void test_track_past_event_section_is_rejected(void) {
    /* The ordinary case: the capacity is right, and the track exceeds it. */
    test_block_t block;
    make_block(&block);
    block.track.event_offset = 1;
    assert(prg32_audio_load_block(&block, sizeof(block)) == -1);
    assert(!g_prg32_audio.tracks[0].present);
}

static void test_events_after_data_is_rejected(void) {
    /* Without the check, data_offset - events_offset wraps, the capacity is
     * close to 2^30 events, and this track would point outside the block. */
    test_block_t block;
    make_block(&block);
    block.header.events_offset = sizeof(block);
    block.header.data_offset = sizeof(block) - sizeof(prg32_audio_event_t);
    block.track.event_offset = FAR_EVENT_OFFSET;
    block.track.event_count = 1;
    memset(&g_prg32_audio, 0, sizeof(g_prg32_audio));
    assert(prg32_audio_load_block(&block, sizeof(block)) == -1);
    assert(!g_prg32_audio.tracks[0].present);
    assert(g_prg32_audio.tracks[0].events == NULL);
}

int main(void) {
    test_well_formed_block_loads();
    test_empty_event_section_loads();
    test_track_past_event_section_is_rejected();
    test_events_after_data_is_rejected();
    return 0;
}
"""


class AudioBlockLayoutTests(unittest.TestCase):
    def test_load_block_layout_checks(self) -> None:
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler unavailable")

        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            for name, text in STUB_HEADERS.items():
                header = temporary / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(text, encoding="utf-8")
            (temporary / "test.c").write_text(TEST_SOURCE, encoding="utf-8")
            executable = temporary / "audio-block-test"
            subprocess.run(
                [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                 "-include", "stddef.h",
                 "-I", str(temporary),
                 "-I", str(AUDIO),
                 "-I", str(AUDIO / "include"),
                 str(AUDIO / "audio_assets.c"),
                 str(temporary / "test.c"), "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
