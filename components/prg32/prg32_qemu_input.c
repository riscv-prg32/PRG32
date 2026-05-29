#include "prg32.h"
#include "prg32_config.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

#ifndef CONFIG_PRG32_QEMU_UART_INPUT
#define CONFIG_PRG32_QEMU_UART_INPUT 0
#endif

#ifndef CONFIG_ESP_CONSOLE_UART
#define CONFIG_ESP_CONSOLE_UART 0
#endif

#if CONFIG_PRG32_QEMU_UART_INPUT && CONFIG_ESP_CONSOLE_UART

#define PRG32_QEMU_KEY_HOLD_MS 120u
#define PRG32_QEMU_ESC_TIMEOUT_MS 40u
#define PRG32_QEMU_KEY_SLOT_COUNT \
    (sizeof(qemu_key_slots) / sizeof(qemu_key_slots[0]))

static const char *TAG = "prg32_qemu_input";

typedef struct {
    uint32_t bit;
    uint32_t until_ms;
} qemu_key_slot_t;

static qemu_key_slot_t qemu_key_slots[] = {
    {PRG32_BTN_LEFT, 0},
    {PRG32_BTN_RIGHT, 0},
    {PRG32_BTN_UP, 0},
    {PRG32_BTN_DOWN, 0},
    {PRG32_BTN_SELECT, 0},
    {PRG32_BTN_A, 0},
    {PRG32_BTN_B, 0},
};

static int qemu_uart_ready;
static int qemu_uart_unavailable;
static int qemu_esc_state;
static uint32_t qemu_esc_deadline_ms;

static int time_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void qemu_press(uint32_t bit, uint32_t now_ms) {
    for (size_t i = 0; i < PRG32_QEMU_KEY_SLOT_COUNT; ++i) {
        if (qemu_key_slots[i].bit == bit) {
            qemu_key_slots[i].until_ms = now_ms + PRG32_QEMU_KEY_HOLD_MS;
            return;
        }
    }
}

static uint32_t qemu_keys_active(uint32_t now_ms) {
    uint32_t state = 0;
    for (size_t i = 0; i < PRG32_QEMU_KEY_SLOT_COUNT; ++i) {
        if (qemu_key_slots[i].until_ms != 0 &&
            !time_reached(now_ms, qemu_key_slots[i].until_ms)) {
            state |= qemu_key_slots[i].bit;
        } else {
            qemu_key_slots[i].until_ms = 0;
        }
    }
    return state;
}

static int qemu_uart_init(void) {
    if (qemu_uart_ready) {
        return 1;
    }
    if (qemu_uart_unavailable) {
        return 0;
    }

    const uart_port_t uart_num = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    if (!uart_is_driver_installed(uart_num)) {
        const uart_config_t cfg = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        esp_err_t err = uart_param_config(uart_num, &cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "QEMU keyboard config failed: %s", esp_err_to_name(err));
            qemu_uart_unavailable = 1;
            return 0;
        }
        err = uart_driver_install(uart_num, 256, 0, 0, NULL, 0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "QEMU keyboard disabled: %s", esp_err_to_name(err));
            qemu_uart_unavailable = 1;
            return 0;
        }
    }

    qemu_uart_ready = 1;
    ESP_LOGI(TAG, "QEMU keyboard input enabled");
    return 1;
}

static void qemu_clear_escape(uint32_t now_ms) {
    if (qemu_esc_state == 1 && time_reached(now_ms, qemu_esc_deadline_ms)) {
        qemu_press(PRG32_BTN_B, now_ms);
        qemu_esc_state = 0;
    } else if (qemu_esc_state > 1 && time_reached(now_ms, qemu_esc_deadline_ms)) {
        qemu_esc_state = 0;
    }
}

static void qemu_process_key(uint8_t byte, uint32_t now_ms) {
    if (qemu_esc_state == 1) {
        if (byte == '[') {
            qemu_esc_state = 2;
            qemu_esc_deadline_ms = now_ms + PRG32_QEMU_ESC_TIMEOUT_MS;
            return;
        }
        if (byte == 'O') {
            qemu_esc_state = 3;
            qemu_esc_deadline_ms = now_ms + PRG32_QEMU_ESC_TIMEOUT_MS;
            return;
        }
        qemu_press(PRG32_BTN_B, now_ms);
        qemu_esc_state = 0;
    }

    if (qemu_esc_state == 2 || qemu_esc_state == 3) {
        switch (byte) {
            case 'A': qemu_press(PRG32_BTN_UP, now_ms); break;
            case 'B': qemu_press(PRG32_BTN_DOWN, now_ms); break;
            case 'C': qemu_press(PRG32_BTN_RIGHT, now_ms); break;
            case 'D': qemu_press(PRG32_BTN_LEFT, now_ms); break;
            default: break;
        }
        qemu_esc_state = 0;
        return;
    }

    switch (byte) {
        case 0x1b:
            qemu_esc_state = 1;
            qemu_esc_deadline_ms = now_ms + PRG32_QEMU_ESC_TIMEOUT_MS;
            break;
        case '\r':
        case '\n':
        case ' ':
            qemu_press(PRG32_BTN_SELECT, now_ms);
            break;
        case 'w':
        case 'W':
            qemu_press(PRG32_BTN_UP, now_ms);
            break;
        case 's':
        case 'S':
            qemu_press(PRG32_BTN_DOWN, now_ms);
            break;
        case 'a':
        case 'A':
            qemu_press(PRG32_BTN_LEFT, now_ms);
            break;
        case 'd':
        case 'D':
            qemu_press(PRG32_BTN_RIGHT, now_ms);
            break;
        case 'j':
        case 'J':
        case 'z':
        case 'Z':
            qemu_press(PRG32_BTN_A, now_ms);
            break;
        case 'k':
        case 'K':
        case 'x':
        case 'X':
        case 0x7f:
        case '\b':
            qemu_press(PRG32_BTN_B, now_ms);
            break;
        default:
            break;
    }
}

uint32_t prg32_qemu_input_read(void) {
    uint32_t now_ms = prg32_ticks_ms();
    qemu_clear_escape(now_ms);

    if (!qemu_uart_init()) {
        return qemu_keys_active(now_ms);
    }

    uint8_t bytes[16];
    int n = uart_read_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
                            bytes,
                            sizeof(bytes),
                            0);
    for (int i = 0; i < n; ++i) {
        now_ms = prg32_ticks_ms();
        qemu_process_key(bytes[i], now_ms);
    }

    now_ms = prg32_ticks_ms();
    qemu_clear_escape(now_ms);
    return qemu_keys_active(now_ms);
}

#else

uint32_t prg32_qemu_input_read(void) {
    return 0;
}

#endif
