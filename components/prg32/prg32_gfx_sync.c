#include "prg32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static StaticSemaphore_t g_gfx_lock_storage;
static SemaphoreHandle_t g_gfx_lock;
static TaskHandle_t g_gfx_owner;
static uint32_t g_gfx_depth;

void prg32_gfx_lock_init(void) {
    if (!g_gfx_lock) {
        g_gfx_lock = xSemaphoreCreateMutexStatic(&g_gfx_lock_storage);
    }
}

void prg32_gfx_lock(void) {
    prg32_gfx_lock_init();
    if (g_gfx_lock) {
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        if (g_gfx_owner == self) {
            g_gfx_depth++;
            return;
        }
        xSemaphoreTake(g_gfx_lock, portMAX_DELAY);
        g_gfx_owner = self;
        g_gfx_depth = 1;
    }
}

int prg32_gfx_try_lock(uint32_t timeout_ms) {
    prg32_gfx_lock_init();
    if (!g_gfx_lock) {
        return -1;
    }
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (g_gfx_owner == self) {
        g_gfx_depth++;
        return 0;
    }
    if (xSemaphoreTake(g_gfx_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return -1;
    }
    g_gfx_owner = self;
    g_gfx_depth = 1;
    return 0;
}

void prg32_gfx_unlock(void) {
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (g_gfx_lock && g_gfx_owner == self) {
        if (g_gfx_depth > 1) {
            g_gfx_depth--;
            return;
        }
        g_gfx_owner = NULL;
        g_gfx_depth = 0;
        xSemaphoreGive(g_gfx_lock);
    }
}
