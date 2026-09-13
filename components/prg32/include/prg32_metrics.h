#ifndef PRG32_METRICS_H
#define PRG32_METRICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int enabled;
    const char *server_url;
    const char *board_id;
    const char *target;
    const char *display_backend;
    const char *firmware_version;
    const char *firmware_git_sha;
    const char *game_name;
    uint32_t sample_period_frames;
    uint32_t upload_period_ms;
} prg32_metrics_config_t;

typedef struct {
    uint32_t frame;
    uint32_t timestamp_ms;
    uint32_t update_us;
    uint32_t draw_us;
    uint32_t present_us;
    uint32_t frame_us;
    uint32_t heap_free;
    uint32_t heap_min_free;
    uint32_t input_mask;
    uint16_t fps_x100;
    uint16_t upload_queue_depth;
    uint8_t deadline_missed;
    uint8_t reserved;
} prg32_metric_sample_t;

typedef struct {
    char run_id[72];
    char board_id[40];
    char target[24];
    char display_backend[24];
    char firmware_version[32];
    char firmware_git_sha[24];
    char game_name[40];
    char build_type[12];
    char wifi_mode[20];
    uint32_t cartridge_generation;
    uint32_t sample_period_frames;
    uint64_t started_at_device_us;
    uint32_t duration_us;
    uint32_t frames;
    uint32_t sample_count;
    uint32_t window_count;
    uint32_t screen_count;
    uint32_t fps_mean_x100;
    uint32_t frame_us_min;
    uint32_t frame_us_mean;
    uint32_t frame_us_p50;
    uint32_t frame_us_p95;
    uint32_t frame_us_p99;
    uint32_t frame_us_max;
    uint32_t missed_deadlines;
    uint32_t update_us_mean;
    uint32_t draw_us_mean;
    uint32_t present_us_mean;
    uint32_t heap_min;
} prg32_performance_summary_t;

typedef int (*prg32_performance_json_writer_t)(const char *chunk, void *ctx);

/* Public, append-only performance broker ABI. Every descriptor starts with a
 * contract version and byte size so future firmware can extend it safely. */
#define PRG32_PERF_ABI_VERSION 1u
#define PRG32_PERF_COLOR_RGB565 0u
#define PRG32_PERF_COLOR_INDEXED 1u
#define PRG32_PERF_COLOR_CUSTOM 2u
#define PRG32_PERF_STATE_IDLE 0u
#define PRG32_PERF_STATE_RUNNING 1u
#define PRG32_PERF_STATE_COMPLETE 2u
#define PRG32_PERF_STATE_ABORTED 3u

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

typedef struct {
    uint16_t abi_version;
    uint16_t struct_size;
    uint32_t state;
    uint32_t case_count;
    int32_t active_case;
    uint32_t baseline_free_bytes;
    uint32_t baseline_largest_block;
    uint32_t peak_free_bytes;
    uint32_t peak_largest_block;
    uint32_t after_free_bytes;
    uint32_t after_largest_block;
} prg32_perf_state_t;

uint64_t prg32_perf_now_us(void);
int prg32_perf_begin(const prg32_perf_suite_desc_t *suite);
int prg32_perf_case_begin(const prg32_perf_case_desc_t *desc,
                          uint32_t expected_samples);
int prg32_perf_record(const prg32_perf_sample_t *sample);
int prg32_perf_case_end(void);
int prg32_perf_end(void);
int prg32_perf_abort(void);
int prg32_perf_get_state(prg32_perf_state_t *out);
int prg32_perf_get_summary(prg32_performance_summary_t *out);

int prg32_metrics_init(const prg32_metrics_config_t *config);
int prg32_metrics_start_run(void);
int prg32_metrics_stop_run(void);
int prg32_metrics_is_enabled(void);
int prg32_metrics_record(const prg32_metric_sample_t *sample);
const char *prg32_metrics_run_id(void);

int prg32_performance_test_run(void);
int prg32_performance_has_results(void);
int prg32_performance_summary(prg32_performance_summary_t *out);
int prg32_performance_json_write(prg32_performance_json_writer_t writer,
                                 void *ctx);
char *prg32_performance_json_alloc(void);
void prg32_performance_json_free(char *json);

#ifdef __cplusplus
}
#endif

#endif
