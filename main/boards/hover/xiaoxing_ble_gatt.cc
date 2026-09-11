#include "xiaoxing_ble_gatt.h"

#include "sdkconfig.h"

#ifdef CONFIG_BT_NIMBLE_ENABLED

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

#include <cJSON.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "application.h"
#include "mcp_server.h"

static const char* TAG = "XiaoxingGatt";

#define BLE_DEVICE_NAME_PREFIX BOARD_TYPE

static const ble_uuid16_t kServiceUuid = BLE_UUID16_INIT(0xFFA0);
static const ble_uuid16_t kWriteUuid = BLE_UUID16_INIT(0xFFA1);
static const ble_uuid16_t kNotifyUuid = BLE_UUID16_INIT(0xFFA2);

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_notify_handle = 0;
static uint16_t g_write_handle = 0;
static bool g_started = false;
static bool g_delay_armed = false;
static bool g_nimble_from_scratch = false;
static char g_device_name[24] = BOARD_TYPE "0000";
static esp_timer_handle_t g_delay_timer = nullptr;
static esp_timer_handle_t g_idle_stop_timer = nullptr;

static void xiaoxing_advertise();
static void arm_idle_stop();
static void cancel_idle_stop();
static void xiaoxing_notify(const std::string& payload);
static std::string xiaoxing_handle_json(const std::string& body);

static int xiaoxing_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt* ctxt, void* arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return 0;
    }
    if (attr_handle != g_write_handle || ctxt->om == nullptr) {
        return 0;
    }
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > 1024) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    auto* buf = new std::string();
    buf->resize(len);
    os_mbuf_copydata(ctxt->om, 0, len, buf->data());
    Application::GetInstance().Schedule([buf]() {
        std::string response = xiaoxing_handle_json(*buf);
        delete buf;
        xiaoxing_notify(response);
    });
    return 0;
}

static const struct ble_gatt_svc_def g_gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &kNotifyUuid.u,
                    .access_cb = xiaoxing_gatt_access,
                    .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
                    .val_handle = &g_notify_handle,
                },
                {
                    .uuid = &kWriteUuid.u,
                    .access_cb = xiaoxing_gatt_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &g_write_handle,
                },
                {0},
            },
    },
    {0},
};

static int xiaoxing_gap_event(struct ble_gap_event* event, void* arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                g_conn_handle = event->connect.conn_handle;
                cancel_idle_stop();
                ESP_LOGI(TAG, "connected handle=%d", g_conn_handle);
            } else {
                g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                xiaoxing_advertise();
                arm_idle_stop();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "disconnected");
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            xiaoxing_advertise();
            arm_idle_stop();
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            xiaoxing_advertise();
            return 0;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "mtu=%d", event->mtu.value);
            return 0;
        default:
            return 0;
    }
}

static void xiaoxing_advertise() {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<uint8_t*>(g_device_name);
    fields.name_len = strlen(g_device_name);
    fields.name_is_complete = 1;
    fields.uuids16 = &kServiceUuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, xiaoxing_gap_event,
                           NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as %s service=0xFFA0", g_device_name);
    arm_idle_stop();
}

static void idle_stop_cb(void* arg) {
    (void)arg;
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    ESP_LOGW(TAG, "no GATT client for 90s, stop BLE to free SRAM");
    xiaoxing_ble_gatt_stop();
}

static void cancel_idle_stop() {
    if (g_idle_stop_timer != nullptr) {
        esp_timer_stop(g_idle_stop_timer);
    }
}

static void arm_idle_stop() {
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    const esp_timer_create_args_t args = {
        .callback = idle_stop_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "xiaoxing_idle",
        .skip_unhandled_events = true,
    };
    if (g_idle_stop_timer == nullptr) {
        if (esp_timer_create(&args, &g_idle_stop_timer) != ESP_OK) {
            ESP_LOGE(TAG, "idle stop timer create failed");
            return;
        }
    }
    esp_timer_stop(g_idle_stop_timer);
    constexpr uint64_t kIdleStopUs = 90ULL * 1000 * 1000;
    esp_timer_start_once(g_idle_stop_timer, kIdleStopUs);
}

static void xiaoxing_on_sync() {
    ble_svc_gap_device_name_set(g_device_name);
    xiaoxing_advertise();
}

static void xiaoxing_on_reset(int reason) {
    ESP_LOGW(TAG, "nimble reset %d", reason);
}

static void xiaoxing_host_task(void* param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void xiaoxing_notify(const std::string& payload) {
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || g_notify_handle == 0) {
        ESP_LOGW(TAG, "notify skipped, no connection");
        return;
    }
    uint16_t mtu = ble_att_mtu(g_conn_handle);
    size_t chunk = mtu > 8 ? (mtu - 3) : 20;
    size_t offset = 0;
    while (offset < payload.size()) {
        size_t n = std::min(chunk, payload.size() - offset);
        struct os_mbuf* om = ble_hs_mbuf_from_flat(payload.data() + offset, n);
        if (om == nullptr) {
            ESP_LOGE(TAG, "notify mbuf failed");
            return;
        }
        int rc = ble_gatts_notify_custom(g_conn_handle, g_notify_handle, om);
        if (rc != 0) {
            ESP_LOGE(TAG, "notify rc=%d", rc);
            return;
        }
        offset += n;
    }
}

static std::string make_error(int id, const std::string& message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", message.c_str());
    char* printed = cJSON_PrintUnformatted(root);
    std::string out = printed ? printed : "{}";
    cJSON_free(printed);
    cJSON_Delete(root);
    return out;
}

static std::string xiaoxing_handle_json(const std::string& body) {
    ESP_LOGI(TAG, "request %s", body.c_str());
    cJSON* req = cJSON_Parse(body.c_str());
    if (req == nullptr) {
        return make_error(0, "invalid json");
    }
    cJSON* id_item = cJSON_GetObjectItem(req, "id");
    int id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;
    cJSON* method_item = cJSON_GetObjectItem(req, "method");
    if (!cJSON_IsString(method_item)) {
        cJSON_Delete(req);
        return make_error(id, "missing method");
    }
    std::string method = method_item->valuestring;

    std::map<std::string, std::string> args;
    cJSON* params = cJSON_GetObjectItem(req, "params");
    if (cJSON_IsObject(params)) {
        cJSON* child = params->child;
        while (child != nullptr) {
            if (child->string != nullptr) {
                if (cJSON_IsString(child)) {
                    args[child->string] = child->valuestring;
                } else if (cJSON_IsNumber(child)) {
                    args[child->string] = std::to_string(child->valueint);
                } else if (cJSON_IsBool(child)) {
                    args[child->string] = cJSON_IsTrue(child) ? "true" : "false";
                }
            }
            child = child->next;
        }
    }
    cJSON_Delete(req);

    std::string mcp_json;
    try {
        mcp_json = McpServer::GetInstance().CallToolSync(method, args);
    } catch (const std::exception& e) {
        return make_error(id, e.what());
    }

    cJSON* mcp = cJSON_Parse(mcp_json.c_str());
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id", id);
    bool is_error = false;
    if (mcp != nullptr) {
        cJSON* err_flag = cJSON_GetObjectItem(mcp, "isError");
        is_error = cJSON_IsTrue(err_flag);
    }
    cJSON_AddBoolToObject(root, "ok", !is_error);

    cJSON* text_item = nullptr;
    if (mcp != nullptr) {
        cJSON* content = cJSON_GetObjectItem(mcp, "content");
        if (cJSON_IsArray(content) && cJSON_GetArraySize(content) > 0) {
            text_item = cJSON_GetObjectItem(cJSON_GetArrayItem(content, 0), "text");
        }
    }
    if (is_error) {
        cJSON_AddStringToObject(root, "error",
                                cJSON_IsString(text_item) ? text_item->valuestring : "tool error");
    } else if (cJSON_IsString(text_item)) {
        cJSON* parsed = cJSON_Parse(text_item->valuestring);
        if (parsed != nullptr) {
            cJSON_AddItemToObject(root, "result", parsed);
        } else {
            cJSON_AddStringToObject(root, "result", text_item->valuestring);
        }
    } else {
        cJSON_AddTrueToObject(root, "result");
    }
    cJSON_Delete(mcp);

    char* printed = cJSON_PrintUnformatted(root);
    std::string out = printed ? printed : "{}";
    cJSON_free(printed);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "response %s", out.c_str());
    return out;
}

static bool add_gatt_services() {
    int rc = ble_gatts_count_cfg(g_gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "count_cfg rc=%d", rc);
        return false;
    }
    rc = ble_gatts_add_svcs(g_gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "add_svcs rc=%d", rc);
        return false;
    }
    return true;
}

static void delayed_start_cb(void* arg) {
    (void)arg;
    ESP_LOGI(TAG, "delayed start firing, internal=%u psram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    xiaoxing_ble_gatt_start();
}

extern "C" void xiaoxing_ble_gatt_start_delayed(void) {
    if (g_started || g_delay_armed) {
        return;
    }
    const esp_timer_create_args_t args = {
        .callback = delayed_start_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "xiaoxing_gatt",
        .skip_unhandled_events = true,
    };
    if (g_delay_timer == nullptr) {
        if (esp_timer_create(&args, &g_delay_timer) != ESP_OK) {
            ESP_LOGE(TAG, "delay timer create failed, start now");
            xiaoxing_ble_gatt_start();
            return;
        }
    }
    g_delay_armed = true;
    // Wait until MQTT TLS and wake-word AFE have finished allocating.
    constexpr uint64_t kDelayUs = 20ULL * 1000 * 1000;
    ESP_LOGI(TAG, "GATT start delayed 20s, internal=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    esp_timer_start_once(g_delay_timer, kDelayUs);
}

extern "C" void xiaoxing_ble_gatt_start(void) {
    if (g_started) {
        ESP_LOGI(TAG, "already started");
        return;
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(g_device_name, sizeof(g_device_name), "%s%02X%02X", BLE_DEVICE_NAME_PREFIX, mac[4],
             mac[5]);
    ESP_LOGI(TAG, "start as %s internal=%u psram=%u", g_device_name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    esp_bt_controller_status_t bt_status = esp_bt_controller_get_status();
    bool controller_on = bt_status == ESP_BT_CONTROLLER_STATUS_ENABLED;

    if (controller_on) {
        ble_hs_cfg.sync_cb = xiaoxing_on_sync;
        ble_hs_cfg.reset_cb = xiaoxing_on_reset;
        if (!add_gatt_services()) {
            return;
        }
        ble_svc_gap_device_name_set(g_device_name);
        xiaoxing_advertise();
        g_started = true;
        return;
    }

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init rc=%d", rc);
        return;
    }
    ble_hs_cfg.sync_cb = xiaoxing_on_sync;
    ble_hs_cfg.reset_cb = xiaoxing_on_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (!add_gatt_services()) {
        return;
    }
    ble_svc_gap_device_name_set(g_device_name);
    nimble_port_freertos_init(xiaoxing_host_task);
    g_nimble_from_scratch = true;
    g_started = true;
}

extern "C" void xiaoxing_ble_gatt_stop(void) {
    if (g_delay_timer != nullptr) {
        esp_timer_stop(g_delay_timer);
    }
    cancel_idle_stop();
    g_delay_armed = false;
    if (!g_started) {
        return;
    }
    ESP_LOGI(TAG, "stop, internal=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ble_gap_adv_stop();
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    if (g_nimble_from_scratch) {
        int rc = nimble_port_stop();
        if (rc == 0) {
            nimble_port_deinit();
        } else {
            ESP_LOGW(TAG, "nimble_port_stop rc=%d", rc);
        }
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        g_nimble_from_scratch = false;
    }
    g_started = false;
    ESP_LOGI(TAG, "stopped, internal=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

#else

extern "C" void xiaoxing_ble_gatt_start(void) {}
extern "C" void xiaoxing_ble_gatt_start_delayed(void) {}
extern "C" void xiaoxing_ble_gatt_stop(void) {}

#endif
