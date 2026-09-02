#include "driver/uart.h"
#include "esp_log.h"
#include "string.h"

// Define the pins you want to use for UART1.
// Change these depending on your board's wiring.
#define PRG32_QEMU_UART_1_TX 4
#define PRG32_QEMU_UART_1_RX 5

static const char *TAG = "prg32_qemu_audio";

void prg32_qemu_audio_init(void) {
  const uart_port_t uart_num = UART_NUM_1;

  if (uart_is_driver_installed(uart_num)) {
      return;
  }

  // 1. Configure the UART parameters
  const uart_config_t uart_config = {
      .baud_rate = 4000000,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t err = uart_param_config(uart_num, &uart_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(err));
    return;
  }

  // 2. Set the pins for UART1
  err = uart_set_pin(uart_num, PRG32_QEMU_UART_1_TX, PRG32_QEMU_UART_1_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
    return;
  }

  // 3. Install the UART driver with a large TX buffer so audio doesn't block
  err = uart_driver_install(uart_num, 1024 * 2, 4096, 0, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "UART install failed: %s", esp_err_to_name(err));
    return;
  }

  ESP_LOGI(TAG, "UART1 initialized successfully.");
}

void prg32_qemu_audio_write_pcm(const int16_t *buffer, size_t frames,
                                int mode) {
  const uart_port_t uart_num = UART_NUM_1;
  if (!uart_is_driver_installed(uart_num)) {
    return;
  }

  // Each frame is 2 bytes (mono) or 4 bytes (stereo)
  size_t bytes = frames * (mode == 2 ? 4 : 2);

  uart_write_bytes(uart_num, buffer, bytes);
}

void prg32_qemu_audio_read_ack(uint8_t *ack, size_t len) {
  const uart_port_t uart_num = UART_NUM_1;
  uart_read_bytes(uart_num, ack, len, portMAX_DELAY);
}
