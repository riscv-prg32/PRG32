#include "prg32.h"
#include "prg32_config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef PRG32_RESTART_HOTKEY_ENABLE
#define PRG32_RESTART_HOTKEY_ENABLE 1
#endif

#ifndef PRG32_PIN_BTN_SELECT
#define PRG32_PIN_BTN_SELECT PRG32_PIN_BTN_START
#endif

#ifndef PRG32_PIN_P2_SELECT
#define PRG32_PIN_P2_SELECT PRG32_PIN_P2_START
#endif

#define PRG32_RESTART_HOTKEY_P1 \
    (PRG32_BTN_A | PRG32_BTN_B | PRG32_BTN_DOWN)
#define PRG32_RESTART_HOTKEY_P2 \
    (PRG32_P2_BTN_A | PRG32_P2_BTN_B | PRG32_P2_BTN_DOWN)

static const char *TAG = "prg32_controller";

/*
 * PRG32 controller layer.
 *
 * ESP32-C6 has a USB Serial/JTAG device controller, but it is not a normal
 * USB host port for plugging in arbitrary USB HID gamepads. Therefore this
 * file supports three classroom-friendly input backends:
 *   1. GPIO buttons on the reference breadboard/PCB.
 *   2. A USB-HID-host bridge, e.g. RP2040/CH559/ESP32-S3, connected by UART.
 *   3. A host-terminal keyboard/debug mode through the same UART protocol.
 *
 * The game sees only the stable PRG32 bitmask: LEFT/RIGHT/UP/DOWN/SELECT/A/B.
 * This is intentionally similar to memory-mapped input registers on 1980s
 * consoles, which makes it a useful Computer Architecture teaching example.
 */

#if PRG32_CONTROLLER_BRIDGE_ENABLE
static uint32_t bridge_state;
static uint8_t bridge_packet[4];
static int bridge_packet_len;

static void bridge_feed(uint8_t byte) {
    /* Packet format: 'U' 'G' lo hi, where lo/hi are the PRG32 bitmask. */
    if (bridge_packet_len == 0) {
        if (byte == 'U') {
            bridge_packet[bridge_packet_len++] = byte;
        }
        return;
    }
    if (bridge_packet_len == 1) {
        if (byte == 'G') {
            bridge_packet[bridge_packet_len++] = byte;
        } else if (byte != 'U') {
            bridge_packet_len = 0;
        }
        return;
    }
    bridge_packet[bridge_packet_len++] = byte;
    if (bridge_packet_len == 4) {
        bridge_state =
            (uint32_t)bridge_packet[2] | ((uint32_t)bridge_packet[3] << 8);
        bridge_packet_len = 0;
    }
}

void prg32_controller_bridge_init(void) {
    const uart_config_t cfg = {
        .baud_rate = PRG32_CONTROLLER_BRIDGE_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err =
        uart_driver_install(PRG32_CONTROLLER_BRIDGE_UART, 256, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG,
                 "controller bridge UART driver install failed: %s",
                 esp_err_to_name(err));
        return;
    }
    err = uart_param_config(PRG32_CONTROLLER_BRIDGE_UART, &cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "controller bridge UART config failed: %s",
                 esp_err_to_name(err));
        return;
    }
    err = uart_set_pin(PRG32_CONTROLLER_BRIDGE_UART,
                       PRG32_PIN_CONTROLLER_TX,
                       PRG32_PIN_CONTROLLER_RX,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "controller bridge UART pin setup failed: %s",
                 esp_err_to_name(err));
        return;
    }
}

static uint32_t read_bridge(void) {
    uint8_t b[16];
    int n = uart_read_bytes(PRG32_CONTROLLER_BRIDGE_UART, b, sizeof(b), 0);
    for (int i = 0; i < n; ++i) {
        bridge_feed(b[i]);
    }
    return bridge_state;
}
#else
void prg32_controller_bridge_init(void) {}
static uint32_t read_bridge(void) { return 0; }
#endif

static uint32_t read_gpio_buttons(void) {
    uint32_t v = 0;
    if (PRG32_PIN_BTN_LEFT >= 0 && !gpio_get_level(PRG32_PIN_BTN_LEFT)) {
        v |= PRG32_BTN_LEFT;
    }
    if (PRG32_PIN_BTN_RIGHT >= 0 && !gpio_get_level(PRG32_PIN_BTN_RIGHT)) {
        v |= PRG32_BTN_RIGHT;
    }
    if (PRG32_PIN_BTN_UP >= 0 && !gpio_get_level(PRG32_PIN_BTN_UP)) {
        v |= PRG32_BTN_UP;
    }
    if (PRG32_PIN_BTN_DOWN >= 0 && !gpio_get_level(PRG32_PIN_BTN_DOWN)) {
        v |= PRG32_BTN_DOWN;
    }
    if (PRG32_PIN_BTN_A >= 0 && !gpio_get_level(PRG32_PIN_BTN_A)) {
        v |= PRG32_BTN_A;
    }
    if (PRG32_PIN_BTN_B >= 0 && !gpio_get_level(PRG32_PIN_BTN_B)) {
        v |= PRG32_BTN_B;
    }
    if (PRG32_PIN_BTN_START >= 0 && !gpio_get_level(PRG32_PIN_BTN_START)) {
        v |= PRG32_BTN_SELECT;
    }
    if (PRG32_PIN_BTN_SELECT >= 0 && !gpio_get_level(PRG32_PIN_BTN_SELECT)) {
        v |= PRG32_BTN_SELECT;
    }
    if (PRG32_PIN_P2_LEFT >= 0 && !gpio_get_level(PRG32_PIN_P2_LEFT)) {
        v |= PRG32_P2_BTN_LEFT;
    }
    if (PRG32_PIN_P2_RIGHT >= 0 && !gpio_get_level(PRG32_PIN_P2_RIGHT)) {
        v |= PRG32_P2_BTN_RIGHT;
    }
    if (PRG32_PIN_P2_UP >= 0 && !gpio_get_level(PRG32_PIN_P2_UP)) {
        v |= PRG32_P2_BTN_UP;
    }
    if (PRG32_PIN_P2_DOWN >= 0 && !gpio_get_level(PRG32_PIN_P2_DOWN)) {
        v |= PRG32_P2_BTN_DOWN;
    }
    if (PRG32_PIN_P2_A >= 0 && !gpio_get_level(PRG32_PIN_P2_A)) {
        v |= PRG32_P2_BTN_A;
    }
    if (PRG32_PIN_P2_B >= 0 && !gpio_get_level(PRG32_PIN_P2_B)) {
        v |= PRG32_P2_BTN_B;
    }
    if (PRG32_PIN_P2_START >= 0 && !gpio_get_level(PRG32_PIN_P2_START)) {
        v |= PRG32_P2_BTN_SELECT;
    }
    if (PRG32_PIN_P2_SELECT >= 0 && !gpio_get_level(PRG32_PIN_P2_SELECT)) {
        v |= PRG32_P2_BTN_SELECT;
    }
    return v;
}

uint32_t prg32_controller_read(void) {
    uint32_t v = read_gpio_buttons();
    v |= read_bridge();
    v |= prg32_diag_input_state();
#if PRG32_RESTART_HOTKEY_ENABLE
    if ((v & PRG32_RESTART_HOTKEY_P1) == PRG32_RESTART_HOTKEY_P1 ||
        (v & PRG32_RESTART_HOTKEY_P2) == PRG32_RESTART_HOTKEY_P2) {
        ESP_LOGE(TAG, "ABOUT TO esp_restart() from %s:%d", __FILE__, __LINE__);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
#endif
    return v;
}

const char *prg32_controller_name(uint32_t bit) {
    switch (bit) {
        case PRG32_BTN_LEFT: return "LEFT";
        case PRG32_BTN_RIGHT: return "RIGHT";
        case PRG32_BTN_UP: return "UP";
        case PRG32_BTN_DOWN: return "DOWN";
        case PRG32_BTN_A: return "A";
        case PRG32_BTN_B: return "B / BACK";
        case PRG32_BTN_START: return "SELECT";
        case PRG32_P2_BTN_LEFT: return "P2 LEFT";
        case PRG32_P2_BTN_RIGHT: return "P2 RIGHT";
        case PRG32_P2_BTN_UP: return "P2 UP";
        case PRG32_P2_BTN_DOWN: return "P2 DOWN";
        case PRG32_P2_BTN_A: return "P2 A";
        case PRG32_P2_BTN_B: return "P2 B";
        case PRG32_P2_BTN_START: return "P2 SELECT";
        default: return "UNKNOWN";
    }
}
