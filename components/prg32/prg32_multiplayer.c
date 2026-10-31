#include "prg32.h"
#include "prg32_config.h"
#include "prg32_multiplayer_wire.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_PRG32_DISPLAY_QEMU_RGB
#define CONFIG_PRG32_DISPLAY_QEMU_RGB 0
#endif

#ifndef CONFIG_PRG32_MULTIPLAYER_OFFLINE_STUB
#define CONFIG_PRG32_MULTIPLAYER_OFFLINE_STUB 0
#endif

#ifndef PRG32_MULTIPLAYER_ENABLE
#define PRG32_MULTIPLAYER_ENABLE 1
#endif

#ifndef PRG32_MULTIPLAYER_TRANSPORT_ENABLE
#define PRG32_MULTIPLAYER_TRANSPORT_ENABLE PRG32_MULTIPLAYER_ENABLE
#endif

#ifndef PRG32_MULTIPLAYER_SERVER_URL
#define PRG32_MULTIPLAYER_SERVER_URL "ws://192.168.4.2:8081"
#endif

#ifndef PRG32_MULTIPLAYER_SEND_PERIOD_MS
#define PRG32_MULTIPLAYER_SEND_PERIOD_MS 50
#endif

#ifndef PRG32_MULTIPLAYER_PEER_TIMEOUT_MS
#define PRG32_MULTIPLAYER_PEER_TIMEOUT_MS 3000
#endif

/* What a client session is doing on the wire (protocol: "Connection lifecycle").
 * `member_requested` below says what the cartridge wants; the link says how far
 * the network has got. */
typedef enum {
    PRG32_MP_LINK_DISCONNECTED = 0,
    PRG32_MP_LINK_CONNECTED_UNJOINED,
    PRG32_MP_LINK_JOIN_PENDING,
    PRG32_MP_LINK_JOINED,
} prg32_mp_link_t;

typedef struct {
    uint8_t initialized;
    uint8_t member_requested;
    prg32_mp_link_t link;
    uint32_t flags;
    /* Authoritative ID from `welcome`; zero until it has been processed. */
    uint32_t player_id;
    /* The ID to ask for in the next `join`. The server may refuse it. */
    uint32_t preferred_player_id;
    uint32_t last_send_ms;
    uint32_t last_join_ms;
    char signature[PRG32_MP_SIGNATURE_LEN];
    prg32_player_state_t local;
    prg32_player_state_t peers[PRG32_MP_MAX_PEERS];
    int peer_count;
} prg32_mp_state_t;

static prg32_mp_state_t g_mp;

static void ensure_mp_state(void);

static void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void clear_peers(void) {
    memset(g_mp.peers, 0, sizeof(g_mp.peers));
    g_mp.peer_count = 0;
}

static void prune_peers(uint32_t now_ms) {
    int out = 0;
    for (int i = 0; i < g_mp.peer_count; ++i) {
        prg32_player_state_t peer = g_mp.peers[i];
        if ((uint32_t)(now_ms - peer.last_seen_ms) <=
            PRG32_MULTIPLAYER_PEER_TIMEOUT_MS) {
            g_mp.peers[out++] = peer;
        }
    }
    g_mp.peer_count = out;
}

#if !PRG32_MULTIPLAYER_TRANSPORT_ENABLE

/* No network. With CONFIG_PRG32_MULTIPLAYER_OFFLINE_STUB, join succeeds locally
 * so that a deterministic test can run a multiplayer cartridge alone. The
 * protocol forbids reporting that as real multiplayer, so
 * prg32_multiplayer_available() stays false in both cases. */

void prg32_multiplayer_init(void) {
    if (!g_mp.initialized) {
        memset(&g_mp, 0, sizeof(g_mp));
        g_mp.initialized = 1;
    }
}

static void ensure_mp_state(void) {
    prg32_multiplayer_init();
}

bool prg32_multiplayer_available(void) {
    return false;
}

int prg32_multiplayer_join(const char *cartridge_signature, uint32_t flags) {
    ensure_mp_state();
#if CONFIG_PRG32_MULTIPLAYER_OFFLINE_STUB
    if (!prg32_mp_valid_signature(cartridge_signature)) {
        return -1;
    }
    g_mp.member_requested = 1;
    g_mp.link = PRG32_MP_LINK_JOINED;
    g_mp.player_id = 1;
    g_mp.flags = flags;
    copy_text(g_mp.signature, sizeof(g_mp.signature), cartridge_signature);
    clear_peers();
    return 0;
#else
    (void)cartridge_signature;
    (void)flags;
    return -1;
#endif
}

int prg32_multiplayer_leave(void) {
    ensure_mp_state();
    g_mp.member_requested = 0;
    g_mp.link = PRG32_MP_LINK_DISCONNECTED;
    g_mp.player_id = 0;
    clear_peers();
    return 0;
}

void prg32_multiplayer_tick(void) {
    ensure_mp_state();
    prune_peers(prg32_ticks_ms());
}

uint32_t prg32_multiplayer_local_player_id(void) {
    ensure_mp_state();
    return g_mp.link == PRG32_MP_LINK_JOINED ? g_mp.player_id : 0;
}

int prg32_multiplayer_set_local_state(int16_t x,
                                      int16_t y,
                                      uint16_t sprite,
                                      uint16_t flags) {
    ensure_mp_state();
    if (!g_mp.member_requested) {
        return -1;
    }
    g_mp.local.player_id = g_mp.player_id;
    g_mp.local.x = x;
    g_mp.local.y = y;
    g_mp.local.sprite = sprite;
    g_mp.local.flags = flags;
    g_mp.local.frame++;
    g_mp.local.last_seen_ms = prg32_ticks_ms();
    return 0;
}

int prg32_multiplayer_set_input(uint32_t input) {
    ensure_mp_state();
    if (!g_mp.member_requested) {
        return -1;
    }
    g_mp.local.input = input & 0x7fu;
    return 0;
}

int prg32_multiplayer_get_peer_count(void) {
    ensure_mp_state();
    prune_peers(prg32_ticks_ms());
    return g_mp.peer_count;
}

int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out) {
    ensure_mp_state();
    prune_peers(prg32_ticks_ms());
    if (!out || index < 0 || index >= g_mp.peer_count) {
        return -1;
    }
    *out = g_mp.peers[index];
    return 0;
}

#else

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_PRG32_DISPLAY_QEMU_RGB
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"
#include "soc/soc.h"
#else
#include "esp_mac.h"
#endif

/* Sending a state message may wait this long for the socket. A full socket
 * then costs one late frame, not the connection: the WebSocket client treats a
 * write that returns nothing as a failure and aborts, and with a timeout of
 * zero a busy socket does exactly that. */
#define PRG32_MP_SEND_TIMEOUT_MS 100
/* While connected but not admitted (a lost `join`, or `room_full`), ask again
 * this often. This also keeps the server's 15 s idle timeout from firing. */
#define PRG32_MP_JOIN_RETRY_MS 5000
/* Reconnection delay: doubles from the first value up to the second, plus up
 * to half of it again at random, so a classroom does not reconnect in step. */
#define PRG32_MP_RECONNECT_MIN_MS 1000
#define PRG32_MP_RECONNECT_MAX_MS 30000
/* WebSocket ping traffic that notices a connection that went silent. The
 * server ignores control frames for its idle timeout, so state messages keep
 * the session alive; these only detect a dead path sooner. */
#define PRG32_MP_PING_INTERVAL_SEC 5
#define PRG32_MP_PONG_TIMEOUT_SEC 12
/* How long connecting and the WebSocket upgrade may take. A busy server answers
 * slowly, and giving up early only adds another reconnection to its load. */
#define PRG32_MP_NETWORK_TIMEOUT_MS 5000
/* TLS needs more stack than the plain socket path. */
#define PRG32_MP_TASK_STACK_WS 4096
#define PRG32_MP_TASK_STACK_WSS 8192

static const char *TAG = "prg32_mp";
static esp_websocket_client_handle_t g_ws;
static SemaphoreHandle_t g_mp_lock;
/* Set by the WebSocket task on `device_blocked`; acted on by the tick, because
 * the task cannot destroy its own client. */
static volatile uint8_t g_policy_stop;
static uint32_t g_reconnect_delay_ms;

/* The stable local identity source, sent only in `join` and never logged. */
static char g_local_device_id[PRG32_MP_LOCAL_DEVICE_ID_LEN];

#if CONFIG_PRG32_DISPLAY_QEMU_RGB
static const char *const PRG32_MP_PLATFORM = "qemu";
#else
static const char *const PRG32_MP_PLATFORM = "esp32c6";
#endif

static int lock_mp(void) {
    if (!g_mp_lock) {
        return 0;
    }
    return xSemaphoreTake(g_mp_lock, portMAX_DELAY) == pdTRUE ? 0 : -1;
}

static void unlock_mp(void) {
    if (g_mp_lock) {
        xSemaphoreGive(g_mp_lock);
    }
}

#if CONFIG_PRG32_DISPLAY_QEMU_RGB

/* Each emulator installation has a random UUID in its own emulated NVS, so
 * flash images must be cloned before their first run or have this key erased;
 * `prg32 qemu run --instance N` does the erasing. */
#define PRG32_MP_IDENTITY_NAMESPACE "prg32id"
#define PRG32_MP_IDENTITY_KEY "install"
#define PRG32_MP_UUID_TEXT_LEN 36

static void load_local_device_id(void) {
    nvs_handle_t nvs;
    if (nvs_open(PRG32_MP_IDENTITY_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        size_t size = sizeof(g_local_device_id);
        if (nvs_get_str(nvs, PRG32_MP_IDENTITY_KEY, g_local_device_id, &size) ==
                ESP_OK &&
            strlen(g_local_device_id) == PRG32_MP_UUID_TEXT_LEN) {
            nvs_close(nvs);
            return;
        }
    } else {
        nvs = 0;
    }

    uint8_t uuid[16];
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uint8_t)((uuid[6] & 0x0f) | 0x40); /* version 4 */
    uuid[8] = (uint8_t)((uuid[8] & 0x3f) | 0x80); /* RFC 4122 variant */
    snprintf(g_local_device_id,
             sizeof(g_local_device_id),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6],
             uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13],
             uuid[14], uuid[15]);
    if (nvs) {
        if (nvs_set_str(nvs, PRG32_MP_IDENTITY_KEY, g_local_device_id) != ESP_OK ||
            nvs_commit(nvs) != ESP_OK) {
            ESP_LOGW(TAG, "could not save the installation identity");
        }
        nvs_close(nvs);
    }
}

/* QEMU has no Wi-Fi. Its network is the emulated OpenCores Ethernet MAC behind
 * `-nic user,model=open_eth`, and DHCP comes from QEMU's user-mode network. */
/* Receiving by polling instead of by interrupt.
 *
 * With busy Ethernet traffic, Espressif's QEMU sometimes ends up with the CPU's
 * interrupt threshold stuck at the critical-section level while no task is in a
 * critical section. Every interrupt, including the system tick, then stays masked
 * and the whole emulated console stops for good. It happens with either TCG
 * threading mode. The cause is in the emulator's delivery of the MAC's interrupt.
 *
 * The driver's receive task only needs a task notification, not the interrupt
 * itself. So this task keeps the MAC's interrupt masked and notifies the driver
 * when the MAC reports a received frame, once per tick (10 ms). The driver's own
 * buffers hold what arrives in between. Real hardware is not affected: this code
 * exists only in QEMU builds. These are the OpenCores MAC's registers; the
 * driver's header for them is private. */
#define PRG32_OPENETH_INT_SOURCE_REG 0x600CD004
#define PRG32_OPENETH_INT_MASK_REG 0x600CD008
#define PRG32_OPENETH_INT_RXB (1u << 2)
#define PRG32_OPENETH_RX_TASK_NAME "emac_rx"
#define PRG32_MP_ETH_POLL_STACK 2048
#define PRG32_MP_ETH_POLL_PRIORITY 16

static void ethernet_poll_task(void *arg) {
    TaskHandle_t rx_task = (TaskHandle_t)arg;
    while (1) {
        REG_WRITE(PRG32_OPENETH_INT_MASK_REG, 0);
        if (REG_READ(PRG32_OPENETH_INT_SOURCE_REG) & PRG32_OPENETH_INT_RXB) {
            /* Clear first: a frame arriving after this sets the bit again. */
            REG_WRITE(PRG32_OPENETH_INT_SOURCE_REG, PRG32_OPENETH_INT_RXB);
            xTaskNotifyGive(rx_task);
        }
        vTaskDelay(1);
    }
}

static int ensure_network(void) {
    static bool started;
    if (started) {
        return 0;
    }

    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_OK;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "network stack init failed: %s", esp_err_to_name(err));
        return -1;
    }

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 100;
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    err = esp_eth_driver_install(&eth_config, &eth);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet driver install failed: %s", esp_err_to_name(err));
        return -1;
    }

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_config);
    if (!netif) {
        return -1;
    }
    err = esp_netif_attach(netif, esp_eth_new_netif_glue(eth));
    if (err == ESP_OK) {
        err = esp_eth_start(eth);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet start failed: %s", esp_err_to_name(err));
        return -1;
    }
    TaskHandle_t rx_task = xTaskGetHandle(PRG32_OPENETH_RX_TASK_NAME);
    if (!rx_task || xTaskCreate(ethernet_poll_task,
                                "prg32_eth_poll",
                                PRG32_MP_ETH_POLL_STACK,
                                rx_task,
                                PRG32_MP_ETH_POLL_PRIORITY,
                                NULL) != pdPASS) {
        ESP_LOGW(TAG, "could not start Ethernet receive polling");
        return -1;
    }
    started = true;
    return 0;
}

#else

static void load_local_device_id(void) {
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        ESP_LOGW(TAG, "could not read the factory MAC");
    }
    snprintf(g_local_device_id,
             sizeof(g_local_device_id),
             "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int ensure_network(void) {
    prg32_wifi_mode_t mode = prg32_wifi_current_mode();
    if (mode == PRG32_WIFI_MODE_STA || mode == PRG32_WIFI_MODE_APSTA) {
        return 0;
    }

    prg32_wifi_config_t config = {
#if PRG32_WIFI_AP_ENABLE
        .mode = PRG32_WIFI_MODE_APSTA,
#else
        .mode = PRG32_WIFI_MODE_STA,
#endif
    };
    copy_text(config.ssid, sizeof(config.ssid), PRG32_WIFI_SSID);
    copy_text(config.password, sizeof(config.password), PRG32_WIFI_PASSWORD);
    copy_text(config.ap_ssid, sizeof(config.ap_ssid), PRG32_WIFI_AP_SSID);
    copy_text(config.ap_password,
              sizeof(config.ap_password),
              PRG32_WIFI_AP_PASSWORD);
    return prg32_wifi_start_mode(&config);
}

#endif

static int send_text(const char *text, int len) {
    if (!g_ws) {
        return -1;
    }
    return esp_websocket_client_send_text(g_ws, text, len,
                                          pdMS_TO_TICKS(PRG32_MP_SEND_TIMEOUT_MS));
}

/* Sends `join` when the socket is up. No lock is held while sending: the
 * WebSocket task takes the multiplayer lock with its client lock held, so
 * holding them the other way round could deadlock. */
static void send_join(void) {
    char msg[PRG32_MP_JOIN_JSON_MAX];
    int n = -1;
    if (lock_mp() != 0) {
        return;
    }
    if (g_mp.member_requested && g_mp.link != PRG32_MP_LINK_DISCONNECTED) {
        n = prg32_mp_format_join(msg, sizeof(msg), g_mp.signature, g_mp.flags,
                                 g_mp.preferred_player_id, PRG32_MP_PLATFORM,
                                 g_local_device_id);
        if (n > 0) {
            /* A second join moves this connection to the new room, so the old
             * session's identity and peers no longer apply. */
            g_mp.link = PRG32_MP_LINK_JOIN_PENDING;
            g_mp.player_id = 0;
            clear_peers();
        }
        g_mp.last_join_ms = prg32_ticks_ms();
    }
    unlock_mp();
    if (n > 0) {
        send_text(msg, n);
    }
}

/* A new socket is a new session: the peers and the admission are gone. */
static void forget_session(prg32_mp_link_t link) {
    if (lock_mp() == 0) {
        g_mp.link = link;
        g_mp.player_id = 0;
        clear_peers();
        unlock_mp();
    }
}

static void schedule_reconnect(esp_websocket_client_handle_t client) {
    uint32_t delay_ms = g_reconnect_delay_ms ? g_reconnect_delay_ms
                                             : PRG32_MP_RECONNECT_MIN_MS;
    uint32_t next_ms = delay_ms * 2;
    g_reconnect_delay_ms =
        next_ms > PRG32_MP_RECONNECT_MAX_MS ? PRG32_MP_RECONNECT_MAX_MS : next_ms;
    delay_ms += esp_random() % (delay_ms / 2 + 1);
    esp_websocket_client_set_reconnect_timeout(client, (int)delay_ms);
    ESP_LOGI(TAG, "reconnecting in %lu ms", (unsigned long)delay_ms);
}

static void handle_welcome(const prg32_mp_message_t *msg) {
    if (lock_mp() != 0) {
        return;
    }
    if (g_mp.member_requested) {
        /* The server's value replaces the provisional one. */
        g_mp.player_id = msg->player_id;
        g_mp.preferred_player_id = msg->player_id;
        g_mp.local.player_id = msg->player_id;
        g_mp.link = PRG32_MP_LINK_JOINED;
        g_reconnect_delay_ms = 0;
    }
    unlock_mp();
    /* Only server-issued identifiers are logged, never the local source. */
    ESP_LOGI(TAG,
             "joined %s as player %lu, device %s, platform %s, persistent %d, "
             "connection %s",
             g_mp.signature,
             (unsigned long)msg->player_id,
             msg->public_device_id[0] ? msg->public_device_id : "(legacy server)",
             msg->platform[0] ? msg->platform : "(legacy server)",
             msg->persistent_device_id ? 1 : 0,
             msg->connection_id[0] ? msg->connection_id : "(legacy server)");
}

/* Stores a snapshot under its player ID, unless it is older than the one held. */
static void handle_peer(const prg32_mp_message_t *msg) {
    if (lock_mp() != 0) {
        return;
    }
    const prg32_player_state_t *peer = &msg->peer;
    if (g_mp.link == PRG32_MP_LINK_JOINED && peer->player_id != g_mp.player_id) {
        int slot = -1;
        for (int i = 0; i < g_mp.peer_count; ++i) {
            if (g_mp.peers[i].player_id == peer->player_id) {
                slot = i;
                break;
            }
        }
        if (slot < 0 && g_mp.peer_count < PRG32_MP_MAX_PEERS) {
            slot = g_mp.peer_count++;
        } else if (slot >= 0 &&
                   prg32_mp_frame_is_stale(peer->frame, g_mp.peers[slot].frame)) {
            slot = -1;
        }
        if (slot >= 0) {
            g_mp.peers[slot] = *peer;
            g_mp.peers[slot].last_seen_ms = prg32_ticks_ms();
        }
    }
    unlock_mp();
}

static void handle_leave(uint32_t player_id) {
    if (lock_mp() != 0) {
        return;
    }
    int out = 0;
    for (int i = 0; i < g_mp.peer_count; ++i) {
        if (g_mp.peers[i].player_id != player_id) {
            g_mp.peers[out++] = g_mp.peers[i];
        }
    }
    g_mp.peer_count = out;
    unlock_mp();
}

static void handle_error(const prg32_mp_message_t *msg) {
    ESP_LOGW(TAG, "server error: %s", msg->error_code);
    switch (msg->error_action) {
        case PRG32_MP_ERROR_ROOM_FULL:
            /* The socket stays open without a room; the tick asks again. */
            forget_session(PRG32_MP_LINK_CONNECTED_UNJOINED);
            break;
        case PRG32_MP_ERROR_BACK_OFF:
            g_reconnect_delay_ms = PRG32_MP_RECONNECT_MAX_MS;
            break;
        case PRG32_MP_ERROR_STOP:
            g_reconnect_delay_ms = PRG32_MP_RECONNECT_MAX_MS;
            g_policy_stop = 1;
            break;
        case PRG32_MP_ERROR_REPORT:
        default:
            break;
    }
}

static void handle_ws_text(const char *data, int len) {
    prg32_mp_message_t msg;
    switch (prg32_mp_parse_message(data, (size_t)len, &msg)) {
        case PRG32_MP_MSG_WELCOME:
            handle_welcome(&msg);
            break;
        case PRG32_MP_MSG_PEER:
            handle_peer(&msg);
            break;
        case PRG32_MP_MSG_LEAVE:
            handle_leave(msg.player_id);
            break;
        case PRG32_MP_MSG_ERROR:
            handle_error(&msg);
            break;
        case PRG32_MP_MSG_INVALID:
            ESP_LOGW(TAG, "ignored a malformed message of %d bytes", len);
            break;
        case PRG32_MP_MSG_PONG:
        case PRG32_MP_MSG_UNKNOWN:
        default:
            break;
    }
}

static void websocket_event(void *handler_args,
                            esp_event_base_t base,
                            int32_t event_id,
                            void *event_data) {
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data =
        (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            forget_session(PRG32_MP_LINK_CONNECTED_UNJOINED);
            send_join();
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
            forget_session(PRG32_MP_LINK_DISCONNECTED);
            schedule_reconnect(data->client);
            break;
        case WEBSOCKET_EVENT_DATA:
            /* Opcode 1 is a text frame. Binary, ping, pong, and close frames
             * also arrive here. Messages from the server are far smaller than
             * the client's receive buffer, so a message delivered in pieces
             * cannot be one of ours. */
            if (data->op_code == 0x01 && data->fin && data->payload_offset == 0 &&
                data->data_len == data->payload_len && data->data_len > 0) {
                handle_ws_text(data->data_ptr, data->data_len);
            }
            break;
        default:
            break;
    }
}

static int ensure_client(void) {
    if (g_ws) {
        return 0;
    }
    bool secure = strncmp(PRG32_MULTIPLAYER_SERVER_URL, "wss:", 4) == 0;
    esp_websocket_client_config_t cfg = {
        .uri = PRG32_MULTIPLAYER_SERVER_URL,
        .reconnect_timeout_ms = PRG32_MP_RECONNECT_MIN_MS,
        .network_timeout_ms = PRG32_MP_NETWORK_TIMEOUT_MS,
        /* Reconnect after the server closes cleanly too, for example after
         * its idle timeout. */
        .enable_close_reconnect = true,
        .ping_interval_sec = PRG32_MP_PING_INTERVAL_SEC,
        .pingpong_timeout_sec = PRG32_MP_PONG_TIMEOUT_SEC,
        .task_stack = secure ? PRG32_MP_TASK_STACK_WSS : PRG32_MP_TASK_STACK_WS,
        .crt_bundle_attach = secure ? esp_crt_bundle_attach : NULL,
    };
    g_ws = esp_websocket_client_init(&cfg);
    if (!g_ws) {
        return -1;
    }
    ESP_ERROR_CHECK(esp_websocket_register_events(g_ws,
                                                  WEBSOCKET_EVENT_ANY,
                                                  websocket_event,
                                                  NULL));
    esp_err_t err = esp_websocket_client_start(g_ws);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WebSocket start failed: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(g_ws);
        g_ws = NULL;
        return -1;
    }
    return 0;
}

/* Stops the transport and frees its memory for the next cartridge. Waits for
 * the WebSocket task to end, up to the network timeout if it is connecting. */
static void destroy_client(void) {
    if (g_ws) {
        esp_websocket_client_destroy(g_ws);
        g_ws = NULL;
    }
    g_reconnect_delay_ms = 0;
    g_policy_stop = 0;
}

void prg32_multiplayer_init(void) {
    if (!g_mp_lock) {
        g_mp_lock = xSemaphoreCreateMutex();
    }
    if (!g_mp.initialized) {
        memset(&g_mp, 0, sizeof(g_mp));
        g_mp.initialized = 1;
        /* Only a preference: the ID is not valid until `welcome` confirms it. */
        g_mp.preferred_player_id = esp_random();
        if (g_mp.preferred_player_id == 0) {
            g_mp.preferred_player_id = 1;
        }
        load_local_device_id();
    }
}

static void ensure_mp_state(void) {
    prg32_multiplayer_init();
}

bool prg32_multiplayer_available(void) {
    return PRG32_MULTIPLAYER_ENABLE != 0;
}

int prg32_multiplayer_join(const char *cartridge_signature, uint32_t flags) {
    ensure_mp_state();
    if (!prg32_mp_valid_signature(cartridge_signature) ||
        !prg32_multiplayer_available()) {
        return -1;
    }
    if (ensure_network() != 0) {
        return -1;
    }
    if (lock_mp() != 0) {
        return -1;
    }
    g_mp.member_requested = 1;
    g_mp.flags = flags;
    copy_text(g_mp.signature, sizeof(g_mp.signature), cartridge_signature);
    clear_peers();
    unlock_mp();
    if (ensure_client() != 0) {
        if (lock_mp() == 0) {
            g_mp.member_requested = 0;
            unlock_mp();
        }
        return -1;
    }
    /* If the socket is not up yet, the CONNECTED event sends the join. */
    send_join();
    return 0;
}

int prg32_multiplayer_leave(void) {
    /* Stopping a cartridge calls this whether or not the cartridge ever used
     * multiplayer, so an unused service stays unallocated. */
    if (!g_mp.initialized) {
        return 0;
    }
    prg32_mp_link_t link = PRG32_MP_LINK_DISCONNECTED;
    if (lock_mp() == 0) {
        link = g_mp.link;
        g_mp.member_requested = 0;
        g_mp.player_id = 0;
        clear_peers();
        unlock_mp();
    }
    if (link != PRG32_MP_LINK_DISCONNECTED) {
        send_text(PRG32_MP_LEAVE_JSON, (int)strlen(PRG32_MP_LEAVE_JSON));
    }
    /* With no membership to keep, the socket would only reach the server's idle
     * timeout, so it closes here and the next join opens a new session. */
    destroy_client();
    forget_session(PRG32_MP_LINK_DISCONNECTED);
    return 0;
}

void prg32_multiplayer_tick(void) {
    ensure_mp_state();
    if (g_policy_stop) {
        ESP_LOGW(TAG, "blocked by the server: not reconnecting");
        prg32_multiplayer_leave();
        return;
    }

    uint32_t now = prg32_ticks_ms();
    char msg[PRG32_MP_STATE_JSON_MAX];
    int n = -1;
    bool retry_join = false;
    if (lock_mp() == 0) {
        prune_peers(now);
        if (g_mp.member_requested) {
            if (g_mp.link == PRG32_MP_LINK_JOINED) {
                uint32_t elapsed_ms = now - g_mp.last_send_ms;
                if (elapsed_ms >= PRG32_MULTIPLAYER_SEND_PERIOD_MS) {
                    /* Count from the time this send was due: at 30 frames per
                     * second, the gaps then alternate between 33 and 66 ms and
                     * average the protocol's 50 ms. */
                    g_mp.last_send_ms =
                        elapsed_ms < 2 * PRG32_MULTIPLAYER_SEND_PERIOD_MS
                            ? g_mp.last_send_ms + PRG32_MULTIPLAYER_SEND_PERIOD_MS
                            : now;
                    n = prg32_mp_format_state(msg, sizeof(msg), &g_mp.local);
                }
            } else if (g_mp.link != PRG32_MP_LINK_DISCONNECTED &&
                       (uint32_t)(now - g_mp.last_join_ms) >=
                           PRG32_MP_JOIN_RETRY_MS) {
                retry_join = true;
            }
        }
        unlock_mp();
    }
    if (n > 0) {
        send_text(msg, n);
    } else if (retry_join) {
        send_join();
    }
}

uint32_t prg32_multiplayer_local_player_id(void) {
    ensure_mp_state();
    uint32_t player_id = 0;
    if (lock_mp() == 0) {
        if (g_mp.link == PRG32_MP_LINK_JOINED) {
            player_id = g_mp.player_id;
        }
        unlock_mp();
    }
    return player_id;
}

int prg32_multiplayer_set_local_state(int16_t x,
                                      int16_t y,
                                      uint16_t sprite,
                                      uint16_t flags) {
    ensure_mp_state();
    if (lock_mp() != 0) {
        return -1;
    }
    if (!g_mp.member_requested) {
        unlock_mp();
        return -1;
    }
    /* While waiting for `welcome` this only keeps the latest snapshot. The tick
     * transmits it once the session is admitted. */
    g_mp.local.x = x;
    g_mp.local.y = y;
    g_mp.local.sprite = sprite;
    g_mp.local.flags = flags;
    g_mp.local.frame++;
    g_mp.local.last_seen_ms = prg32_ticks_ms();
    unlock_mp();
    return 0;
}

int prg32_multiplayer_set_input(uint32_t input) {
    ensure_mp_state();
    if (lock_mp() != 0) {
        return -1;
    }
    if (!g_mp.member_requested) {
        unlock_mp();
        return -1;
    }
    g_mp.local.input = input & 0x7fu;
    unlock_mp();
    return 0;
}

int prg32_multiplayer_get_peer_count(void) {
    ensure_mp_state();
    int count = 0;
    if (lock_mp() == 0) {
        prune_peers(prg32_ticks_ms());
        count = g_mp.peer_count;
        unlock_mp();
    }
    return count;
}

int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out) {
    ensure_mp_state();
    if (!out || lock_mp() != 0) {
        return -1;
    }
    prune_peers(prg32_ticks_ms());
    if (index < 0 || index >= g_mp.peer_count) {
        unlock_mp();
        return -1;
    }
    *out = g_mp.peers[index];
    unlock_mp();
    return 0;
}

#endif
