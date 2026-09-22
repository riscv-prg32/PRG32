#include "prg32.h"
#include "prg32_btkbd_internal.h"
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
static int qemu_esc_param;
static uint32_t qemu_esc_deadline_ms;

static int time_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void qemu_press(uint32_t bit, uint32_t now_ms) {
    /* A text cartridge may ask for keys only (prg32_btkbd_set_mapping(0)). */
    if (!prg32_btkbd_session_mapping_enabled()) {
        return;
    }
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

/*
 * The console also acts as the PRG32 keyboard on QEMU: every decoded key is
 * queued for prg32_btkbd_read_key() with the HID usage a real keyboard would
 * send, so keyboard cartridges behave the same on the desktop.
 */
static uint8_t qemu_ascii_usage(uint8_t byte) {
    if (byte >= 'a' && byte <= 'z') {
        return (uint8_t)(0x04u + (byte - 'a'));
    }
    if (byte >= 'A' && byte <= 'Z') {
        return (uint8_t)(0x04u + (byte - 'A'));
    }
    if (byte >= 1 && byte <= 26) {
        return (uint8_t)(0x04u + (byte - 1)); /* CTRL+letter */
    }
    if (byte >= '1' && byte <= '9') {
        return (uint8_t)(PRG32_HID_KEY_1 + (byte - '1'));
    }
    if (byte == '0') {
        return (uint8_t)(PRG32_HID_KEY_1 + 9u);
    }
    if (byte == ' ') {
        return PRG32_HID_KEY_SPACE;
    }
    return 0;
}

static void qemu_type_byte(uint8_t byte) {
    if (byte == '\r' || byte == '\n') {
        prg32_btkbd_inject_key(PRG32_KEY_ENTER, PRG32_HID_KEY_ENTER);
    } else if (byte == 0x7f || byte == '\b') {
        prg32_btkbd_inject_key(PRG32_KEY_BACKSPACE, PRG32_HID_KEY_BACKSPACE);
    } else if (byte == '\t') {
        prg32_btkbd_inject_key(PRG32_KEY_TAB, PRG32_HID_KEY_TAB);
    } else if ((byte >= 32 && byte < 127) || (byte >= 1 && byte <= 26)) {
        prg32_btkbd_inject_key(byte, qemu_ascii_usage(byte));
    }
}

static void qemu_clear_escape(uint32_t now_ms) {
    if (qemu_esc_state == 1 && time_reached(now_ms, qemu_esc_deadline_ms)) {
        prg32_btkbd_inject_key(PRG32_KEY_ESCAPE, PRG32_HID_KEY_ESCAPE);
        qemu_press(PRG32_BTN_B, now_ms);
        qemu_esc_state = 0;
    } else if (qemu_esc_state > 1 && time_reached(now_ms, qemu_esc_deadline_ms)) {
        qemu_esc_state = 0;
        qemu_esc_param = 0;
    }
}

/* Final byte of an escape sequence: arrows also press the D-pad bits. */
static void qemu_escape_final(uint8_t byte, uint32_t now_ms) {
    switch (byte) {
        case 'A':
            prg32_btkbd_inject_key(PRG32_KEY_UP, PRG32_HID_KEY_UP);
            qemu_press(PRG32_BTN_UP, now_ms);
            return;
        case 'B':
            prg32_btkbd_inject_key(PRG32_KEY_DOWN, PRG32_HID_KEY_DOWN);
            qemu_press(PRG32_BTN_DOWN, now_ms);
            return;
        case 'C':
            prg32_btkbd_inject_key(PRG32_KEY_RIGHT, PRG32_HID_KEY_RIGHT);
            qemu_press(PRG32_BTN_RIGHT, now_ms);
            return;
        case 'D':
            prg32_btkbd_inject_key(PRG32_KEY_LEFT, PRG32_HID_KEY_LEFT);
            qemu_press(PRG32_BTN_LEFT, now_ms);
            return;
        case 'H': prg32_btkbd_inject_key(PRG32_KEY_HOME, 0x4A); return;
        case 'F': prg32_btkbd_inject_key(PRG32_KEY_END, 0x4D); return;
        case 'P': case 'Q': case 'R': case 'S': /* ESC O P..S = F1..F4 */
            prg32_btkbd_inject_key(PRG32_KEY_F1 + (byte - 'P'),
                                   (uint8_t)(PRG32_HID_KEY_F1 + (byte - 'P')));
            return;
        case '~':
            break;
        default:
            return;
    }
    int key = 0;
    uint8_t usage = 0;
    switch (qemu_esc_param) {
        case 1: case 7: key = PRG32_KEY_HOME; usage = 0x4A; break;
        case 2: key = PRG32_KEY_INSERT; usage = 0x49; break;
        case 3: key = PRG32_KEY_DELETE; usage = 0x4C; break;
        case 4: case 8: key = PRG32_KEY_END; usage = 0x4D; break;
        case 5: key = PRG32_KEY_PAGE_UP; usage = 0x4B; break;
        case 6: key = PRG32_KEY_PAGE_DOWN; usage = 0x4E; break;
        case 11: case 12: case 13: case 14: case 15:
            key = PRG32_KEY_F1 + (qemu_esc_param - 11); /* F1..F5 */
            break;
        case 17: case 18: case 19: case 20: case 21:
            key = PRG32_KEY_F1 + 5 + (qemu_esc_param - 17); /* F6..F10 */
            break;
        case 23: case 24:
            key = PRG32_KEY_F1 + 10 + (qemu_esc_param - 23); /* F11, F12 */
            break;
        default: break;
    }
    if (key >= PRG32_KEY_F1) {
        usage = (uint8_t)(PRG32_HID_KEY_F1 + (key - PRG32_KEY_F1));
    }
    if (key) {
        prg32_btkbd_inject_key(key, usage);
    }
}

static void qemu_process_key(uint8_t byte, uint32_t now_ms) {
    if (qemu_esc_state == 1) {
        if (byte == '[') {
            qemu_esc_state = 2;
            qemu_esc_param = 0;
            qemu_esc_deadline_ms = now_ms + PRG32_QEMU_ESC_TIMEOUT_MS;
            return;
        }
        if (byte == 'O') {
            qemu_esc_state = 3;
            qemu_esc_deadline_ms = now_ms + PRG32_QEMU_ESC_TIMEOUT_MS;
            return;
        }
        prg32_btkbd_inject_key(PRG32_KEY_ESCAPE, PRG32_HID_KEY_ESCAPE);
        qemu_press(PRG32_BTN_B, now_ms);
        qemu_esc_state = 0;
    }

    if (qemu_esc_state == 2 || qemu_esc_state == 3) {
        /* Terminal escape sequence: ESC [ <digits> <final byte> or ESC O <x>.
         * Digits select keys such as F5 (ESC [ 1 5 ~) or DELETE (ESC [ 3 ~). */
        if (qemu_esc_state == 2 && ((byte >= '0' && byte <= '9') || byte == ';')) {
            if (byte != ';' && qemu_esc_param < 100) {
                qemu_esc_param = qemu_esc_param * 10 + (byte - '0');
            }
            qemu_esc_deadline_ms = now_ms + PRG32_QEMU_ESC_TIMEOUT_MS;
            return;
        }
        qemu_escape_final(byte, now_ms);
        qemu_esc_state = 0;
        qemu_esc_param = 0;
        return;
    }

    if (byte != 0x1b) {
        qemu_type_byte(byte);
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
