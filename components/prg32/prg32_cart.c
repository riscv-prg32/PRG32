#include "prg32.h"
#include "prg32_config.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define PRG32_CART_HEADER_MIN_SIZE ((size_t)sizeof(prg32_cart_header_t))

typedef void (*prg32_cart_entry_t)(void);

static const char *TAG = "prg32_cart";

uint8_t prg32_cart_exec[PRG32_CART_RAM_SIZE] IRAM_ATTR __attribute__((aligned(16)));

static const char *const g_cart_labels[PRG32_CART_SLOT_COUNT] = {
    "cart0",
    "cart1",
};
static const uint8_t g_cart_subtypes[PRG32_CART_SLOT_COUNT] = {
    0x40,
    0x41,
};

static const esp_partition_t *g_cart_partitions[PRG32_CART_SLOT_COUNT];
static SemaphoreHandle_t g_cart_lock;
static prg32_cart_header_t g_header;
static prg32_cart_entry_t g_init;
static prg32_cart_entry_t g_update;
static prg32_cart_entry_t g_draw;
static uint32_t g_audio_size;
static uint32_t g_generation;
static uint8_t g_current_slot;
static bool g_loaded;
static bool g_stored;
static char g_error[96] = "no cartridge loaded";

static esp_err_t cart_nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void set_error(const char *msg) {
    if (!msg) {
        msg = "unknown cartridge error";
    }
    snprintf(g_error, sizeof(g_error), "%s", msg);
#if PRG32_DEBUG
    ESP_LOGW(TAG, "%s", g_error);
#endif
}

static void set_errorf(const char *fmt, ...) {
    if (!fmt) {
        set_error("unknown cartridge error");
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error, sizeof(g_error), fmt, ap);
    va_end(ap);
#if PRG32_DEBUG
    ESP_LOGW(TAG, "%s", g_error);
#endif
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static int lock_cart(void) {
    if (!g_cart_lock) {
        return 0;
    }
    return xSemaphoreTake(g_cart_lock, portMAX_DELAY) == pdTRUE ? 0 : -1;
}

static void unlock_cart(void) {
    if (g_cart_lock) {
        xSemaphoreGive(g_cart_lock);
    }
}

static const char *slot_name(uint8_t slot) {
    if (slot >= PRG32_CART_SLOT_COUNT) {
        return "cart?";
    }
    return g_cart_labels[slot];
}

static const esp_partition_t *cart_partition_by_slot(uint8_t slot) {
    if (slot >= PRG32_CART_SLOT_COUNT) {
        return NULL;
    }
    if (!g_cart_partitions[slot]) {
        g_cart_partitions[slot] = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            (esp_partition_subtype_t)g_cart_subtypes[slot],
            g_cart_labels[slot]);
    }
    return g_cart_partitions[slot];
}

static const esp_partition_t *cart_partition(void) {
    return cart_partition_by_slot(g_current_slot);
}

static int read_stored_header(uint8_t slot,
                              prg32_cart_header_t *header,
                              size_t *image_size,
                              uint32_t *audio_size) {
    const esp_partition_t *part = cart_partition_by_slot(slot);
    if (!part || !header) {
        return -1;
    }
    esp_err_t err = esp_partition_read(part, 0, header, sizeof(*header));
    if (err != ESP_OK ||
        memcmp(header->magic, PRG32_CART_MAGIC, sizeof(header->magic)) != 0 ||
        header->header_size < PRG32_CART_HEADER_MIN_SIZE ||
        header->code_size == 0 ||
        header->header_size + header->code_size > part->size) {
        return -1;
    }

    size_t total = header->header_size + header->code_size;
    uint32_t audio = 0;
    if ((header->flags & PRG32_CART_FLAG_AUDIO_BLOCK) != 0u) {
        prg32_audio_block_header_t block;
        if (total + sizeof(block) > part->size ||
            esp_partition_read(part, total, &block, sizeof(block)) != ESP_OK ||
            memcmp(block.magic, PRG32_AUDIO_BLOCK_MAGIC, 4) != 0 ||
            block.block_size < sizeof(block) ||
            total + block.block_size > part->size) {
            return -1;
        }
        audio = block.block_size;
        total += block.block_size;
    }
    if (image_size) {
        *image_size = total;
    }
    if (audio_size) {
        *audio_size = audio;
    }
    return 0;
}

static int validate_header(const prg32_cart_header_t *h,
                           size_t image_size,
                           const uint8_t **payload,
                           const uint8_t **audio_block,
                           size_t *audio_size) {
    if (!h || !payload || !audio_block || !audio_size) {
        set_error("missing cartridge image");
        return -1;
    }
    *audio_block = NULL;
    *audio_size = 0;
    if (image_size < PRG32_CART_HEADER_MIN_SIZE) {
        set_error("cartridge image is too small");
        return -1;
    }
    if (memcmp(h->magic, PRG32_CART_MAGIC, sizeof(h->magic)) != 0) {
        set_error("bad cartridge magic");
        return -1;
    }
    if (h->abi_major != PRG32_CART_ABI_MAJOR) {
        set_errorf("unsupported cartridge ABI major=%u expected=%u",
                   (unsigned)h->abi_major,
                   (unsigned)PRG32_CART_ABI_MAJOR);
        return -1;
    }
    if (h->header_size < PRG32_CART_HEADER_MIN_SIZE ||
        h->header_size > image_size) {
        set_error("invalid cartridge header size");
        return -1;
    }
    if (h->load_addr != (uint32_t)(uintptr_t)prg32_cart_exec) {
        set_error("cartridge linked for a different runtime address");
        return -1;
    }
    if (h->code_size == 0 || h->code_size > h->mem_size ||
        h->mem_size > PRG32_CART_RAM_SIZE) {
        set_errorf("invalid cartridge size code=%lu mem=%lu ram=%lu",
                   (unsigned long)h->code_size,
                   (unsigned long)h->mem_size,
                   (unsigned long)PRG32_CART_RAM_SIZE);
        return -1;
    }
    if ((size_t)h->header_size + h->code_size > image_size) {
        set_errorf("truncated cartridge payload image=%lu needed=%lu",
                   (unsigned long)image_size,
                   (unsigned long)((size_t)h->header_size + h->code_size));
        return -1;
    }
    if (h->init_offset >= h->code_size ||
        h->update_offset >= h->code_size ||
        h->draw_offset >= h->code_size) {
        set_errorf("entry offset outside code init=%lu update=%lu draw=%lu code=%lu",
                   (unsigned long)h->init_offset,
                   (unsigned long)h->update_offset,
                   (unsigned long)h->draw_offset,
                   (unsigned long)h->code_size);
        return -1;
    }
    if ((h->init_offset & 1u) != 0u ||
        (h->update_offset & 1u) != 0u ||
        (h->draw_offset & 1u) != 0u) {
        set_error("entry offsets must be 2-byte aligned");
        return -1;
    }
    *payload = ((const uint8_t *)h) + h->header_size;
    uint32_t crc = crc32_update(0, *payload, h->code_size);
    if (crc != h->payload_crc32) {
        set_errorf("cartridge payload CRC mismatch expected=0x%08lx got=0x%08lx",
                   (unsigned long)h->payload_crc32,
                   (unsigned long)crc);
        return -1;
    }
    if ((h->flags & PRG32_CART_FLAG_AUDIO_BLOCK) != 0u) {
        size_t audio_offset = (size_t)h->header_size + h->code_size;
        if (image_size < audio_offset + sizeof(prg32_audio_block_header_t)) {
            set_error("cartridge AUDIO block is truncated");
            return -1;
        }
        const prg32_audio_block_header_t *audio =
            (const prg32_audio_block_header_t *)(((const uint8_t *)h) + audio_offset);
        if (memcmp(audio->magic, PRG32_AUDIO_BLOCK_MAGIC, 4) != 0 ||
            audio->block_size < sizeof(*audio) ||
            audio_offset + audio->block_size > image_size) {
            set_error("cartridge AUDIO block header is invalid");
            return -1;
        }
        *audio_block = (const uint8_t *)audio;
        *audio_size = audio->block_size;
    }
    return 0;
}

static int load_image_locked(const void *image, size_t image_size) {
    const prg32_cart_header_t *h = (const prg32_cart_header_t *)image;
    const uint8_t *payload = NULL;
    const uint8_t *audio_block = NULL;
    size_t audio_size = 0;
    if (validate_header(h, image_size, &payload, &audio_block, &audio_size) != 0) {
        return -1;
    }

    g_loaded = false;
    memset(prg32_cart_exec, 0, sizeof(prg32_cart_exec));
    memcpy(prg32_cart_exec, payload, h->code_size);
    __asm__ volatile("fence.i" ::: "memory");

    memcpy(&g_header, h, sizeof(g_header));
    if (audio_block && audio_size) {
        if (prg32_audio_load_block(audio_block, audio_size) != 0) {
            g_loaded = false;
            set_error("failed to load cartridge AUDIO block");
            return -1;
        }
        g_audio_size = (uint32_t)audio_size;
    } else {
        prg32_audio_clear_assets();
        g_audio_size = 0;
    }
    g_init = (prg32_cart_entry_t)(void *)(prg32_cart_exec + h->init_offset);
    g_update = (prg32_cart_entry_t)(void *)(prg32_cart_exec + h->update_offset);
    g_draw = (prg32_cart_entry_t)(void *)(prg32_cart_exec + h->draw_offset);
    g_loaded = true;
    g_generation++;
    set_error("ok");
    ESP_LOGI(TAG,
             "loaded cartridge '%s' (%lu bytes code, %lu bytes memory, %lu bytes audio)",
             g_header.name,
             (unsigned long)g_header.code_size,
             (unsigned long)g_header.mem_size,
             (unsigned long)g_audio_size);
    return 0;
}

void prg32_cart_init(void) {
    if (!g_cart_lock) {
        g_cart_lock = xSemaphoreCreateMutex();
    }
    bool any_partition = false;
    for (uint8_t slot = 0; slot < PRG32_CART_SLOT_COUNT; ++slot) {
        if (cart_partition_by_slot(slot)) {
            any_partition = true;
        }
    }
    if (!any_partition) {
        set_error("cartridge partitions not found");
        ESP_LOGW(TAG, "%s", g_error);
        return;
    }
    g_loaded = false;
    g_stored = false;
    set_error("cartridge runtime ready");
}

uintptr_t prg32_cart_load_addr(void) {
    return (uintptr_t)prg32_cart_exec;
}

size_t prg32_cart_ram_size(void) {
    return sizeof(prg32_cart_exec);
}

uint32_t prg32_cart_generation(void) {
    return g_generation;
}

int prg32_cart_is_loaded(void) {
    return g_loaded ? 1 : 0;
}

int prg32_cart_load_stored(void) {
    const esp_partition_t *part = cart_partition();
    if (!part) {
        set_errorf("%s partition not found", slot_name(g_current_slot));
        return -1;
    }

    prg32_cart_header_t header;
    size_t image_size = 0;
    if (read_stored_header(g_current_slot, &header, &image_size, NULL) != 0) {
        g_loaded = false;
        g_stored = false;
        set_errorf("no stored cartridge in %s", slot_name(g_current_slot));
        return -1;
    }
    uint8_t *image = malloc(image_size);
    if (!image) {
        set_error("out of memory reading cartridge");
        return -1;
    }
    esp_err_t err = esp_partition_read(part, 0, image, image_size);
    if (err != ESP_OK) {
        free(image);
        set_error("failed to read stored cartridge");
        return -1;
    }

    if (lock_cart() != 0) {
        free(image);
        set_error("failed to lock cartridge runtime");
        return -1;
    }
    int rc = load_image_locked(image, image_size);
    if (rc == 0) {
        g_stored = true;
    }
    unlock_cart();
    free(image);
    return rc;
}

int prg32_cart_install(const void *image, size_t image_size, int persist) {
    if (!image || image_size == 0) {
        set_error("missing cartridge image");
        return -1;
    }
    if (lock_cart() != 0) {
        set_error("failed to lock cartridge runtime");
        return -1;
    }
    int rc = load_image_locked(image, image_size);
    unlock_cart();
    if (rc != 0) {
        return rc;
    }

    if (persist) {
        g_stored = false;
        const esp_partition_t *part = cart_partition();
        if (!part) {
            set_errorf("%s partition not found", slot_name(g_current_slot));
            return -1;
        }
        if (image_size > part->size) {
            set_errorf("cartridge is larger than %s partition",
                       slot_name(g_current_slot));
            return -1;
        }
        esp_err_t err = esp_partition_erase_range(part, 0, part->size);
        if (err == ESP_OK) {
            err = esp_partition_write(part, 0, image, image_size);
        }
        if (err != ESP_OK) {
            set_error("failed to persist cartridge");
            return -1;
        }
        g_stored = true;
    }
    return 0;
}

int prg32_cart_store_slot(uint8_t slot, const void *image, size_t image_size) {
    if (slot >= PRG32_CART_SLOT_COUNT) {
        set_error("invalid cartridge slot");
        return -1;
    }
    if (!image || image_size < sizeof(prg32_cart_header_t)) {
        set_error("missing cartridge image");
        return -1;
    }

    ESP_LOGI(TAG,
             "cart store: validating slot=%s image size=%u",
             slot_name(slot),
             (unsigned)image_size);

    const prg32_cart_header_t *h = (const prg32_cart_header_t *)image;
    const uint8_t *payload = NULL;
    const uint8_t *audio_block = NULL;
    size_t audio_size = 0;
    if (validate_header(h, image_size, &payload, &audio_block, &audio_size) != 0) {
        return -1;
    }

    const esp_partition_t *part = cart_partition_by_slot(slot);
    if (!part) {
        set_errorf("%s partition not found", slot_name(slot));
        return -1;
    }
    if (image_size > part->size) {
        set_errorf("cartridge is larger than %s partition", slot_name(slot));
        return -1;
    }

    if (lock_cart() != 0) {
        set_error("failed to lock cartridge storage");
        return -1;
    }

    ESP_LOGI(TAG,
             "cart store: erase %s size=%u",
             slot_name(slot),
             (unsigned)part->size);
    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "cart store: write %u bytes", (unsigned)image_size);
        err = esp_partition_write(part, 0, image, image_size);
    }
    if (err == ESP_OK) {
        prg32_cart_header_t stored_header;
        if (read_stored_header(slot, &stored_header, NULL, NULL) != 0) {
            err = ESP_ERR_INVALID_STATE;
        }
    }

    unlock_cart();

    if (err != ESP_OK) {
        set_errorf("failed to persist cartridge: %s", esp_err_to_name(err));
        return -1;
    }

    set_error("ok");
    ESP_LOGI(TAG, "cart store: done");
    return 0;
}

int prg32_cart_install_slot(uint8_t slot,
                            const void *image,
                            size_t image_size,
                            int persist) {
    if (slot >= PRG32_CART_SLOT_COUNT) {
        set_error("invalid cartridge slot");
        return -1;
    }
    uint8_t previous_slot = g_current_slot;
    uint32_t previous_generation = g_generation;
    g_current_slot = slot;
    int rc = prg32_cart_install(image, image_size, persist);
    if (rc != 0 && g_generation == previous_generation) {
        g_current_slot = previous_slot;
    }
    return rc;
}

int prg32_cart_select_stored(void) {
    for (uint8_t slot = 0; slot < PRG32_CART_SLOT_COUNT; ++slot) {
        prg32_cart_header_t header;
        if (read_stored_header(slot, &header, NULL, NULL) == 0) {
            return prg32_cart_select_slot(slot);
        }
    }
    g_loaded = false;
    g_stored = false;
    set_error("no stored cartridge");
    return -1;
}

int prg32_cart_select_slot(uint8_t slot) {
    if (slot >= PRG32_CART_SLOT_COUNT) {
        set_error("invalid cartridge slot");
        return -1;
    }
    g_current_slot = slot;
    return prg32_cart_load_stored();
}

int prg32_cart_default_slot(void) {
    if (cart_nvs_init() != ESP_OK) {
        return -1;
    }

    nvs_handle_t nvs;
    if (nvs_open("prg32cart", NVS_READONLY, &nvs) != ESP_OK) {
        return -1;
    }
    uint8_t slot = 0xff;
    esp_err_t err = nvs_get_u8(nvs, "default", &slot);
    nvs_close(nvs);
    if (err != ESP_OK || slot >= PRG32_CART_SLOT_COUNT) {
        return -1;
    }

    prg32_cart_header_t header;
    if (read_stored_header(slot, &header, NULL, NULL) != 0) {
        return -1;
    }
    return slot;
}

int prg32_cart_set_default_slot(int slot) {
    if (cart_nvs_init() != ESP_OK) {
        set_error("failed to initialize cartridge settings");
        return -1;
    }
    if (slot >= PRG32_CART_SLOT_COUNT) {
        set_error("invalid default cartridge slot");
        return -1;
    }
    if (slot >= 0) {
        prg32_cart_header_t header;
        if (read_stored_header((uint8_t)slot, &header, NULL, NULL) != 0) {
            set_error("default cartridge slot is empty");
            return -1;
        }
    }

    nvs_handle_t nvs;
    if (nvs_open("prg32cart", NVS_READWRITE, &nvs) != ESP_OK) {
        set_error("failed to open cartridge settings");
        return -1;
    }
    esp_err_t err = slot < 0
        ? nvs_erase_key(nvs, "default")
        : nvs_set_u8(nvs, "default", (uint8_t)slot);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        set_error("failed to save default cartridge");
        return -1;
    }
    set_error("ok");
    return 0;
}

int prg32_cart_select_default(void) {
    int slot = prg32_cart_default_slot();
    if (slot < 0) {
        set_error("no default cartridge");
        return -1;
    }
    return prg32_cart_select_slot((uint8_t)slot);
}

int prg32_cart_stored_count(void) {
    int count = 0;
    for (uint8_t slot = 0; slot < PRG32_CART_SLOT_COUNT; ++slot) {
        prg32_cart_header_t header;
        if (read_stored_header(slot, &header, NULL, NULL) == 0) {
            count++;
        }
    }
    return count;
}

int prg32_cart_get_slot_info(uint8_t slot, prg32_cart_info_t *info) {
    if (!info || slot >= PRG32_CART_SLOT_COUNT) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    snprintf(info->slot_name, sizeof(info->slot_name), "%s", slot_name(slot));
    info->slot = slot;
    info->load_addr = (uint32_t)(uintptr_t)prg32_cart_exec;

    if (lock_cart() != 0) {
        return -1;
    }
    prg32_cart_header_t header;
    uint32_t audio_size = 0;
    if (read_stored_header(slot, &header, NULL, &audio_size) == 0) {
        snprintf(info->name, sizeof(info->name), "%s", header.name);
        info->code_size = header.code_size;
        info->mem_size = header.mem_size;
        info->audio_size = audio_size;
        info->stored = 1;
        info->audio = audio_size > 0 ? 1 : 0;
    }
    info->loaded = (g_loaded && g_current_slot == slot) ? 1 : 0;
    info->generation = g_generation;
    unlock_cart();
    return 0;
}

int prg32_cart_get_info(prg32_cart_info_t *info) {
    if (!info) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    if (lock_cart() != 0) {
        return -1;
    }
    snprintf(info->slot_name, sizeof(info->slot_name), "%s", slot_name(g_current_slot));
    info->slot = g_current_slot;
    if (g_loaded) {
        snprintf(info->name, sizeof(info->name), "%s", g_header.name);
        info->code_size = g_header.code_size;
        info->mem_size = g_header.mem_size;
        info->audio_size = g_audio_size;
        info->audio = g_audio_size > 0 ? 1 : 0;
    }
    info->load_addr = (uint32_t)(uintptr_t)prg32_cart_exec;
    info->generation = g_generation;
    info->loaded = g_loaded ? 1 : 0;
    info->stored = g_stored ? 1 : 0;
    unlock_cart();
    return 0;
}

static int call_entry(prg32_cart_entry_t entry) {
    if (!entry || !g_loaded) {
        return -1;
    }
    if (lock_cart() != 0) {
        return -1;
    }
    if (!g_loaded) {
        unlock_cart();
        return -1;
    }
    uintptr_t base = (uintptr_t)prg32_cart_exec;
    uintptr_t addr = (uintptr_t)entry;
    uintptr_t end = base + g_header.code_size;
    if (addr < base || addr >= end || ((addr - base) & 1u) != 0u) {
        set_error("entry pointer is outside cartridge code range");
        unlock_cart();
        return -1;
    }
    entry();
    unlock_cart();
    return 0;
}

int prg32_cart_call_init(void) {
    return call_entry(g_init);
}

int prg32_cart_call_update(void) {
    return call_entry(g_update);
}

int prg32_cart_call_draw(void) {
    return call_entry(g_draw);
}

const char *prg32_cart_last_error(void) {
    return g_error;
}
