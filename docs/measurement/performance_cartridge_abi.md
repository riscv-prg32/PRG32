# Pluggable Performance Cartridge ABI

This file is the concise ABI contract. The canonical
[Performance Test Guide](../performance_test.md) provides the complete
execution workflow, measurement semantics, error handling, and a step-by-step
custom-cartridge tutorial.

ABI version 1 exposes a generic broker; it contains no benchmark workload.
Descriptors are public-header-only, begin with `abi_version` and
`struct_size`, and use fixed-width scalar fields.

```c
prg32_perf_suite_desc_t suite = {
  PRG32_PERF_ABI_VERSION, sizeof(suite), 1, "my-suite"
};
prg32_perf_begin(&suite);

prg32_perf_case_desc_t test = {
  PRG32_PERF_ABI_VERSION, sizeof(test), 0,
  PRG32_PERF_COLOR_CUSTOM, "case-name", "what this case measures"
};
prg32_perf_case_begin(&test, 60);
/* Measure update, draw and present with prg32_perf_now_us(). */
prg32_perf_record(&sample);
prg32_perf_case_end();
prg32_perf_end();
```

`expected_samples` sizes a temporary array of 32-bit frame times used for
percentiles. It is freed by `prg32_perf_case_end`, `prg32_perf_abort`, or
before a replacement run. Completion retains at most twelve compact case
aggregates; it does not retain samples. Call `prg32_perf_get_state` for
lifecycle and free/largest-block checkpoints, and
`prg32_perf_get_summary` for the completed aggregate.

Any failure should lead the cartridge to call `prg32_perf_abort`. Only one
suite and one case may be active. Results remain volatile until a new run or
reboot. This avoids flash wear and follows existing RAM-only result semantics.

The HTTP endpoint remains `GET /api/performance.json`. Schema version 2 keeps
the established top-level and summary names, returns compact
`screen_summaries`, and leaves `samples` and `aggregate_windows` empty.
The additive `memory` object reports broker checkpoints. Consumers needing
raw samples must collect them externally during a run.

See [the reference cartridge](../../cartridges/performancetest/README.md) for a
complete independent implementation.
