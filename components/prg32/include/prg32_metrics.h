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

int prg32_metrics_init(const prg32_metrics_config_t *config);
int prg32_metrics_start_run(void);
int prg32_metrics_stop_run(void);
int prg32_metrics_is_enabled(void);
int prg32_metrics_record(const prg32_metric_sample_t *sample);
const char *prg32_metrics_run_id(void);

#ifdef __cplusplus
}
#endif

#endif
