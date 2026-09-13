# PRG32 Performance Test: Execution, Retrieval, and Extension Guide

## 1. Purpose and scope

ESP32-C6 presentation now expands the 8-bit indexed game framebuffer into an
RGB565 transfer strip. Therefore `present_us` includes palette lookup and strip
preparation as well as SPI transfer. The static framebuffer saving is exactly
63,488 bytes relative to the former RGB565 viewport; real-device timing and
heap values must be collected with the procedure below and must not be inferred
from QEMU results.

The PRG32 Performance Test is a reproducible, cartridge-based instrument for
characterizing frame execution and memory behavior. It is designed for
teaching, regression analysis, and controlled platform comparisons. The test
workload is not compiled into the resident firmware: a normal portable
cartridge generates measurements and submits them to a small resident
performance broker through the public cartridge ABI.

This separation has two methodological benefits. First, the firmware contains
measurement services rather than a privileged benchmark, so student and
research cartridges use the same execution boundary. Second, a benchmark can
be replaced without reflashing the platform, while the JSON result contract
remains stable.

This guide explains how to:

1. build and run the reference test in QEMU or on an ESP32-C6 board;
2. retrieve the compact JSON result from a network-connected board;
3. interpret timing and heap measurements responsibly; and
4. create a custom performance-test cartridge with the public ABI.

For the JSON field-level reference, see
[Performance Metrics](measurement/metrics_api.md). For the executable package
contract, see [Application Binary Interface](software/abi.md).

## 2. Measurement model

### 2.1 Reference workload

The reference cartridge executes five workloads. Each workload runs for 60
frames in RGB565 mode and 60 frames in full indexed mode, producing 10 compact
case summaries and 600 measured frames in total.

| Workload | Experimental purpose |
| --- | --- |
| `clear-fill` | Measures viewport clearing and large filled-rectangle throughput. |
| `text-overlay` | Exercises repeated 8×8 text rendering and status overlays. |
| `sprite-storm` | Exercises many independently moving sprite-sized objects. |
| `scrolling` | Exercises scrolling geometry and parallax-like point fields. |
| `mixed-gameplay` | Combines text, sprites, scrolling, and playfield objects. |

Every case includes the same 24 four-color probe sprites. RGB565 cases use the
legacy RGB565 clear, pixel, rectangle, and sprite APIs. Indexed cases use
`prg32_gfx_clear_indexed`, `prg32_gfx_pixel_indexed`,
`prg32_gfx_rect_indexed`, and packed 2-bit sprites mapped into the system
palette. Text uses the shared text API and exact fixed palette colors. Scene
state is reset before every case, keeping geometry and visible colors matched
while changing the complete primitive-storage path.

### 2.2 Timing boundary

The cartridge records three explicit stages in microseconds:

- `update_us`: measured cartridge state-update work;
- `draw_us`: construction of the frame in the graphics buffer; and
- `present_us`: the call that submits the frame to the display backend.

`frame_total_us` spans from the beginning of the measured update interval to
the return of `prg32_gfx_present()`. The broker uses the supplied total when it
is nonzero; otherwise it sums the three stage values. A frame is classified as
a missed deadline when its total exceeds 33,333 µs, corresponding to a nominal
30 Hz budget.

The measurement does not represent end-to-end human-visible latency. Input
device polling, display scan-out, host scheduling, and LCD response may add
latency outside the measured interval.

### 2.3 Storage and lifetime

The broker allocates one temporary array of 32-bit frame times for the active
case. It sorts that array at case completion to calculate percentiles, retains
only a compact aggregate, and releases the array. At most 12 case aggregates
are retained.

Results are volatile:

- starting a new suite replaces the preceding result;
- aborting a run releases the active temporary allocation;
- rebooting the board or QEMU clears the result; and
- schema version 2 does not retain raw samples.

Retrieve and archive a hardware result immediately after a run if it is part
of an experiment.

## 3. Prerequisites

From the repository root, verify the development environment:

```bash
source "$HOME/esp-idf/export.sh"
python3 -m prg32 doctor
```

The reference source is
`cartridges/performancetest/src/performancetest.c`. Its build script produces
portable, metadata-bearing packages under `cartridges/performancetest/dist/`.

QEMU requires Espressif's ESP32-C3-capable QEMU distribution and the virtual
RGB display component. Hardware execution requires an ESP32-C6 board running a
compatible PRG32 firmware image and a network path to its HTTP service.

## 4. Run the reference test in QEMU

QEMU is appropriate for verifying control flow, ABI compatibility, output
layout, and relative changes within a controlled host environment. It is not a
substitute for ESP32-C6 measurements.

### Step 1: build the QEMU firmware

```bash
python3 -m prg32 qemu build
```

This creates `build-qemu/qemu_flash.bin` for the emulated ESP32-C3 target.

### Step 2: build the QEMU cartridge

```bash
PRG32_ARCHITECTURE=qemu cartridges/performancetest/scripts/build.sh
```

The store-ready output is
`cartridges/performancetest/dist/performancetest-qemu.prg32`.

### Step 3: stage the cartridge

```bash
python3 -m prg32 qemu upload \
  cartridges/performancetest/dist/performancetest-qemu.prg32 \
  --slot cart0
```

Staging replaces the contents of `cart0` in the generated QEMU flash image.

### Step 4: start QEMU

```bash
python3 -m prg32 qemu run
```

The cartridge starts automatically when `cart0` is the default or only usable
slot. Do not provide input while the cases run. Pressing B or SELECT aborts the
suite. A successful run ends with a screen containing the overall frame count,
mean FPS, mean and maximum frame time, missed deadlines, and minimum free heap.

QEMU firmware deliberately exposes no Wi-Fi address, so its result page displays
`http://<board-ip>/api/performance.json`. Treat this as an endpoint template,
not as a resolvable QEMU URL. LEFT and RIGHT switch the result table between
RGB565 and indexed color; A starts a new run. Each workload row shows mean FPS,
mean update time in microseconds, mean draw and present times in milliseconds,
and p95 total frame time in milliseconds. The values are also available in the
JSON `screen_summaries` entries as `fps_mean`, `update_us_mean`,
`draw_us_mean`, `present_us_mean`, and `frame_us_p95`. The QEMU result screen is
sufficient for execution verification; collect publishable ESP32-C6 JSON from
physical hardware.

## 5. Run the reference test on ESP32-C6

### Step 1: build and flash compatible firmware

```bash
python3 -m prg32 esp32c6 build
python3 -m prg32 esp32c6 flash
```

If compatible firmware is already installed, rebuilding and reflashing are not
required. Portable packages are still checked against the resident ABI before
upload.

### Step 2: build the hardware cartridge

```bash
PRG32_ARCHITECTURE=esp32c6 cartridges/performancetest/scripts/build.sh
```

### Step 3: determine the device base URL

In access-point mode the conventional base URL is
`http://192.168.4.1`. In infrastructure mode, use the current address shown by
PRG32 setup. The firmware does not presently advertise a stable board FQDN;
therefore documentation and cartridge output use the current IP address.

The following examples use a shell variable for clarity:

```bash
PRG32_DEVICE_URL=http://192.168.4.1
```

### Step 4: upload and launch the cartridge

```bash
python3 -m prg32 esp32c6 upload-and-run \
  cartridges/performancetest/dist/performancetest-esp32c6.prg32 \
  --url "$PRG32_DEVICE_URL" \
  --slot cart0
```

Keep power, clock configuration, display wiring, firmware revision, and ambient
conditions fixed across comparative runs. Avoid button input and concurrent
HTTP activity while measurement is active.

### Step 5: confirm completion

Wait for `PERFORMANCE TEST COMPLETE`. The displayed summary is an immediate
sanity check, not the complete research record. The result endpoint appears on
the same screen as two visual lines that form one URL:

```text
http://192.168.4.1
/api/performance.json
```

Press A only after retrieving the result; A starts a new run and replaces the
stored aggregate.

## 6. Retrieve and preserve results

The preferred CLI command validates the HTTP response and writes the JSON:

```bash
python3 -m prg32 esp32c6 performance \
  --url "$PRG32_DEVICE_URL" \
  --out data/prg32_performance_run01.json
```

The equivalent HTTP request is:

```bash
curl "$PRG32_DEVICE_URL/api/performance.json" \
  --output data/prg32_performance_run01.json
```

Before analysis, record experimental provenance alongside the JSON:

- firmware commit and dirty/clean state;
- cartridge source commit and package checksum;
- board identity and hardware revision;
- target, display backend, and build configuration;
- date, repetition number, and environmental notes; and
- whether the run was QEMU or physical hardware.

Preserve the original JSON as immutable raw data. Derived tables should be
generated into a separate directory.

## 7. Result structure and interpretation

### 7.1 Compact schema version 2

The endpoint returns a compact object with `schema_version: 2`. The principal
sections are:

| Section | Interpretation |
| --- | --- |
| Run metadata | Identifies the target, backend, suite, Wi-Fi mode, start time, and run identifier. |
| `screen_summaries` | Contains one aggregate for each completed case and color mode. |
| `memory` | Records free-heap and largest-block checkpoints before, during, and after the suite. |
| `summary` | Contains the weighted aggregate across every completed case. |
| `samples` | Empty in compact schema version 2. |
| `aggregate_windows` | Empty in compact schema version 2. |
| `comparisons` | Empty in compact schema version 2; construct comparisons by matching case index and color mode. |

`screen_count` counts distinct case indices, whereas `result_count` counts
case/mode combinations. The reference suite therefore reports 5 screens and
10 results.

### 7.2 Timing statistics

For a case containing values \(x_1,\ldots,x_n\), the broker reports the
arithmetic mean and nearest-rank percentiles after sorting the observed frame
times. In practical terms:

- `frame_us_mean` estimates typical computational cost but is sensitive to
  large outliers;
- `frame_us_p50` is the median observed frame time;
- `frame_us_p95` and `frame_us_p99` expose tail behavior;
- `frame_us_max` records the worst observed frame; and
- `fps_mean` is computed as `1,000,000 / frame_us_mean`, rather than as the
  arithmetic mean of instantaneous FPS values.

The overall `summary` provides min, mean, max, and stage means. Percentiles are
meaningful at the individual `screen_summaries` level; the current compact
overall summary does not combine case percentile distributions.

### 7.3 Stage attribution

Compare `update_us_mean`, `draw_us_mean`, and `present_us_mean` to identify the
dominant stage. A draw-heavy regression suggests changes in geometry, text, or
sprite processing. A present-heavy regression more often implicates display
synchronization or backend transfer. Interpret very small differences only
after repeated runs because timer resolution and scheduler activity contribute
noise.

### 7.4 Deadline misses

`missed_deadlines` counts frames whose measured total exceeds 33,333 µs. Zero
misses does not prove that frame pacing is perfectly uniform; inspect p95, p99,
and maximum times. Conversely, one isolated miss should be reported rather
than silently discarded, then investigated through repetition.

### 7.5 Heap measurements

`heap_min` is the smallest observed free 8-bit-capable heap during a case or
suite. In the `memory` object:

- `baseline_free_bytes` and `baseline_largest_block` are sampled at suite start;
- `peak_free_bytes` and `peak_largest_block` are the lowest observed values
  during broker operation despite the historical `peak` name; and
- `after_free_bytes` and `after_largest_block` are sampled after completion.

The largest free block is a fragmentation indicator. Similar total free heap
with a shrinking largest block can indicate fragmentation. Small residual
differences may reflect unrelated runtime activity; persistent losses across
repeated runs warrant investigation.

### 7.6 Comparing RGB565 and indexed modes

Join results using both `screen_index` and `screen_name`, then compare entries
whose `color_mode` values are `rgb565` and `indexed`. Report absolute values and
a clearly defined delta, for example:

\[
\Delta t = t_{indexed} - t_{rgb565}
\]

A positive time delta means the full indexed path was slower for that measurement.
Both source paths ultimately reach the same indexed ESP32-C6 framebuffer and
RGB565 LCD transfer, so the comparison concerns RGB565 compatibility mapping
versus direct index writes plus compact sprite decoding, not a change in LCD
wire format.

### 7.7 Poing application-level alternative

The Poing cartridge provides a complementary 300-frame suite named
`poing-indexed-renderer`. Press SELECT during normal Poing gameplay to measure
its procedural sphere, grid, shadow, stars, HUD, update logic, and explicit
present stage as one `PRG32_PERF_COLOR_INDEXED` case. It is an application-level
workload, not another condition in the five-screen reference experiment; do not
pool its results with the paired reference summaries.

Never combine QEMU and ESP32-C6 values into one performance population. QEMU
is a different target with host-dependent scheduling and display behavior.

## 8. Experimental practice

For defensible measurements:

1. state a hypothesis before collecting data;
2. keep all nonexperimental variables fixed;
3. perform a warm-up run when initialization effects are not under study;
4. collect multiple independent runs for every condition;
5. retain every valid run and disclose exclusions;
6. compare like-for-like case names and color modes; and
7. report firmware, cartridge, hardware, and tool versions.

The reference suite uses 60 observations per case. This is adequate for a
short diagnostic benchmark but is not automatically sufficient for every
statistical claim. A custom suite may increase `expected_samples`, subject to
available heap, or repeat cases as independent runs.

## 9. Performance broker ABI for custom tests

### 9.1 ABI role and compatibility

The broker was added in portable cartridge ABI 1.4 at append-only function
indices 124–132. Its public declarations are in `prg32_metrics.h`, included by
`prg32.h`. A cartridge that calls the broker must be built as portable and must
declare the `metrics` feature as required.

All extensible descriptors begin with `abi_version` and `struct_size`.
Initialize both fields exactly. This convention permits a later firmware to
recognize the version and safely reject incompatible layouts.

### 9.2 Function lifecycle

The valid lifecycle is:

```text
prg32_perf_begin
  ├─ prg32_perf_case_begin
  │    ├─ prg32_perf_record    (one or more times)
  │    └─ prg32_perf_case_end
  ├─ ... additional cases ...
  └─ prg32_perf_end
       ├─ prg32_perf_get_state
       └─ prg32_perf_get_summary
```

At any failure after suite start, call `prg32_perf_abort()`. Only one suite and
one case may be active. `prg32_perf_end()` is valid only when no case remains
active.

| Function | Responsibility |
| --- | --- |
| `prg32_perf_now_us()` | Returns the monotonic device timer in microseconds. |
| `prg32_perf_begin()` | Replaces the previous result and starts one suite. |
| `prg32_perf_case_begin()` | Starts a case and allocates its temporary frame-time array. |
| `prg32_perf_record()` | Adds one measured sample to the active case. |
| `prg32_perf_case_end()` | Computes the case aggregate and releases temporary samples. |
| `prg32_perf_end()` | Finalizes the suite and makes its result retrievable. |
| `prg32_perf_abort()` | Releases active resources and marks the suite aborted. |
| `prg32_perf_get_state()` | Copies lifecycle and heap checkpoints. |
| `prg32_perf_get_summary()` | Copies the completed overall summary. |

Most contract or lifecycle failures return `-1`. Case creation returns `-2`
when its temporary allocation fails; recording returns `-2` after
`expected_samples` values; ending an empty case returns `-2`. Treat every
nonzero return as a failed operation rather than relying on a particular error
number in application logic.

### 9.3 Descriptors and sample structure

```c
typedef struct {
    uint16_t abi_version;
    uint16_t struct_size;
    uint32_t suite_version;
    const char *name;
} prg32_perf_suite_desc_t;

typedef struct {
    uint16_t abi_version;
    uint16_t struct_size;
    uint32_t case_index;
    uint32_t color_mode;
    const char *name;
    const char *metric_goal;
} prg32_perf_case_desc_t;

typedef struct {
    uint16_t abi_version;
    uint16_t struct_size;
    uint32_t frame_index;
    uint32_t update_us;
    uint32_t draw_us;
    uint32_t present_us;
    uint32_t frame_total_us;
    uint32_t input_mask;
} prg32_perf_sample_t;
```

`case_index` identifies a logical workload. Reuse it when two cases are paired
conditions, and distinguish the conditions with `color_mode`. Valid predefined
values are `PRG32_PERF_COLOR_RGB565`, `PRG32_PERF_COLOR_INDEXED`, and
`PRG32_PERF_COLOR_CUSTOM`. Names are copied into fixed resident buffers, so use
concise stable identifiers. `metric_goal` should state what the case isolates.

`expected_samples` is both the allocation size and the hard recording limit.
At four bytes per observation, 600 expected samples require approximately
2,400 bytes plus allocator overhead. Do not reserve a large array merely as a
precaution on memory-constrained hardware.

### 9.4 State and summary outputs

`prg32_perf_get_state()` requires the caller to initialize the leading
contract fields before making the call:

```c
prg32_perf_state_t state;
state.abi_version = PRG32_PERF_ABI_VERSION;
state.struct_size = sizeof(state);

if (prg32_perf_get_state(&state) != 0) {
    /* The caller supplied an incompatible output structure. */
}
```

The returned `state` is one of `PRG32_PERF_STATE_IDLE`,
`PRG32_PERF_STATE_RUNNING`, `PRG32_PERF_STATE_COMPLETE`, or
`PRG32_PERF_STATE_ABORTED`. `case_count` is the number of compact aggregates
already committed. `active_case` is the broker's active aggregate slot while a
case runs and `-1` otherwise. The remaining fields contain the free-heap and
largest-block checkpoints described in Section 7.5.

`prg32_perf_get_summary()` succeeds only after `prg32_perf_end()` has placed
the suite in the complete state. Its output is a `prg32_performance_summary_t`:

| Field group | Contents and use |
| --- | --- |
| Identity | `run_id`, `board_id`, `target`, `display_backend`, and `game_name` identify the run and execution environment. |
| Provenance | Firmware/build fields are present for compatibility with the wider metrics schema; consumers must tolerate empty fields when the compact broker cannot populate them. |
| Extent | `duration_us`, `frames`, and `screen_count` describe the completed suite. |
| Central timing | `fps_mean_x100` and `frame_us_mean` summarize all completed cases using frame-count weighting. |
| Range | `frame_us_min` and `frame_us_max` are extrema across the retained case aggregates. |
| Stage means | `update_us_mean`, `draw_us_mean`, and `present_us_mean` are frame-count-weighted stage costs. |
| Reliability and memory | `missed_deadlines` is the total deadline count; `heap_min` is the lowest observed free heap. |

`fps_mean_x100` uses fixed-point hundredths: a value of `6326` represents
63.26 FPS. The structure also contains percentile fields shared with the wider
metrics model. Current compact suite finalization does not synthesize an
overall percentile distribution from per-case aggregates; use the percentiles
inside `screen_summaries` for inferential work.

## 10. Step-by-step: create a custom performance cartridge

### Step 1: define the scientific question

Write one falsifiable question, such as: “Does clipped indexed-sprite drawing
increase mean draw time relative to unclipped indexed-sprite drawing?” Define
the controlled variables, frame count, warm-up policy, and deadline before
writing the workload.

### Step 2: create an isolated cartridge directory

Use this structure:

```text
cartridges/myperformancetest/
|-- README.md
|-- src/
|   `-- myperformancetest.c
|-- scripts/
|   `-- build.sh
`-- dist/                         generated packages
```

Store metadata beside the cartridge if it will be published. Do not modify the
resident firmware to embed the workload.

### Step 3: implement the cartridge entry points

The following minimal pattern measures one case. It uses explicit arithmetic
and error handling so the timing boundary remains auditable.

```c
#include "prg32.h"
#include <stdint.h>

#define SAMPLE_COUNT 120u

static uint32_t frame_index;
static uint64_t update_start;
static uint64_t update_end;
static int running;

static void fail_run(void) {
    prg32_perf_abort();
    running = 0;
}

void myperformancetest_init(void) {
    prg32_perf_suite_desc_t suite = {
        PRG32_PERF_ABI_VERSION,
        sizeof(prg32_perf_suite_desc_t),
        1u,
        "indexed-clipping-study"
    };
    prg32_perf_case_desc_t test_case = {
        PRG32_PERF_ABI_VERSION,
        sizeof(prg32_perf_case_desc_t),
        0u,
        PRG32_PERF_COLOR_INDEXED,
        "indexed-clipped",
        "indexed sprite clipping cost"
    };

    frame_index = 0u;
    running = prg32_perf_begin(&suite) == 0;
    if (running && prg32_perf_case_begin(&test_case, SAMPLE_COUNT) != 0) {
        fail_run();
    }
}

void myperformancetest_update(void) {
    update_start = prg32_perf_now_us();
    if (running) {
        /* Update only the state required by the experimental workload. */
    }
    update_end = prg32_perf_now_us();
}

void myperformancetest_draw(void) {
    prg32_perf_sample_t sample;
    uint64_t draw_start;
    uint64_t draw_end;
    uint64_t present_start;
    uint64_t present_end;

    if (!running) {
        return;
    }

    draw_start = prg32_perf_now_us();
    prg32_gfx_clear(PRG32_COLOR_BLACK);
    /* Draw the workload under study here. */
    draw_end = prg32_perf_now_us();

    present_start = prg32_perf_now_us();
    prg32_gfx_present();
    present_end = prg32_perf_now_us();

    sample.abi_version = PRG32_PERF_ABI_VERSION;
    sample.struct_size = sizeof(sample);
    sample.frame_index = frame_index;
    sample.update_us = (uint32_t)(update_end - update_start);
    sample.draw_us = (uint32_t)(draw_end - draw_start);
    sample.present_us = (uint32_t)(present_end - present_start);
    sample.frame_total_us = (uint32_t)(present_end - update_start);
    sample.input_mask = prg32_input_read();

    if (prg32_perf_record(&sample) != 0) {
        fail_run();
        return;
    }

    ++frame_index;
    if (frame_index == SAMPLE_COUNT) {
        if (prg32_perf_case_end() != 0 || prg32_perf_end() != 0) {
            fail_run();
        } else {
            running = 0;
        }
    }
}
```

For multiple cases, call `prg32_perf_case_end()`, reset all scene state, and
then call `prg32_perf_case_begin()` for the next descriptor. Do not include
case initialization, asset generation, allocation, or deliberate delays in a
measured frame unless they are themselves the subject of the experiment.

### Step 4: build a portable package

```bash
python3 -m prg32 cartridge build \
  cartridges/myperformancetest/src/myperformancetest.c \
  --portable \
  --entry-prefix myperformancetest \
  --name myperformancetest \
  --architecture qemu \
  --required-feature metrics \
  --out cartridges/myperformancetest/dist/myperformancetest-qemu.prg32
```

Add `--required-feature sprites` when the workload calls sprite APIs. Build a
second package with `--architecture esp32c6` for physical measurements.

### Step 5: inspect ABI requirements

```bash
python3 -m prg32 cartridge summary \
  cartridges/myperformancetest/dist/myperformancetest-qemu.prg32
```

Confirm that the package uses `abi-table` imports and declares every required
feature. A custom benchmark should not use legacy absolute imports.

For a store-ready research cartridge, add deterministic metadata and colophon
files and attach them with `python3 -m prg32 store attach-metadata`. Record the
suite purpose, version, authorship, supported architecture, and repository in
that metadata. The complete packaging contract is documented in
[Cartridge Format and Tooling](software/cartridges.md).

### Step 6: stage, execute, and retrieve

Use the QEMU procedure in Section 4 for functional validation. Then rebuild for
ESP32-C6, upload it to a physical board, repeat the experiment, and retrieve
the JSON as described in Sections 5 and 6.

### Step 7: document the experiment

The cartridge README should specify:

- research question and expected interpretation;
- exact case order and sample count;
- timing boundaries and excluded work;
- controlled state and reset behavior;
- required ABI features;
- target-specific limitations; and
- commands required to reproduce the packages and run.

## 11. Common errors

| Symptom | Likely cause | Corrective action |
| --- | --- | --- |
| `prg32_perf_begin()` returns nonzero | Another suite is active or the descriptor is invalid. | Abort the prior run and initialize version, size, and name. |
| Case creation returns `-2` | Insufficient heap for `expected_samples`. | Reduce the sample count or free cartridge allocations. |
| Recording returns `-2` | More samples were submitted than declared. | Align the loop bound and `expected_samples`. |
| Suite completion fails | A case is still active. | End the case before ending the suite. |
| Endpoint reports no result | The run was aborted, replaced, or cleared by reboot. | Complete a fresh run and retrieve it immediately. |
| QEMU shows `<board-ip>` | QEMU has no cartridge-visible Wi-Fi address. | Use the display for functional validation and hardware for HTTP retrieval. |
| Values differ strongly between runs | Host scheduling, network/display activity, thermal state, or uncontrolled initialization changed. | Stabilize the environment and collect repeated runs. |

## 12. Related documentation

- [Performance Metrics](measurement/metrics_api.md): JSON and streaming-metrics reference.
- [Pluggable Performance Cartridge ABI](measurement/performance_cartridge_abi.md): concise ABI contract.
- [Scientific Measurement Tutorial](measurement/scientific_measurement_tutorial.md): experimental design and reporting.
- [Cartridge Format and Tooling](software/cartridges.md): portable package construction.
- [QEMU Screen Emulator](usage/qemu.md): emulator installation and controls.
- [Reference cartridge README](../cartridges/performancetest/README.md): implementation-specific build notes.
