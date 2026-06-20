#include "prg32.h"
#include "prg32_config.h"

/*
 * USB gamepad input belongs to the resident runtime, never to a cartridge.
 * This keeps the cartridge input ABI identical on C6, P4, and QEMU.
 *
 * ESP-IDF's USB HID class driver delivers reports asynchronously.  The board
 * glue calls prg32_usb_gamepad_submit_boot_report() after decoding a standard
 * HID boot/gamepad report; games only ever see prg32_input_read().
 */

#if defined(CONFIG_IDF_TARGET_ESP32P4) && CONFIG_IDF_TARGET_ESP32P4 && PRG32_USB_GAMEPAD_ENABLE
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "usb/usb_host.h"
#include "esp_log.h"

static const char *TAG = "prg32_usb_gamepad";
static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t g_state;
static int g_host_ready;

static void host_daemon(void *arg) {
    (void)arg;
    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

static void init_host_once(void) {
    if (g_host_ready) {
        return;
    }
    const usb_host_config_t config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    if (usb_host_install(&config) != ESP_OK) {
        ESP_LOGW(TAG, "USB host unavailable; GPIO input remains active");
        return;
    }
    if (xTaskCreate(host_daemon, "prg32_usb", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "cannot start USB host task");
        return;
    }
    g_host_ready = 1;
    ESP_LOGI(TAG, "USB host ready; connect a standard HID gamepad");
}

/* Called by the HID class adapter. axes use -1, 0, +1; buttons are PRG32 bits. */
void prg32_usb_gamepad_submit(int8_t x, int8_t y, uint32_t buttons) {
    uint32_t state = buttons & (PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_START);
    if (x < 0) state |= PRG32_BTN_LEFT;
    if (x > 0) state |= PRG32_BTN_RIGHT;
    if (y < 0) state |= PRG32_BTN_UP;
    if (y > 0) state |= PRG32_BTN_DOWN;
    portENTER_CRITICAL(&g_lock);
    g_state = state;
    portEXIT_CRITICAL(&g_lock);
}

uint32_t prg32_usb_gamepad_read(void) {
    init_host_once();
    portENTER_CRITICAL(&g_lock);
    uint32_t state = g_state;
    portEXIT_CRITICAL(&g_lock);
    return state;
}
#else
uint32_t prg32_usb_gamepad_read(void) { return 0; }
#endif
