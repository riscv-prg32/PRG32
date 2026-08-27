#include "prg32.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

extern uint8_t _bss_start;
extern uint8_t _bss_end;
extern uint8_t _data_start;
extern uint8_t _data_end;

void prg32_memory_get_stats(prg32_memory_stats_t *stats) {
    if (!stats) return;
    
    memset(stats, 0, sizeof(*stats));

    stats->static_bss_bytes = (uint32_t)(&_bss_end - &_bss_start);
    stats->static_data_bytes = (uint32_t)(&_data_end - &_data_start);

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);

    stats->heap_total_bytes = info.total_free_bytes + info.total_allocated_bytes;
    stats->heap_free_bytes = info.total_free_bytes;
    stats->heap_allocated_bytes = info.total_allocated_bytes;
    stats->heap_largest_free_block = info.largest_free_block;
}

void prg32_memory_log_stats(void) {
    prg32_memory_stats_t stats;
    prg32_memory_get_stats(&stats);

    prg32_band_log("--- MEMORY STATS ---");
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Static BSS: %lu B", (unsigned long)stats.static_bss_bytes);
    prg32_band_log(buf);

    snprintf(buf, sizeof(buf), "Static DATA: %lu B", (unsigned long)stats.static_data_bytes);
    prg32_band_log(buf);

    snprintf(buf, sizeof(buf), "Heap Total: %lu B", (unsigned long)stats.heap_total_bytes);
    prg32_band_log(buf);

    snprintf(buf, sizeof(buf), "Heap Free: %lu B", (unsigned long)stats.heap_free_bytes);
    prg32_band_log(buf);

    snprintf(buf, sizeof(buf), "Heap Alloc: %lu B", (unsigned long)stats.heap_allocated_bytes);
    prg32_band_log(buf);

    prg32_console_write("\r\n--- MEMORY STATS ---\r\n");
    snprintf(buf, sizeof(buf), "Static BSS: %lu B\r\n", (unsigned long)stats.static_bss_bytes);
    prg32_console_write(buf);
    snprintf(buf, sizeof(buf), "Static DATA: %lu B\r\n", (unsigned long)stats.static_data_bytes);
    prg32_console_write(buf);
    snprintf(buf, sizeof(buf), "Heap Total: %lu B\r\n", (unsigned long)stats.heap_total_bytes);
    prg32_console_write(buf);
    snprintf(buf, sizeof(buf), "Heap Free: %lu B\r\n", (unsigned long)stats.heap_free_bytes);
    prg32_console_write(buf);
    snprintf(buf, sizeof(buf), "Heap Alloc: %lu B\r\n", (unsigned long)stats.heap_allocated_bytes);
    prg32_console_write(buf);
    snprintf(buf, sizeof(buf), "Heap Largest Free: %lu B\r\n", (unsigned long)stats.heap_largest_free_block);
    prg32_console_write(buf);
}
