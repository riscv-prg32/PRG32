#include "prg32.h"
#include "prg32_config.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PERF_CASES 12u
#define PERF_NAME 32u
#define PERF_GOAL 64u
#define PERF_BUDGET_US 33333u
#ifndef CONFIG_PRG32_METRICS_BOARD_ID
#define CONFIG_PRG32_METRICS_BOARD_ID "prg32-board"
#endif
#ifndef CONFIG_IDF_TARGET
#define CONFIG_IDF_TARGET "unknown"
#endif
#if CONFIG_PRG32_DISPLAY_QEMU_RGB
#define PERF_TARGET "qemu-esp32c3"
#define PERF_DISPLAY "qemu_rgb"
#else
#define PERF_TARGET CONFIG_IDF_TARGET
#define PERF_DISPLAY "ili9341"
#endif

typedef struct {
  char name[PERF_NAME], goal[PERF_GOAL];
  uint32_t index, mode, first, last, frames, fps, min, mean, p50, p95, p99,
      max, missed, update, draw, present, heap_min;
} perf_result_t;

typedef struct {
  uint32_t state, sequence, suite_version, count;
  int32_t active;
  uint64_t started, ended, update_total, draw_total, present_total, frame_total;
  uint32_t baseline_free, baseline_largest, peak_free, peak_largest;
  uint32_t after_free, after_largest, *values, capacity;
  char name[PERF_NAME];
  perf_result_t results[PERF_CASES];
  prg32_performance_summary_t summary;
} perf_broker_t;

static perf_broker_t g;

static void text_copy(char *dst, size_t size, const char *src) {
  if (!dst || !size) return;
  size_t i=0;
  for (;src&&src[i]&&i+1<size;++i) {
    unsigned char c=(unsigned char)src[i];
    dst[i]=(c<0x20)?' ':(c=='"'||c=='\\')?'_':(char)c;
  }
  dst[i]='\0';
}
static void release_values(void) {
  heap_caps_free(g.values);
  g.values = NULL;
  g.capacity = 0;
}
static void sample_heap(uint32_t *free_out, uint32_t *largest_out) {
  *free_out = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  *largest_out = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}
static void sort_values(uint32_t *v, uint32_t count) {
  for (uint32_t i = 1; i < count; ++i) {
    uint32_t x = v[i], j = i;
    while (j && v[j - 1] > x) { v[j] = v[j - 1]; --j; }
    v[j] = x;
  }
}
static uint32_t pct(const uint32_t *v, uint32_t count, uint32_t percent) {
  uint32_t rank = (count * percent + 99u) / 100u;
  if (!rank) rank = 1;
  if (rank > count) rank = count;
  return count ? v[rank - 1u] : 0;
}
static const char *mode_name(uint32_t mode) {
  return mode == PRG32_PERF_COLOR_INDEXED ? "indexed" :
         mode == PRG32_PERF_COLOR_RGB565 ? "rgb565" : "custom";
}
static const char *wifi_name(void) {
  prg32_wifi_mode_t m = prg32_wifi_current_mode();
  return m == PRG32_WIFI_MODE_STA ? "infrastructure" :
         m == PRG32_WIFI_MODE_AP ? "access_point" :
         m == PRG32_WIFI_MODE_APSTA ? "ap_infrastructure" : "off";
}

uint64_t prg32_perf_now_us(void) { return (uint64_t)esp_timer_get_time(); }

int prg32_perf_begin(const prg32_perf_suite_desc_t *suite) {
  if (!suite || suite->abi_version != PRG32_PERF_ABI_VERSION ||
      suite->struct_size < sizeof(*suite) || !suite->name ||
      g.state == PRG32_PERF_STATE_RUNNING) return -1;
  release_values();
  uint32_t sequence = g.sequence + 1u;
  memset(&g, 0, sizeof(g));
  g.sequence = sequence; g.active = -1; g.state = PRG32_PERF_STATE_RUNNING;
  g.suite_version = suite->suite_version;
  text_copy(g.name, sizeof(g.name), suite->name);
  g.started = prg32_perf_now_us();
  sample_heap(&g.baseline_free, &g.baseline_largest);
  g.peak_free = g.baseline_free; g.peak_largest = g.baseline_largest;
  return 0;
}

int prg32_perf_case_begin(const prg32_perf_case_desc_t *desc,
                          uint32_t expected_samples) {
  if (!desc || desc->abi_version != PRG32_PERF_ABI_VERSION ||
      desc->struct_size < sizeof(*desc) || !desc->name || !expected_samples ||
      g.state != PRG32_PERF_STATE_RUNNING || g.active >= 0 ||
      g.count >= PERF_CASES) return -1;
  g.values = heap_caps_calloc(expected_samples, sizeof(uint32_t), MALLOC_CAP_8BIT);
  if (!g.values) return -2;
  g.capacity = expected_samples;
  perf_result_t *r = &g.results[g.count];
  memset(r, 0, sizeof(*r)); r->index = desc->case_index;
  r->mode = desc->color_mode; r->heap_min = UINT32_MAX;
  text_copy(r->name, sizeof(r->name), desc->name);
  text_copy(r->goal, sizeof(r->goal), desc->metric_goal);
  g.active = (int32_t)g.count;
  g.update_total = g.draw_total = g.present_total = g.frame_total = 0;
  uint32_t f, l; sample_heap(&f, &l);
  if (f < g.peak_free) g.peak_free = f;
  if (l < g.peak_largest) g.peak_largest = l;
  return 0;
}

int prg32_perf_record(const prg32_perf_sample_t *sample) {
  if (!sample || sample->abi_version != PRG32_PERF_ABI_VERSION ||
      sample->struct_size < sizeof(*sample) || g.active < 0) return -1;
  perf_result_t *r = &g.results[g.active];
  if (r->frames >= g.capacity) return -2;
  uint32_t total = sample->frame_total_us ? sample->frame_total_us :
      sample->update_us + sample->draw_us + sample->present_us;
  g.values[r->frames++] = total;
  g.update_total += sample->update_us; g.draw_total += sample->draw_us;
  g.present_total += sample->present_us; g.frame_total += total;
  if (total > PERF_BUDGET_US) ++r->missed;
  uint32_t f, l; sample_heap(&f, &l);
  if (f < r->heap_min) r->heap_min = f;
  if (f < g.peak_free) g.peak_free = f;
  if (l < g.peak_largest) g.peak_largest = l;
  return 0;
}

int prg32_perf_case_end(void) {
  if (g.state != PRG32_PERF_STATE_RUNNING || g.active < 0) return -1;
  perf_result_t *r = &g.results[g.active];
  if (!r->frames) { release_values(); g.active = -1; return -2; }
  sort_values(g.values, r->frames);
  r->min = g.values[0]; r->max = g.values[r->frames - 1u];
  r->mean = (uint32_t)(g.frame_total / r->frames);
  r->p50 = pct(g.values, r->frames, 50); r->p95 = pct(g.values, r->frames, 95);
  r->p99 = pct(g.values, r->frames, 99);
  r->fps = r->mean ? (uint32_t)(100000000ULL / r->mean) : 0;
  r->update = (uint32_t)(g.update_total / r->frames);
  r->draw = (uint32_t)(g.draw_total / r->frames);
  r->present = (uint32_t)(g.present_total / r->frames);
  if (r->heap_min == UINT32_MAX) r->heap_min = 0;
  r->first = g.summary.frames; g.summary.frames += r->frames;
  r->last = g.summary.frames - 1u;
  ++g.count; g.active = -1; release_values();
  return 0;
}

static void make_summary(void) {
  prg32_performance_summary_t *s = &g.summary;
  text_copy(s->board_id, sizeof(s->board_id), CONFIG_PRG32_METRICS_BOARD_ID);
  text_copy(s->target, sizeof(s->target), PERF_TARGET);
  text_copy(s->display_backend, sizeof(s->display_backend), PERF_DISPLAY);
  text_copy(s->game_name, sizeof(s->game_name), g.name);
  text_copy(s->wifi_mode, sizeof(s->wifi_mode), wifi_name());
  snprintf(s->run_id, sizeof(s->run_id), "perf-%llu-%lu",
           (unsigned long long)g.started, (unsigned long)g.sequence);
  s->started_at_device_us = g.started; s->duration_us = (uint32_t)(g.ended-g.started);
  s->screen_count = 0; s->frame_us_min = UINT32_MAX; s->heap_min = UINT32_MAX;
  uint64_t ft=0, ut=0, dt=0, pt=0;
  for (uint32_t i=0;i<g.count;++i) {
    perf_result_t *r=&g.results[i];
    if(r->index+1u>s->screen_count)s->screen_count=r->index+1u;
    ft+=(uint64_t)r->mean*r->frames; ut+=(uint64_t)r->update*r->frames;
    dt+=(uint64_t)r->draw*r->frames; pt+=(uint64_t)r->present*r->frames;
    s->missed_deadlines+=r->missed;
    if(r->min<s->frame_us_min)s->frame_us_min=r->min;
    if(r->max>s->frame_us_max)s->frame_us_max=r->max;
    if(r->heap_min<s->heap_min)s->heap_min=r->heap_min;
  }
  if(s->frames) {
    s->frame_us_mean=(uint32_t)(ft/s->frames);
    s->fps_mean_x100=s->frame_us_mean?(uint32_t)(100000000ULL/s->frame_us_mean):0;
    s->update_us_mean=(uint32_t)(ut/s->frames);
    s->draw_us_mean=(uint32_t)(dt/s->frames);
    s->present_us_mean=(uint32_t)(pt/s->frames);
  }
  if(s->frame_us_min==UINT32_MAX)s->frame_us_min=0;
  if(s->heap_min==UINT32_MAX)s->heap_min=0;
}

int prg32_perf_end(void) {
  if (g.state != PRG32_PERF_STATE_RUNNING || g.active >= 0) return -1;
  g.ended=prg32_perf_now_us(); sample_heap(&g.after_free,&g.after_largest);
  make_summary(); g.state=PRG32_PERF_STATE_COMPLETE; return 0;
}
int prg32_perf_abort(void) {
  if (g.state != PRG32_PERF_STATE_RUNNING) return -1;
  release_values(); g.active=-1; g.ended=prg32_perf_now_us();
  sample_heap(&g.after_free,&g.after_largest);
  g.state=PRG32_PERF_STATE_ABORTED; return 0;
}
int prg32_perf_get_state(prg32_perf_state_t *out) {
  if(!out || out->abi_version!=PRG32_PERF_ABI_VERSION ||
     out->struct_size<sizeof(*out)) return -1;
  out->state=g.state; out->case_count=g.count;
  out->active_case=g.state==PRG32_PERF_STATE_RUNNING?g.active:-1;
  out->baseline_free_bytes=g.baseline_free; out->baseline_largest_block=g.baseline_largest;
  out->peak_free_bytes=g.peak_free; out->peak_largest_block=g.peak_largest;
  out->after_free_bytes=g.after_free; out->after_largest_block=g.after_largest;
  return 0;
}
int prg32_perf_get_summary(prg32_performance_summary_t *out) {
  if(!out || g.state!=PRG32_PERF_STATE_COMPLETE)return -1;
  *out=g.summary; return 0;
}

/* Legacy firmware entry points remain linkable; only their built-in workload
 * has gone. The reference benchmark is now a portable cartridge. */
int prg32_performance_test_run(void) { return -1; }
int prg32_performance_has_results(void) { return g.state==PRG32_PERF_STATE_COMPLETE; }
int prg32_performance_summary(prg32_performance_summary_t *out) {
  return prg32_perf_get_summary(out);
}

static int writef(prg32_performance_json_writer_t writer,void *ctx,
                  const char *format,...) {
  char b[384]; va_list a; va_start(a,format);
  int n=vsnprintf(b,sizeof(b),format,a); va_end(a);
  return n<0||(size_t)n>=sizeof(b)||writer(b,ctx)!=0?-1:0;
}
int prg32_performance_json_write(prg32_performance_json_writer_t writer,void *ctx) {
  if(!writer)return -1;
  if(g.state==PRG32_PERF_STATE_RUNNING)return writer("{\"ok\":false,\"running\":true}",ctx);
  if(g.state!=PRG32_PERF_STATE_COMPLETE)return writer("{\"ok\":false,\"error\":\"no performance test results\"}",ctx);
  prg32_performance_summary_t *s=&g.summary;
  if(writef(writer,ctx,"{\"ok\":true,\"schema_version\":2,\"run_id\":\"%s\",\"board_id\":\"%s\",\"target\":\"%s\",\"display_backend\":\"%s\",\"game_name\":\"%s\",\"wifi_mode\":\"%s\",\"sample_period_frames\":0,\"screen_count\":%lu,\"result_count\":%lu,\"color_modes\":[\"rgb565\",\"indexed\"],\"started_at_device_us\":%llu,\"started_at_server_ts\":null,\"duration_us\":%lu,\"samples\":[],\"aggregate_windows\":[],\"screen_summaries\":[",s->run_id,s->board_id,s->target,s->display_backend,s->game_name,s->wifi_mode,(unsigned long)s->screen_count,(unsigned long)g.count,(unsigned long long)s->started_at_device_us,(unsigned long)s->duration_us))return -1;
  for(uint32_t i=0;i<g.count;++i){perf_result_t *r=&g.results[i];
    if(writef(writer,ctx,"%s{\"screen_index\":%lu,\"screen_name\":\"%s\",\"color_mode\":\"%s\",\"metric_goal\":\"%s\",\"first_frame\":%lu,\"last_frame\":%lu,\"frames\":%lu,\"fps_mean\":%lu.%02lu,\"frame_us_min\":%lu,\"frame_us_mean\":%lu,\"frame_us_p50\":%lu,\"frame_us_p95\":%lu,\"frame_us_p99\":%lu,\"frame_us_max\":%lu,\"missed_deadlines\":%lu,\"update_us_mean\":%lu,\"draw_us_mean\":%lu,\"present_us_mean\":%lu,\"heap_min\":%lu}",i?",":"",(unsigned long)r->index,r->name,mode_name(r->mode),r->goal,(unsigned long)r->first,(unsigned long)r->last,(unsigned long)r->frames,(unsigned long)(r->fps/100),(unsigned long)(r->fps%100),(unsigned long)r->min,(unsigned long)r->mean,(unsigned long)r->p50,(unsigned long)r->p95,(unsigned long)r->p99,(unsigned long)r->max,(unsigned long)r->missed,(unsigned long)r->update,(unsigned long)r->draw,(unsigned long)r->present,(unsigned long)r->heap_min))return -1;}
  return writef(writer,ctx,"],\"comparisons\":[],\"memory\":{\"baseline_free_bytes\":%lu,\"baseline_largest_block\":%lu,\"peak_free_bytes\":%lu,\"peak_largest_block\":%lu,\"after_free_bytes\":%lu,\"after_largest_block\":%lu},\"summary\":{\"frames\":%lu,\"fps_mean\":%lu.%02lu,\"frame_us_min\":%lu,\"frame_us_mean\":%lu,\"frame_us_max\":%lu,\"missed_deadlines\":%lu,\"update_us_mean\":%lu,\"draw_us_mean\":%lu,\"present_us_mean\":%lu,\"heap_min\":%lu,\"screen_count\":%lu}}",(unsigned long)g.baseline_free,(unsigned long)g.baseline_largest,(unsigned long)g.peak_free,(unsigned long)g.peak_largest,(unsigned long)g.after_free,(unsigned long)g.after_largest,(unsigned long)s->frames,(unsigned long)(s->fps_mean_x100/100),(unsigned long)(s->fps_mean_x100%100),(unsigned long)s->frame_us_min,(unsigned long)s->frame_us_mean,(unsigned long)s->frame_us_max,(unsigned long)s->missed_deadlines,(unsigned long)s->update_us_mean,(unsigned long)s->draw_us_mean,(unsigned long)s->present_us_mean,(unsigned long)s->heap_min,(unsigned long)s->screen_count);
}
typedef struct{char *p;size_t n,cap;} alloc_t;
static int alloc_write(const char *chunk,void *ctx){alloc_t *w=ctx;size_t n=strlen(chunk);
  if(w->n+n+1>w->cap){size_t cap=(w->n+n+1)*2;char *p=realloc(w->p,cap);if(!p)return -1;w->p=p;w->cap=cap;}
  memcpy(w->p+w->n,chunk,n+1);w->n+=n;return 0;}
char *prg32_performance_json_alloc(void){alloc_t w={0};if(prg32_performance_json_write(alloc_write,&w)){free(w.p);return NULL;}return w.p;}
void prg32_performance_json_free(char *json){free(json);}
