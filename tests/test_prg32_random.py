"""Host test of the firmware's bounded random-number routine."""

from pathlib import Path
import shutil
import subprocess
import tempfile

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_random_number_range_and_rejection_sampling() -> None:
    compiler = shutil.which("cc")
    if compiler is None:
        pytest.skip("host C compiler unavailable")

    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        (temporary / "prg32.h").write_text(
            "#include <stdint.h>\n"
            "uint32_t prg32_random_number(uint32_t min, uint32_t max);\n",
            encoding="utf-8",
        )
        (temporary / "esp_random.h").write_text(
            "#include <stdint.h>\nuint32_t esp_random(void);\n",
            encoding="utf-8",
        )
        (temporary / "test.c").write_text(
            """#include <assert.h>
#include <stdint.h>
#include "prg32.h"

static const uint32_t samples[] = {0, 1, UINT32_MAX, 0, UINT32_MAX};
static unsigned sample_index;

uint32_t esp_random(void) {
    assert(sample_index < sizeof(samples) / sizeof(samples[0]));
    return samples[sample_index++];
}

int main(void) {
    assert(prg32_random_number(8, 8) == 8);
    assert(prg32_random_number(9, 8) == 9);
    assert(sample_index == 0);

    /* A three-value range rejects zero, then maps one to eleven. */
    assert(prg32_random_number(10, 12) == 11);
    assert(sample_index == 2);
    assert(prg32_random_number(0, UINT32_MAX) == UINT32_MAX);
    assert(sample_index == 3);

    /* The full high-end interval wraps the intermediate span safely. */
    assert(prg32_random_number(UINT32_MAX - 1, UINT32_MAX) == UINT32_MAX - 1);
    assert(prg32_random_number(UINT32_MAX - 1, UINT32_MAX) == UINT32_MAX);
    assert(sample_index == 5);
    return 0;
}
""",
            encoding="utf-8",
        )
        executable = temporary / "random-test"
        subprocess.run(
            [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
             "-I", str(temporary),
             str(ROOT / "components/prg32/prg32_random.c"),
             str(temporary / "test.c"), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
