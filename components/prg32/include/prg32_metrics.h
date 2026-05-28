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
