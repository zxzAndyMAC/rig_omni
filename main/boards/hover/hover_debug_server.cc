#include "hover_debug_server.h"
#include "xgo.h"
#include "imu.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <cJSON.h>
#include <string.h>
#include <stdio.h>

static const char* TAG = "HoverDebug";

#define DEBUG_VAR_COUNT 20

float debug_var[DEBUG_VAR_COUNT] = {0};

static httpd_handle_t server = NULL;

// HTML页面
static const char* INDEX_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Hover 调试台</title>
    <style>
        body { font-family: -apple-system, "PingFang SC", Arial, sans-serif; max-width: 860px; margin: 0 auto; padding: 20px; background: #1a1a2e; color: #eee; }
        h1 { color: #00d9ff; text-align: center; font-size: 22px; }
        .subtitle { text-align: center; color: #8899aa; font-size: 13px; margin-bottom: 8px; }
        .section { background: #16213e; border-radius: 12px; padding: 16px; margin: 15px 0; }
        .section h2 { color: #00d9ff; margin-top: 0; font-size: 17px; }
        .hint { color: #8899aa; font-size: 12px; margin: 2px 0 10px; }
        .imu-data { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
        .imu-item { background: #0f3460; padding: 15px; border-radius: 8px; text-align: center; }
        .imu-item .label { color: #8899aa; font-size: 12px; }
        .imu-item .value { font-size: 26px; font-weight: bold; color: #00d9ff; }
        .imu-item .desc { color: #667788; font-size: 11px; margin-top: 4px; }
        .var-grid { display: grid; grid-template-columns: 1fr; gap: 10px; }
        .var-item { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
        .var-item label { min-width: 150px; }
        .var-item .name { color: #00d9ff; font-weight: bold; font-size: 14px; }
        .var-item .desc { color: #8899aa; font-size: 12px; flex: 1; min-width: 200px; }
        .var-item input { width: 110px; padding: 8px; border: 1px solid #0f3460; border-radius: 5px; background: #0f3460; color: #fff; font-size: 15px; }
        .var-item button { padding: 8px 16px; background: #00d9ff; border: none; border-radius: 5px; color: #000; cursor: pointer; font-weight: bold; }
        .var-item button:hover { background: #00b8d4; }
        .var-item.locked input, .var-item.locked button { opacity: 0.35; pointer-events: none; }
        .lock-row { display: flex; align-items: center; gap: 8px; margin: 6px 0 12px; color: #e9a560; font-size: 13px; }
        .danger { border: 1px solid #e94560; }
        .danger h2 { color: #e94560; }
        .status { text-align: center; padding: 10px; color: #0f0; font-size: 14px; min-height: 20px; }
    </style>
</head>
<body>
    <h1>Hover 调试台</h1>
    <div class="subtitle">RIG-Hover · 实时姿态与平衡参数（每 0.5 秒自动刷新）</div>

    <div class="section">
        <h2>📐 身体姿态（只读）</h2>
        <div class="hint">静置桌面时 Roll / Pitch 应接近 0；前后扶动可看到 Pitch 变化 —— 这就是它感知「要摔倒」的方式。</div>
        <div class="imu-data">
            <div class="imu-item"><div class="label">Roll 左右倾斜°</div><div class="value" id="roll">--</div><div class="desc">往左/右歪</div></div>
            <div class="imu-item"><div class="label">Pitch 前后俯仰°</div><div class="value" id="pitch">--</div><div class="desc">⭐ 平衡关键量</div></div>
            <div class="imu-item"><div class="label">Yaw 朝向°</div><div class="value" id="yaw">--</div><div class="desc">转了多少度</div></div>
        </div>
    </div>

    <div class="section">
        <h2>🟢 安全区（新手可调）</h2>
        <div class="hint">手扶设备、小步调整。每次改一点，观察反应。</div>
        <div class="var-grid" id="safe-grid"></div>
    </div>

    <div class="section danger">
        <h2>🔴 危险区（会摔车！）</h2>
        <div class="lock-row">
            <input type="checkbox" id="unlock" onchange="toggleLock()">
            <label for="unlock">我已扶稳设备，知道这些参数会让平衡立刻改变</label>
        </div>
        <div class="var-grid" id="danger-grid"></div>
    </div>

    <div class="status" id="status">就绪</div>

    <script>
        // [索引, 名称, 说明, 步长, 默认锁定]
        const SAFE_VARS = [
            [8,  'IMU零点 (imu_zero)', '⭐ 最常调：放平后 Pitch 不为 0 时微调它归零，±0.5 小步', 0.1],
            [0,  '头部角度 (head)',     '头部舵机目标角度，正负代表左右', 5],
            [9,  '转向增量 (delta_yaw)', '车身原地转一点（累计生效）', 1],
            [1,  '位置增量 (delta_pos)', '整体前进/后退一点（累计生效）', 0.1],
        ];
        const DANGER_VARS = [
            [10, 'LQR_k0', '平衡总增益1（默认1400）', 10],
            [11, 'LQR_k1', '平衡总增益2（默认6.8）', 0.1],
            [12, 'LQR_k2', '平衡总增益3（默认32）', 0.5],
            [13, 'LQR_k3', '平衡总增益4（默认1.6）', 0.05],
            [2,  'POS_kp', '位置环 P（默认60）', 1],
            [4,  'VEL_kp', '速度环 P（默认0.15）', 0.01],
            [5,  'VEL_ki', '速度环 I（默认0.01）', 0.005],
            [6,  'PIT_kp', '俯仰环 P（默认21）', 0.5],
            [7,  'PIT_kd', '俯仰阻尼（默认0.7）', 0.05],
        ];

        function buildGrid(gridId, vars) {
            const grid = document.getElementById(gridId);
            vars.forEach(([i, name, desc, step]) => {
                const div = document.createElement('div');
                div.className = 'var-item';
                div.id = 'row' + i;
                div.innerHTML = `
                    <label><span class="name">${name}</span></label>
                    <span class="desc">${desc}</span>
                    <input type="number" step="${step}" id="var${i}" value="0">
                    <button onclick="setVar(${i})">设置</button>
                `;
                grid.appendChild(div);
            });
        }

        function toggleLock() {
            const unlocked = document.getElementById('unlock').checked;
            DANGER_VARS.forEach(([i]) => {
                document.getElementById('row' + i).classList.toggle('locked', !unlocked);
            });
        }

        function fetchData() {
            fetch('/api/data')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('roll').textContent = data.imu.roll.toFixed(2);
                    document.getElementById('pitch').textContent = data.imu.pitch.toFixed(2);
                    document.getElementById('yaw').textContent = data.imu.yaw.toFixed(2);
                    document.getElementById('status').textContent = '已更新 ' + new Date().toLocaleTimeString();
                })
                .catch(e => { document.getElementById('status').textContent = '错误: ' + e.message; });
        }

        function setVar(index) {
            const value = parseFloat(document.getElementById('var' + index).value);
            if (isNaN(value)) return;
            // 温和范围保护（防手滑输爆）
            const limits = {0:200, 1:2, 8:5, 9:30};
            if (index in limits && Math.abs(value) > limits[index]) {
                document.getElementById('status').textContent = '⚠ 值超出安全范围，请小步调整';
                return;
            }
            fetch('/api/set?i=' + index + '&v=' + value)
                .then(r => r.json())
                .then(() => { document.getElementById('status').textContent = '✅ 已设置 [' + index + '] = ' + value; })
                .catch(e => { document.getElementById('status').textContent = '错误: ' + e.message; });
        }

        buildGrid('safe-grid', SAFE_VARS);
        buildGrid('danger-grid', DANGER_VARS);
        toggleLock();
        fetchData();
        setInterval(fetchData, 500);
    </script>
</body>
</html>
)rawliteral";

// GET / - 返回HTML页面
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
    return ESP_OK;
}

// GET /api/data - 返回IMU和变量数据
static esp_err_t data_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    cJSON *imu = cJSON_CreateObject();
    cJSON_AddNumberToObject(imu, "roll", roll);
    cJSON_AddNumberToObject(imu, "pitch", pitch);
    cJSON_AddNumberToObject(imu, "yaw", yaw);
    cJSON_AddItemToObject(root, "imu", imu);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// GET /api/set?i=0&v=1.5 - 设置变量
static esp_err_t set_handler(httpd_req_t *req) {
    char buf[100];
    int buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1 && buf_len < sizeof(buf)) {
        httpd_req_get_url_query_str(req, buf, buf_len);

        char param[32];
        int index = -1;
        float value = 0.0f;

        if (httpd_query_key_value(buf, "i", param, sizeof(param)) == ESP_OK) {
            index = atoi(param);
        }
        if (httpd_query_key_value(buf, "v", param, sizeof(param)) == ESP_OK) {
            value = atof(param);
        }

        switch (index) {
            case 0:
                target_head_pos = value;
                break;
            case 1:
                stable_pos = stable_pos + value;
                break;
            case 2:
                pid_pos.fpKp = value;
                break;
            case 3:
                pid_pos.fpKd = value;
                break;
            case 4:
                pid_vel.fpKp = value;
                break;
            case 5:
                pid_vel.fpKi = value;
                break;
            case 6:
                pid_pit.fpKp = value;
                break;
            case 7:
                kd_pit = value;
                break;
            case 8:
                imu_zero = value;
                break;
            case 9:
                stable_yaw = stable_yaw + value;
                break;
            case 10:
                lqr_k[0] = value;
                break;
            case 11:
                lqr_k[1] = value;
                break;
            case 12:
                lqr_k[2] = value;
                break;
            case 13:
                lqr_k[3] = value;
                break;
            default:
                break;
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

void hover_debug_server_start(void) {
    if (server != NULL) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return;
    }

    httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t uri_data = { .uri = "/api/data", .method = HTTP_GET, .handler = data_handler };
    httpd_uri_t uri_set = { .uri = "/api/set", .method = HTTP_GET, .handler = set_handler };

    httpd_register_uri_handler(server, &uri_index);
    httpd_register_uri_handler(server, &uri_data);
    httpd_register_uri_handler(server, &uri_set);

    ESP_LOGI(TAG, "Debug server started on port 80");
}

void hover_debug_server_stop(void) {
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Debug server stopped");
    }
}

bool hover_debug_server_is_running(void) {
    return server != NULL;
}
