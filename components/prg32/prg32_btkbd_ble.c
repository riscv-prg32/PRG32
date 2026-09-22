#include "prg32.h"
#include "prg32_btkbd_internal.h"
#include "prg32_config.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_PRG32_BT_KEYBOARD
#define CONFIG_PRG32_BT_KEYBOARD 0
#endif

#if CONFIG_PRG32_BT_KEYBOARD

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"

/*
 * PRG32 Bluetooth LE keyboard transport (NimBLE central + HID-over-GATT).
 *
 * The ESP32-C6 radio supports Bluetooth LE only, so PRG32 talks to keyboards
 * through the "HID over GATT Profile" (HOGP). The steps below mirror what a
 * desktop computer does when a keyboard is paired:
 *
 *  1. SCAN      listen for advertisements with the HID service UUID 0x1812
 *               or the "keyboard" appearance 0x03C1;
 *  2. CONNECT   open a link to the selected keyboard;
 *  3. PAIR      encrypt the link. Keyboards with keys use "passkey entry":
 *               PRG32 shows six digits that the user types on the keyboard.
 *               The resulting keys are bonded (stored in NVS) so the next
 *               connection only re-encrypts;
 *  4. DISCOVER  find the HID service characteristics and their CCCD
 *               descriptors (the switches that enable notifications);
 *  5. SUBSCRIBE select the fixed 8-byte "boot keyboard" report format when
 *               the keyboard offers it and enable notifications. Every key
 *               change then arrives as a notification that is forwarded to
 *               prg32_btkbd_on_boot_report().
 *
 * NimBLE runs its own host task: GAP/GATT callbacks below execute there.
 * Requests from setup screens (scan, connect, forget) are handed to the small
 * prg32_btkbd task, which also reconnects to the bonded keyboard after a
 * reset or when the keyboard wakes up from sleep.
 */

static const char *TAG = "prg32_btkbd_ble";

void ble_store_config_init(void);

#define BTKBD_UUID_HID_SERVICE 0x1812u
#define BTKBD_UUID_PROTOCOL_MODE 0x2A4Eu
#define BTKBD_UUID_REPORT 0x2A4Du
#define BTKBD_UUID_BOOT_KEYBOARD_INPUT 0x2A22u
#define BTKBD_UUID_CCCD 0x2902u
#define BTKBD_APPEARANCE_HID 0x03C0u
#define BTKBD_APPEARANCE_KEYBOARD 0x03C1u

#define BTKBD_MAX_SCAN 8
#define BTKBD_MAX_CHRS 16
#define BTKBD_PAIR_CONNECT_MS 15000
#define BTKBD_RECONNECT_CONNECT_MS 8000
#define BTKBD_RECONNECT_PAUSE_MS 3000u
#define BTKBD_TASK_STACK 3072
#define BTKBD_NVS_PEER "kbd_peer"

typedef enum {
  BTKBD_REQUEST_NONE = 0,
  BTKBD_REQUEST_SCAN,
  BTKBD_REQUEST_CONNECT,
  BTKBD_REQUEST_FORGET,
} btkbd_request_t;

typedef struct {
  uint16_t val_handle;
  uint16_t uuid;
  uint8_t properties;
  uint16_t cccd_handle;
} btkbd_chr_t;

/* Link state, written by the host task and by the prg32_btkbd task. */
static volatile uint32_t s_state = PRG32_BTKBD_STATE_IDLE;
static volatile int s_synced;
static int s_started;
static uint8_t s_own_addr_type;
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile int32_t s_passkey = -1;
static char s_error[40];

/* Requests from the setup screens. */
static TaskHandle_t s_task;
static volatile btkbd_request_t s_request;
static volatile uint32_t s_request_scan_ms;
static prg32_btkbd_scan_entry_t s_request_target;
static volatile int s_pairing_ui; /* pause auto-reconnect while pairing */
static volatile int s_save_peer;

/* Scan results. */
static prg32_btkbd_scan_entry_t s_scan[BTKBD_MAX_SCAN];
static volatile int s_scan_count;

/* Device being connected and the bonded device saved in NVS. */
static prg32_btkbd_scan_entry_t s_target;
static prg32_btkbd_scan_entry_t s_peer;
static volatile int s_have_peer;

/* GATT discovery results for the HID service. */
static uint16_t s_svc_start;
static uint16_t s_svc_end;
static btkbd_chr_t s_chrs[BTKBD_MAX_CHRS];
static int s_chr_count;
static uint16_t s_protocol_mode_handle;
static int s_use_boot;
static int s_subscribe_index;

static int btkbd_gap_event(struct ble_gap_event *event, void *arg);

static void btkbd_set_error(const char *text) {
  snprintf(s_error, sizeof(s_error), "%s", text ? text : "");
  if (text && text[0]) {
    ESP_LOGW(TAG, "%s", text);
  }
}

static void btkbd_wake_task(void) {
  if (s_task) {
    xTaskNotifyGive(s_task);
  }
}

/* ------------------------------------------------------------------------ */
/* Bonded peer in NVS                                                       */
/* ------------------------------------------------------------------------ */

static void btkbd_load_peer(void) {
  nvs_handle_t nvs;
  s_have_peer = 0;
  if (nvs_open("prg32", NVS_READONLY, &nvs) != ESP_OK) {
    return;
  }
  size_t size = sizeof(s_peer);
  if (nvs_get_blob(nvs, BTKBD_NVS_PEER, &s_peer, &size) == ESP_OK &&
      size == sizeof(s_peer)) {
    s_peer.name[sizeof(s_peer.name) - 1] = '\0';
    s_have_peer = 1;
  }
  nvs_close(nvs);
}

static void btkbd_store_peer(const prg32_btkbd_scan_entry_t *peer) {
  nvs_handle_t nvs;
  if (nvs_open("prg32", NVS_READWRITE, &nvs) != ESP_OK) {
    return;
  }
  if (peer) {
    nvs_set_blob(nvs, BTKBD_NVS_PEER, peer, sizeof(*peer));
  } else {
    nvs_erase_key(nvs, BTKBD_NVS_PEER);
  }
  nvs_commit(nvs);
  nvs_close(nvs);
}

/* ------------------------------------------------------------------------ */
/* Step 1: scanning                                                         */
/* ------------------------------------------------------------------------ */

static void btkbd_scan_report(const ble_addr_t *addr, int8_t rssi,
                              const uint8_t *data, uint8_t length) {
  struct ble_hs_adv_fields fields;
  if (ble_hs_adv_parse_fields(&fields, data, length) != 0) {
    return;
  }

  int is_hid = 0;
  for (int i = 0; i < fields.num_uuids16; ++i) {
    if (ble_uuid_u16(&fields.uuids16[i].u) == BTKBD_UUID_HID_SERVICE) {
      is_hid = 1;
    }
  }
  if (fields.appearance_is_present) {
    /* Accept generic HID and keyboards; skip mice, gamepads, ... */
    is_hid = fields.appearance == BTKBD_APPEARANCE_KEYBOARD ||
             (is_hid && fields.appearance == BTKBD_APPEARANCE_HID);
  }

  int index = -1;
  for (int i = 0; i < s_scan_count; ++i) {
    if (s_scan[i].addr_type == addr->type &&
        memcmp(s_scan[i].addr, addr->val, 6) == 0) {
      index = i;
    }
  }
  if (index < 0) {
    if (!is_hid || s_scan_count >= BTKBD_MAX_SCAN) {
      return; /* scan responses of unknown devices are ignored */
    }
    index = s_scan_count;
    memset(&s_scan[index], 0, sizeof(s_scan[index]));
    s_scan[index].addr_type = addr->type;
    memcpy(s_scan[index].addr, addr->val, 6);
    snprintf(s_scan[index].name, sizeof(s_scan[index].name),
             "%02X:%02X:%02X:%02X:%02X:%02X", addr->val[5], addr->val[4],
             addr->val[3], addr->val[2], addr->val[1], addr->val[0]);
    s_scan_count = index + 1;
  }
  s_scan[index].rssi = rssi;
  if (fields.appearance_is_present) {
    s_scan[index].appearance = fields.appearance;
  }
  if (fields.name && fields.name_len > 0) {
    size_t n = fields.name_len;
    if (n >= sizeof(s_scan[index].name)) {
      n = sizeof(s_scan[index].name) - 1;
    }
    memcpy(s_scan[index].name, fields.name, n);
    s_scan[index].name[n] = '\0';
  }
}

static void btkbd_start_scan(uint32_t duration_ms) {
  struct ble_gap_disc_params params;
  memset(&params, 0, sizeof(params));
  params.passive = 0;           /* active: also ask for scan responses (names) */
  params.filter_duplicates = 0; /* the name often arrives in a later packet */
  s_scan_count = 0;
  int rc = ble_gap_disc(s_own_addr_type, (int32_t)duration_ms, &params,
                        btkbd_gap_event, NULL);
  if (rc == 0) {
    s_state = PRG32_BTKBD_STATE_SCANNING;
    btkbd_set_error("");
  } else {
    btkbd_set_error("SCAN FAILED");
  }
}

/* ------------------------------------------------------------------------ */
/* Step 2: connection                                                       */
/* ------------------------------------------------------------------------ */

static void btkbd_start_connect(const prg32_btkbd_scan_entry_t *entry,
                                int32_t timeout_ms) {
  ble_addr_t addr;
  addr.type = entry->addr_type;
  memcpy(addr.val, entry->addr, 6);
  s_target = *entry;
  s_passkey = -1;
  int rc = ble_gap_connect(s_own_addr_type, &addr, timeout_ms, NULL,
                           btkbd_gap_event, NULL);
  if (rc == 0) {
    s_state = PRG32_BTKBD_STATE_CONNECTING;
  } else {
    btkbd_set_error("CONNECT FAILED");
  }
}

static void btkbd_fail(uint16_t conn, const char *text) {
  btkbd_set_error(text);
  ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

/* ------------------------------------------------------------------------ */
/* Steps 4 and 5: GATT discovery and subscription                           */
/* ------------------------------------------------------------------------ */

static int btkbd_on_subscribed(uint16_t conn, const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr, void *arg);

/* Enable notifications on the next characteristic that needs them. */
static void btkbd_subscribe_next(uint16_t conn) {
  while (s_subscribe_index < s_chr_count) {
    const btkbd_chr_t *chr = &s_chrs[s_subscribe_index++];
    int wanted = s_use_boot ? chr->uuid == BTKBD_UUID_BOOT_KEYBOARD_INPUT
                            : chr->uuid == BTKBD_UUID_REPORT &&
                                  (chr->properties & BLE_GATT_CHR_PROP_NOTIFY);
    if (!wanted || chr->cccd_handle == 0) {
      continue;
    }
    static const uint8_t enable_notify[2] = {1, 0};
    if (ble_gattc_write_flat(conn, chr->cccd_handle, enable_notify,
                             sizeof(enable_notify), btkbd_on_subscribed,
                             NULL) != 0) {
      btkbd_fail(conn, "SUBSCRIBE FAILED");
    }
    return; /* continue in btkbd_on_subscribed() */
  }

  s_state = PRG32_BTKBD_STATE_CONNECTED;
  s_passkey = -1;
  btkbd_set_error("");
  ESP_LOGI(TAG, "keyboard ready: %s (%s protocol)", s_target.name,
           s_use_boot ? "boot" : "report");
  s_save_peer = 1;
  btkbd_wake_task();
}

static int btkbd_on_subscribed(uint16_t conn, const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr, void *arg) {
  (void)attr;
  (void)arg;
  if (error->status != 0) {
    btkbd_fail(conn, "SUBSCRIBE FAILED");
    return 0;
  }
  btkbd_subscribe_next(conn);
  return 0;
}

static void btkbd_choose_protocol(uint16_t conn) {
  int boot_ready = 0;
  int report_ready = 0;
  for (int i = 0; i < s_chr_count; ++i) {
    if (s_chrs[i].cccd_handle == 0) {
      continue;
    }
    if (s_chrs[i].uuid == BTKBD_UUID_BOOT_KEYBOARD_INPUT) {
      boot_ready = 1;
    }
    if (s_chrs[i].uuid == BTKBD_UUID_REPORT) {
      report_ready = 1;
    }
  }

  /* The boot protocol has one fixed 8-byte layout, so it needs no report
   * descriptor parser. Every HOGP keyboard must implement it. */
  s_use_boot = boot_ready && s_protocol_mode_handle != 0;
  if (s_use_boot) {
    static const uint8_t boot_protocol = 0;
    ble_gattc_write_no_rsp_flat(conn, s_protocol_mode_handle, &boot_protocol,
                                sizeof(boot_protocol));
  } else if (!report_ready) {
    btkbd_fail(conn, "NOT A KEYBOARD");
    return;
  }
  s_subscribe_index = 0;
  btkbd_subscribe_next(conn);
}

static int btkbd_on_descriptor(uint16_t conn, const struct ble_gatt_error *error,
                               uint16_t chr_val_handle,
                               const struct ble_gatt_dsc *dsc, void *arg) {
  (void)chr_val_handle;
  (void)arg;
  if (error->status == 0) {
    if (ble_uuid_u16(&dsc->uuid.u) == BTKBD_UUID_CCCD) {
      /* A descriptor belongs to the closest characteristic value before it. */
      int owner = -1;
      for (int i = 0; i < s_chr_count; ++i) {
        if (s_chrs[i].val_handle < dsc->handle &&
            (owner < 0 || s_chrs[i].val_handle > s_chrs[owner].val_handle)) {
          owner = i;
        }
      }
      if (owner >= 0 && s_chrs[owner].cccd_handle == 0) {
        s_chrs[owner].cccd_handle = dsc->handle;
      }
    }
  } else if (error->status == BLE_HS_EDONE) {
    btkbd_choose_protocol(conn);
  } else {
    btkbd_fail(conn, "DESCRIPTOR DISCOVERY FAILED");
  }
  return 0;
}

static int btkbd_on_characteristic(uint16_t conn,
                                   const struct ble_gatt_error *error,
                                   const struct ble_gatt_chr *chr, void *arg) {
  (void)arg;
  if (error->status == 0) {
    uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
    if (uuid == BTKBD_UUID_PROTOCOL_MODE) {
      s_protocol_mode_handle = chr->val_handle;
    }
    if (s_chr_count < BTKBD_MAX_CHRS) {
      s_chrs[s_chr_count].val_handle = chr->val_handle;
      s_chrs[s_chr_count].uuid = uuid;
      s_chrs[s_chr_count].properties = chr->properties;
      s_chrs[s_chr_count].cccd_handle = 0;
      s_chr_count++;
    }
  } else if (error->status == BLE_HS_EDONE) {
    if (ble_gattc_disc_all_dscs(conn, s_svc_start, s_svc_end,
                                btkbd_on_descriptor, NULL) != 0) {
      btkbd_fail(conn, "DESCRIPTOR DISCOVERY FAILED");
    }
  } else {
    btkbd_fail(conn, "CHARACTERISTIC DISCOVERY FAILED");
  }
  return 0;
}

static int btkbd_on_service(uint16_t conn, const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *service, void *arg) {
  (void)arg;
  if (error->status == 0) {
    if (s_svc_start == 0) { /* keep the first HID service */
      s_svc_start = service->start_handle;
      s_svc_end = service->end_handle;
    }
  } else if (error->status == BLE_HS_EDONE) {
    if (s_svc_start == 0) {
      btkbd_fail(conn, "NO HID SERVICE");
    } else if (ble_gattc_disc_all_chrs(conn, s_svc_start, s_svc_end,
                                       btkbd_on_characteristic, NULL) != 0) {
      btkbd_fail(conn, "CHARACTERISTIC DISCOVERY FAILED");
    }
  } else {
    btkbd_fail(conn, "SERVICE DISCOVERY FAILED");
  }
  return 0;
}

static void btkbd_start_discovery(uint16_t conn) {
  s_svc_start = 0;
  s_svc_end = 0;
  s_chr_count = 0;
  s_protocol_mode_handle = 0;
  static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(BTKBD_UUID_HID_SERVICE);
  if (ble_gattc_disc_svc_by_uuid(conn, &hid_uuid.u, btkbd_on_service, NULL) !=
      0) {
    btkbd_fail(conn, "SERVICE DISCOVERY FAILED");
  }
}

static int btkbd_is_input_handle(uint16_t handle) {
  for (int i = 0; i < s_chr_count; ++i) {
    if (s_chrs[i].val_handle != handle) {
      continue;
    }
    return s_use_boot ? s_chrs[i].uuid == BTKBD_UUID_BOOT_KEYBOARD_INPUT
                      : s_chrs[i].uuid == BTKBD_UUID_REPORT;
  }
  return 0;
}

/* ------------------------------------------------------------------------ */
/* GAP events (host task)                                                   */
/* ------------------------------------------------------------------------ */

static int btkbd_gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;
  struct ble_gap_conn_desc desc;

  switch (event->type) {
    case BLE_GAP_EVENT_DISC:
      btkbd_scan_report(&event->disc.addr, event->disc.rssi, event->disc.data,
                        event->disc.length_data);
      return 0;
#if MYNEWT_VAL(BLE_EXT_ADV)
    case BLE_GAP_EVENT_EXT_DISC:
      btkbd_scan_report(&event->ext_disc.addr, event->ext_disc.rssi,
                        event->ext_disc.data, event->ext_disc.length_data);
      return 0;
#endif
    case BLE_GAP_EVENT_DISC_COMPLETE:
      if (s_state == PRG32_BTKBD_STATE_SCANNING) {
        s_state = PRG32_BTKBD_STATE_IDLE;
      }
      return 0;

    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status != 0) {
        s_state = PRG32_BTKBD_STATE_IDLE;
        if (s_pairing_ui) {
          btkbd_set_error("CONNECT FAILED");
        }
        return 0;
      }
      s_conn = event->connect.conn_handle;
      s_state = PRG32_BTKBD_STATE_PAIRING;
      /* Step 3: encrypt. With a bond this reuses the stored key; otherwise
       * NimBLE runs the pairing procedure. */
      if (ble_gap_security_initiate(s_conn) != 0) {
        btkbd_fail(s_conn, "PAIRING FAILED");
      }
      return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
      struct ble_sm_io io;
      memset(&io, 0, sizeof(io));
      io.action = event->passkey.params.action;
      if (io.action == BLE_SM_IOACT_DISP) {
        io.passkey = esp_random() % 1000000u;
        s_passkey = (int32_t)io.passkey;
        ESP_LOGI(TAG, "type %06" PRIu32 " + ENTER on the keyboard", io.passkey);
      } else if (io.action == BLE_SM_IOACT_NUMCMP) {
        io.numcmp_accept = 1;
      } else {
        return 0;
      }
      ble_sm_inject_io(event->passkey.conn_handle, &io);
      return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
      if (event->enc_change.status == 0) {
        s_passkey = -1;
        btkbd_start_discovery(event->enc_change.conn_handle);
      } else {
        /* Usually a stale bond: the keyboard was paired with another host.
         * Forget our half of the bond so the next attempt pairs again. */
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
          ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        btkbd_fail(event->enc_change.conn_handle, "PAIRING FAILED, RETRY");
      }
      return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
      /* The keyboard lost its bond and pairs again: accept the new keys. */
      if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
        ble_store_util_delete_peer(&desc.peer_id_addr);
      }
      return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_NOTIFY_RX:
      if (s_state == PRG32_BTKBD_STATE_CONNECTED &&
          event->notify_rx.conn_handle == s_conn &&
          btkbd_is_input_handle(event->notify_rx.attr_handle)) {
        uint8_t report[8];
        uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
        /* Boot reports are always 8 bytes. In report protocol only reports
         * with the same 8-byte keyboard layout are understood. */
        if (length == sizeof(report) &&
            ble_hs_mbuf_to_flat(event->notify_rx.om, report, sizeof(report),
                                NULL) == 0) {
          prg32_btkbd_on_boot_report(report, sizeof(report));
        }
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "keyboard disconnected, reason=%d",
               event->disconnect.reason);
      s_conn = BLE_HS_CONN_HANDLE_NONE;
      s_passkey = -1;
      s_chr_count = 0;
      s_state = PRG32_BTKBD_STATE_IDLE;
      prg32_btkbd_on_disconnect();
      btkbd_wake_task();
      return 0;

    default:
      return 0;
  }
}

/* ------------------------------------------------------------------------ */
/* NimBLE host and the prg32_btkbd task                                     */
/* ------------------------------------------------------------------------ */

static void btkbd_on_sync(void) {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &s_own_addr_type);
  s_synced = 1;
  btkbd_wake_task();
}

static void btkbd_on_reset(int reason) {
  s_synced = 0;
  ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
}

static void btkbd_host_task(void *param) {
  (void)param;
  nimble_port_run(); /* returns only after nimble_port_stop() */
  nimble_port_freertos_deinit();
}

/* Wait until a pending scan or connection attempt has really stopped. */
static void btkbd_stop_radio_activity(void) {
  if (ble_gap_disc_active()) {
    ble_gap_disc_cancel();
  }
  if (ble_gap_conn_active()) {
    ble_gap_conn_cancel();
  }
  for (int i = 0; i < 20 && (ble_gap_conn_active() || ble_gap_disc_active());
       ++i) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (s_state == PRG32_BTKBD_STATE_SCANNING ||
      s_state == PRG32_BTKBD_STATE_CONNECTING) {
    s_state = PRG32_BTKBD_STATE_IDLE;
  }
}

static void btkbd_disconnect_and_wait(void) {
  if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    for (int i = 0; i < 40 && s_conn != BLE_HS_CONN_HANDLE_NONE; ++i) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

static void btkbd_task(void *param) {
  (void)param;
  uint32_t next_reconnect_ms = 0;
  while (1) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    if (!s_synced) {
      continue;
    }

    if (s_save_peer) {
      s_save_peer = 0;
      s_peer = s_target;
      s_have_peer = 1;
      btkbd_store_peer(&s_peer);
    }

    btkbd_request_t request = s_request;
    s_request = BTKBD_REQUEST_NONE;
    if (request == BTKBD_REQUEST_SCAN) {
      btkbd_stop_radio_activity();
      btkbd_disconnect_and_wait();
      btkbd_start_scan(s_request_scan_ms);
    } else if (request == BTKBD_REQUEST_CONNECT) {
      btkbd_stop_radio_activity();
      btkbd_disconnect_and_wait();
      btkbd_set_error("");
      btkbd_start_connect(&s_request_target, BTKBD_PAIR_CONNECT_MS);
    } else if (request == BTKBD_REQUEST_FORGET) {
      btkbd_stop_radio_activity();
      btkbd_disconnect_and_wait();
      if (s_have_peer) {
        ble_addr_t addr;
        addr.type = s_peer.addr_type;
        memcpy(addr.val, s_peer.addr, 6);
        ble_gap_unpair(&addr); /* deletes the bond keys from NVS */
      }
      s_have_peer = 0;
      btkbd_store_peer(NULL);
    }

    /* Automatic reconnection to the bonded keyboard. */
    uint32_t now = prg32_ticks_ms();
    if (!s_pairing_ui && s_have_peer && s_state == PRG32_BTKBD_STATE_IDLE &&
        !ble_gap_conn_active() && !ble_gap_disc_active() &&
        (int32_t)(now - next_reconnect_ms) >= 0) {
      btkbd_start_connect(&s_peer, BTKBD_RECONNECT_CONNECT_MS);
      next_reconnect_ms =
          now + BTKBD_RECONNECT_CONNECT_MS + BTKBD_RECONNECT_PAUSE_MS;
    }
  }
}

int prg32_btkbd_ble_init(void) {
  if (s_started) {
    return 0;
  }
  btkbd_load_peer();

  esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
    return -1;
  }

  ble_hs_cfg.sync_cb = btkbd_on_sync;
  ble_hs_cfg.reset_cb = btkbd_on_reset;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  /* PRG32 can show a passkey but has no keyboard of its own: DisplayOnly. */
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_store_config_init();

  if (xTaskCreate(btkbd_task, "prg32_btkbd", BTKBD_TASK_STACK, NULL, 3,
                  &s_task) != pdPASS) {
    ESP_LOGE(TAG, "cannot create keyboard task");
    return -1;
  }
  nimble_port_freertos_init(btkbd_host_task);
  s_started = 1;
  ESP_LOGI(TAG, "Bluetooth LE keyboard host started%s",
           s_have_peer ? ", reconnecting to bonded keyboard" : "");
  return 0;
}

int prg32_btkbd_ble_available(void) {
  return s_started;
}

uint32_t prg32_btkbd_ble_state(void) {
  return s_state;
}

int prg32_btkbd_ble_scan_start(uint32_t duration_ms) {
  if (!s_started) {
    return -1;
  }
  s_pairing_ui = 1;
  s_scan_count = 0;
  s_request_scan_ms = duration_ms;
  s_request = BTKBD_REQUEST_SCAN;
  btkbd_wake_task();
  return 0;
}

void prg32_btkbd_ble_scan_stop(void) {
  if (s_started && ble_gap_disc_active()) {
    ble_gap_disc_cancel();
    if (s_state == PRG32_BTKBD_STATE_SCANNING) {
      s_state = PRG32_BTKBD_STATE_IDLE;
    }
  }
  s_pairing_ui = 0;
  btkbd_wake_task();
}

int prg32_btkbd_ble_scan_count(void) {
  return s_scan_count;
}

int prg32_btkbd_ble_scan_get(int index, prg32_btkbd_scan_entry_t *out) {
  if (!out || index < 0 || index >= s_scan_count) {
    return -1;
  }
  *out = s_scan[index];
  return 0;
}

int prg32_btkbd_ble_connect(const prg32_btkbd_scan_entry_t *entry) {
  if (!s_started || !entry) {
    return -1;
  }
  s_pairing_ui = 1;
  btkbd_set_error("");
  s_request_target = *entry;
  s_request = BTKBD_REQUEST_CONNECT;
  btkbd_wake_task();
  return 0;
}

void prg32_btkbd_ble_forget(void) {
  if (!s_started) {
    return;
  }
  s_request = BTKBD_REQUEST_FORGET;
  btkbd_wake_task();
}

int prg32_btkbd_ble_has_peer(void) {
  return s_have_peer;
}

int32_t prg32_btkbd_ble_passkey(void) {
  return s_passkey;
}

const char *prg32_btkbd_ble_last_error(void) {
  return s_error;
}

int prg32_btkbd_ble_device_name(char *buffer, size_t capacity) {
  if (!buffer || capacity == 0) {
    return -1;
  }
  buffer[0] = '\0';
  if (s_state == PRG32_BTKBD_STATE_CONNECTED) {
    snprintf(buffer, capacity, "%s", s_target.name);
  } else if (s_have_peer) {
    snprintf(buffer, capacity, "%s", s_peer.name);
  } else {
    return -1;
  }
  return (int)strlen(buffer);
}

#else /* !CONFIG_PRG32_BT_KEYBOARD: QEMU and builds without Bluetooth */

int prg32_btkbd_ble_init(void) { return -1; }
int prg32_btkbd_ble_available(void) { return 0; }
uint32_t prg32_btkbd_ble_state(void) { return PRG32_BTKBD_STATE_UNAVAILABLE; }
int prg32_btkbd_ble_scan_start(uint32_t duration_ms) {
  (void)duration_ms;
  return -1;
}
void prg32_btkbd_ble_scan_stop(void) {}
int prg32_btkbd_ble_scan_count(void) { return 0; }
int prg32_btkbd_ble_scan_get(int index, prg32_btkbd_scan_entry_t *out) {
  (void)index;
  (void)out;
  return -1;
}
int prg32_btkbd_ble_connect(const prg32_btkbd_scan_entry_t *entry) {
  (void)entry;
  return -1;
}
void prg32_btkbd_ble_forget(void) {}
int prg32_btkbd_ble_has_peer(void) { return 0; }
int32_t prg32_btkbd_ble_passkey(void) { return -1; }
const char *prg32_btkbd_ble_last_error(void) {
  return "BLUETOOTH NOT IN THIS BUILD";
}
int prg32_btkbd_ble_device_name(char *buffer, size_t capacity) {
  if (buffer && capacity > 0) {
    buffer[0] = '\0';
  }
  return -1;
}

#endif
