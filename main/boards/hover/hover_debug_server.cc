#include "hover_debug_server.h"
#include "xgo.h"
#include "imu.h"
#include "mcp_server.h"
#include "board.h"
#include "display.h"
#include "display/emote_display.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <cJSON.h>
#include <ctype.h>
#include <string.h>
#include <string>
#include <map>
#include <stdio.h>
#include <stdexcept>
#include <lwip/sockets.h>
#include <freertos/semphr.h>
#include <wifi_manager.h>
#include "application.h"

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
    <div class="subtitle">RIG-Hover · 实时姿态与平衡参数（每 0.5 秒自动刷新）· <a href="/mcp" style="color:#00d9ff">MCP 工具控制台 →</a> · <a href="/emoji" style="color:#00d9ff">表情管理 →</a></div>

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

// /mcp - MCP 工具控制台页面
static const char* MCP_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MCP 工具控制台</title>
    <style>
        body { font-family: -apple-system, "PingFang SC", Arial, sans-serif; max-width: 900px; margin: 0 auto; padding: 20px; background: #1a1a2e; color: #eee; }
        h1 { color: #00d9ff; text-align: center; font-size: 22px; }
        .subtitle { text-align: center; color: #8899aa; font-size: 13px; margin-bottom: 8px; }
        .subtitle a { color: #00d9ff; }
        .toolbar { display: flex; gap: 10px; margin: 12px 0; }
        .toolbar input { flex: 1; padding: 8px 12px; border: 1px solid #0f3460; border-radius: 5px; background: #16213e; color: #fff; font-size: 14px; }
        .toolbar button { padding: 8px 16px; background: #00d9ff; border: none; border-radius: 5px; color: #000; cursor: pointer; font-weight: bold; }
        .group-title { color: #e9a560; font-size: 14px; margin: 14px 0 6px; font-weight: bold; }
        .tool { background: #16213e; border-radius: 8px; padding: 12px 14px; margin: 8px 0; border-left: 3px solid #00d9ff; }
        .tool.robot { border-left-color: #00c853; }
        .tool .name { color: #00d9ff; font-weight: bold; font-family: monospace; font-size: 14px; cursor: pointer; }
        .tool.robot .name { color: #00c853; }
        .tool .desc { color: #8899aa; font-size: 12px; margin-top: 4px; white-space: pre-wrap; }
        .params { margin-top: 8px; display: none; background: #0f3460; padding: 10px; border-radius: 6px; }
        .params .param { display: flex; align-items: center; gap: 8px; margin: 6px 0; flex-wrap: wrap; }
        .params .param label { min-width: 130px; color: #fff; font-family: monospace; font-size: 13px; }
        .params .param .meta { color: #667788; font-size: 11px; }
        .params .param input { width: 160px; padding: 6px; border: 1px solid #16213e; border-radius: 4px; background: #16213e; color: #fff; font-size: 14px; }
        .params button.run { margin-top: 8px; padding: 8px 20px; background: #00c853; border: none; border-radius: 5px; color: #000; cursor: pointer; font-weight: bold; }
        .params button.run:hover { background: #00a844; }
        .result { margin-top: 8px; padding: 8px; background: #000; border-radius: 4px; color: #0f0; font-family: monospace; font-size: 12px; white-space: pre-wrap; word-break: break-all; }
        .result.err { color: #e94560; }
        .status { text-align: center; padding: 10px; color: #0f0; font-size: 14px; min-height: 20px; }
        .empty { text-align: center; color: #8899aa; padding: 30px; }
    </style>
</head>
<body>
    <h1>MCP 工具控制台</h1>
    <div class="subtitle">设备本地已注册的全部 MCP 工具 · <b>点击工具名 ▸ 展开参数表单并执行</b> · <a href="/">← 返回调试台</a> · <a href="/emoji">表情管理 →</a></div>
    <div class="toolbar">
        <input id="search" placeholder="搜索工具名或描述…" oninput="render()">
        <button onclick="loadTools()">刷新</button>
    </div>
    <div class="group-title" id="robot-title" style="display:none">🤖 机器人控制（会动！请扶稳放平）</div>
    <div id="robot-list"></div>
    <div class="group-title" id="common-title" style="display:none">🔧 设备功能</div>
    <div id="common-list"></div>
    <div class="empty" id="empty" style="display:none">没有匹配的工具</div>
    <div class="status" id="status">加载中…</div>

    <script>
        let TOOLS = [];

        function esc(s) {
            return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
        }

        // 固件返回的是 inputSchema.properties（按名字索引的对象），转成数组并标注必填
        function toolParams(t) {
            const schema = t.inputSchema || {};
            const props = schema.properties || {};
            const required = schema.required || [];
            return Object.keys(props).map(n => Object.assign({ name: n, required: required.indexOf(n) >= 0 }, props[n]));
        }

        function loadTools() {
            document.getElementById('status').textContent = '加载中…';
            fetch('/api/mcp/tools')
                .then(r => r.json())
                .then(data => { TOOLS = data.tools || []; render(); document.getElementById('status').textContent = '共 ' + TOOLS.length + ' 个工具'; })
                .catch(e => { document.getElementById('status').textContent = '错误: ' + e.message; });
        }

        function inputFor(p, toolName) {
            const id = 'p_' + toolName + '_' + p.name;
            let inp;
            if (p.type === 'boolean') {
                inp = `<select id="${id}"><option value="true">true</option><option value="false">false</option></select>`;
            } else if (p.type === 'integer') {
                inp = `<input type="number" id="${id}" placeholder="${p.default !== undefined ? p.default : ''}">`;
            } else {
                inp = `<input type="text" id="${id}" placeholder="${p.default !== undefined ? esc(String(p.default)) : ''}">`;
            }
            let meta = p.type;
            if (p.minimum !== undefined) meta += ` (${p.minimum} ~ ${p.maximum})`;
            else if (p.enum) meta += ` (${p.enum.join(' | ')})`;
            return `<div class="param"><label>${esc(p.name)}</label>${inp}<span class="meta">${meta}${p.required ? ' ·必填' : ''}</span></div>`;
        }

        // 展开当前工具的参数面板（执行按钮在面板里）
        function toggleParams(nameEl) {
            const params = nameEl.closest('.tool').querySelector('.params');
            params.style.display = params.style.display === 'block' ? 'none' : 'block';
        }

        function render() {
            const q = document.getElementById('search').value.toLowerCase();
            const robotList = document.getElementById('robot-list');
            const commonList = document.getElementById('common-list');
            robotList.innerHTML = ''; commonList.innerHTML = '';
            let robotCount = 0, commonCount = 0;

            TOOLS.forEach(t => {
                const text = (t.name + ' ' + t.description).toLowerCase();
                if (q && !text.includes(q)) return;
                const isRobot = t.name.startsWith('self.robot') || t.name.startsWith('self.dog');
                const div = document.createElement('div');
                div.className = 'tool' + (isRobot ? ' robot' : '');
                const props = toolParams(t);
                const params = props.map(p => inputFor(p, t.name)).join('');
                div.innerHTML = `
                    <div class="name" onclick="toggleParams(this)">▸ ${esc(t.name)}</div>
                    <div class="desc">${esc(t.description)}</div>
                    <div class="params">
                        ${params || '<div class="meta" style="color:#667788;font-size:12px">无参数，直接执行</div>'}
                        <button class="run" onclick="runTool('${esc(t.name)}')">▶ 执行</button>
                        <div class="result" id="result_${esc(t.name)}" style="display:none"></div>
                    </div>
                `;
                if (isRobot) { robotList.appendChild(div); robotCount++; }
                else { commonList.appendChild(div); commonCount++; }
            });

            document.getElementById('robot-title').style.display = robotCount ? 'block' : 'none';
            document.getElementById('common-title').style.display = commonCount ? 'block' : 'none';
            document.getElementById('empty').style.display = (robotCount + commonCount) ? 'none' : 'block';
        }

        function runTool(name) {
            const t = TOOLS.find(x => x.name === name);
            const paramNames = t ? toolParams(t).map(p => p.name) : [];
            const args = {};
            paramNames.forEach(n => {
                const el = document.getElementById('p_' + name + '_' + n);
                if (el && el.value !== '') args[n] = el.value;
            });
            const resultDiv = document.getElementById('result_' + name);
            resultDiv.style.display = 'block';
            resultDiv.className = 'result';
            resultDiv.textContent = '执行中…';
            fetch('/api/mcp/call?name=' + encodeURIComponent(name) + '&args=' + encodeURIComponent(JSON.stringify(args)))
                .then(r => r.json())
                .then(d => {
                    let text = d.ok ? (d.result || '') : ('❌ ' + d.error);
                    try { const j = JSON.parse(d.result); text = d.ok ? JSON.stringify(j, null, 2) : text; } catch (e) {}
                    resultDiv.className = 'result' + (d.ok ? '' : ' err');
                    resultDiv.textContent = text;
                })
                .catch(e => { resultDiv.className = 'result err'; resultDiv.textContent = '❌ ' + e.message; });
        }

        loadTools();
    </script>
</body>
</html>
)rawliteral";

// /emoji - 表情管理页面
static const char* EMOJI_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>表情管理</title>
    <style>
        body { font-family: -apple-system, "PingFang SC", Arial, sans-serif; max-width: 900px; margin: 0 auto; padding: 20px; background: #1a1a2e; color: #eee; }
        h1 { color: #00d9ff; text-align: center; font-size: 22px; }
        .subtitle { text-align: center; color: #8899aa; font-size: 13px; margin-bottom: 8px; }
        .subtitle a { color: #00d9ff; }
        .hint { background: #16213e; border-left: 3px solid #e9a560; padding: 10px 14px; border-radius: 6px; color: #e9a560; font-size: 13px; margin: 12px 0; }
        .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 12px; margin-top: 14px; }
        .card { background: #16213e; border-radius: 10px; padding: 14px; text-align: center; cursor: pointer; border: 2px solid transparent; transition: all .15s; }
        .card:hover { border-color: #00d9ff; transform: translateY(-2px); }
        .card.playing { border-color: #00c853; background: #0d2818; }
        .card .name { color: #00d9ff; font-family: monospace; font-size: 14px; font-weight: bold; word-break: break-all; }
        .card .meta { color: #667788; font-size: 11px; margin-top: 6px; }
        .card .icon { font-size: 30px; margin-bottom: 6px; }
        .status { text-align: center; padding: 10px; color: #0f0; font-size: 14px; min-height: 20px; }
        .empty { text-align: center; color: #8899aa; padding: 30px; }
        .actions { display: flex; gap: 10px; justify-content: center; margin: 10px 0; flex-wrap: wrap; }
        .actions button { padding: 8px 16px; background: #0f3460; border: 1px solid #00d9ff; border-radius: 5px; color: #00d9ff; cursor: pointer; font-size: 13px; }
        .actions button:hover { background: #16213e; }
    </style>
</head>
<body>
    <h1>表情管理</h1>
    <div class="subtitle">设备 assets 中的全部 EAF 表情 · 点击卡片在机器人屏幕上预览 · <a href="/">← 返回调试台</a></div>
    <div class="hint">💡 屏幕上正在播放的会是系统当前表情。聊天中 AI 通过 <code>self.emoji.show</code> 工具也能触发这些表情；下一步语音对话或状态变化可能会覆盖你点的表情，属正常现象。</div>
    <div class="actions">
        <button onclick="playNeutral()">▶ 回到默认表情 (neutral)</button>
        <button onclick="loadEmojis()">↻ 刷新列表</button>
    </div>
    <div class="grid" id="grid"></div>
    <div class="empty" id="empty" style="display:none">没有加载到表情（assets 未挂载？）</div>
    <div class="status" id="status">加载中…</div>

    <script>
        let EMOJIS = [];
        // 常见表情配个小图示（纯装饰，帮助辨认）
        const ICONS = { happy:'😄', sad:'😢', angry:'😠', surprised:'😮', thinking:'🤔', winking:'😉', cool:'😎', laughing:'😂', loving:'😍', crying:'😭', confused:'😕', embarrassed:'😳', sleepy:'😴', confident:'😎', delicious:'🤤', kiss:'😘', relaxed:'😊', shocked:'😱', silly:'🤪', neutral:'😐', listen:'👂', sleepy2:'😴', launch:'🚀', wificonfig:'📶', scanning:'🔍', calibration:'🛠', remote_mode:'🎮', nvs_reset:'♻️' };

        function esc(s) { return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

        function loadEmojis() {
            document.getElementById('status').textContent = '加载中…';
            fetch('/api/emoji/list')
                .then(r => r.json())
                .then(data => {
                    EMOJIS = data.emojis || [];
                    render();
                    document.getElementById('status').textContent = '共 ' + EMOJIS.length + ' 个表情';
                })
                .catch(e => { document.getElementById('status').textContent = '错误: ' + e.message; });
        }

        function render() {
            const grid = document.getElementById('grid');
            grid.innerHTML = '';
            EMOJIS.forEach(e => {
                const div = document.createElement('div');
                div.className = 'card';
                div.id = 'card_' + e.name;
                div.onclick = () => play(e.name);
                div.innerHTML = `
                    <div class="icon">${ICONS[e.name] || '🎬'}</div>
                    <div class="name">${esc(e.name)}</div>
                    <div class="meta">${e.loop ? '循环' : '单次'}${e.fps ? ' · ' + e.fps + 'fps' : ''}</div>
                `;
                grid.appendChild(div);
            });
            document.getElementById('empty').style.display = EMOJIS.length ? 'none' : 'block';
        }

        function play(name) {
            document.getElementById('status').textContent = '正在播放 ' + name + ' …';
            document.querySelectorAll('.card').forEach(c => c.classList.remove('playing'));
            fetch('/api/emoji/play?name=' + encodeURIComponent(name))
                .then(r => r.json())
                .then(d => {
                    if (d.ok) {
                        const card = document.getElementById('card_' + name);
                        if (card) card.classList.add('playing');
                        document.getElementById('status').textContent = '▶ 屏幕上正在播放: ' + name;
                    } else {
                        document.getElementById('status').textContent = '❌ ' + d.error;
                    }
                })
                .catch(e => { document.getElementById('status').textContent = '错误: ' + e.message; });
        }

        function playNeutral() { play('neutral'); }

        loadEmojis();
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

// GET /mcp - MCP 工具控制台页面
static esp_err_t mcp_index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, MCP_HTML, strlen(MCP_HTML));
    return ESP_OK;
}

// GET /api/mcp/tools - 枚举所有 MCP 工具（含参数 schema）
static esp_err_t mcp_tools_handler(httpd_req_t *req) {
    auto& mcp = McpServer::GetInstance();
    cJSON *root = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    for (const auto* tool : mcp.GetTools()) {
        cJSON *tool_json = cJSON_Parse(tool->to_json().c_str());
        cJSON_AddItemToArray(tools, tool_json);
    }
    cJSON_AddItemToObject(root, "tools", tools);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// URL 百分号解码（esp_http_server 的 query 参数返回的是未解码原文）
static void url_decode(const char* src, char* dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_size; i++) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

// GET /api/mcp/call?name=self.xxx&args={"angle":10} - 同步执行一个 MCP 工具
static esp_err_t mcp_call_handler(httpd_req_t *req) {
    char buf[512];
    int buf_len = httpd_req_get_url_query_len(req) + 1;
    std::string reply;

    if (buf_len > 1 && buf_len < (int)sizeof(buf)) {
        httpd_req_get_url_query_str(req, buf, buf_len);

        char raw_name[96] = {0};
        char raw_args[384] = {0};
        char name[96] = {0};
        char args_str[384] = {0};
        httpd_query_key_value(buf, "name", raw_name, sizeof(raw_name));
        httpd_query_key_value(buf, "args", raw_args, sizeof(raw_args));
        url_decode(raw_name, name, sizeof(name));
        url_decode(raw_args, args_str, sizeof(args_str));

        std::map<std::string, std::string> args;
        cJSON *args_json = cJSON_Parse(args_str);
        if (args_json != nullptr) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, args_json) {
                char *v = cJSON_Print(item);
                if (v != NULL) {
                    // 去掉字符串两端的引号（cJSON_Print 会带引号）
                    std::string value = v;
                    cJSON_free(v);
                    if (value.size() >= 2 && value.front() == '\"' && value.back() == '\"') {
                        value = value.substr(1, value.size() - 2);
                    }
                    args[item->string] = value;
                }
            }
        }
        cJSON_Delete(args_json);

        try {
            struct McpJob {
                std::string name;
                std::map<std::string, std::string> args;
                std::string result;
                std::string error;
                SemaphoreHandle_t sem;
            };
            auto* job = new McpJob{name, args, "", "", xSemaphoreCreateBinary()};
            Application::GetInstance().Schedule([job]() {
                try {
                    job->result = McpServer::GetInstance().CallToolSync(job->name, job->args);
                } catch (const std::exception& e) {
                    job->error = e.what();
                }
                xSemaphoreGive(job->sem);
            });
            if (xSemaphoreTake(job->sem, pdMS_TO_TICKS(4000)) != pdTRUE) {
                throw std::runtime_error("mcp timeout");
            }
            if (!job->error.empty()) {
                std::string err = job->error;
                vSemaphoreDelete(job->sem);
                delete job;
                throw std::runtime_error(err);
            }
            reply = "{\"ok\":true,\"result\":";
            reply += job->result;
            reply += "}";
            vSemaphoreDelete(job->sem);
            delete job;
        } catch (const std::exception& e) {
            cJSON *err = cJSON_CreateString(e.what());
            char *err_str = cJSON_PrintUnformatted(err);
            reply = "{\"ok\":false,\"error\":";
            reply += err_str;
            reply += "}";
            cJSON_free(err_str);
            cJSON_Delete(err);
        }
    } else {
        reply = "{\"ok\":false,\"error\":\"bad query\"}";
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, reply.c_str(), reply.length());
    return ESP_OK;
}

// GET /emoji - 表情管理页面
static esp_err_t emoji_index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, EMOJI_HTML, strlen(EMOJI_HTML));
    return ESP_OK;
}

// GET /api/emoji/list - 枚举 assets 中的所有表情
static esp_err_t emoji_list_handler(httpd_req_t *req) {
    auto display = Board::GetInstance().GetDisplay();
    auto* emote_display = dynamic_cast<emote::EmoteDisplay*>(display);

    std::string reply;
    if (emote_display == nullptr) {
        reply = "{\"emojis\":[],\"error\":\"EmoteDisplay not available\"}";
    } else {
        std::string list = emote_display->GetEmojiListJson();
        if (list.empty()) {
            reply = "{\"emojis\":[],\"error\":\"index.json not found or parse failed\"}";
        } else {
            reply = "{\"emojis\":";
            reply += list;
            reply += "}";
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, reply.c_str(), reply.length());
    return ESP_OK;
}

// GET /api/emoji/play?name=happy - 在屏幕上播放指定表情
static esp_err_t emoji_play_handler(httpd_req_t *req) {
    char buf[128];
    std::string reply;

    int buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1 && buf_len < (int)sizeof(buf)) {
        httpd_req_get_url_query_str(req, buf, buf_len);
        char name[64] = {0};
        httpd_query_key_value(buf, "name", name, sizeof(name));

        if (name[0] == '\0') {
            reply = "{\"ok\":false,\"error\":\"missing name\"}";
        } else {
            auto display = Board::GetInstance().GetDisplay();
            if (display == nullptr) {
                reply = "{\"ok\":false,\"error\":\"display not available\"}";
            } else {
                display->SetEmotion(name);
                reply = "{\"ok\":true}";
            }
        }
    } else {
        reply = "{\"ok\":false,\"error\":\"bad query\"}";
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, reply.c_str(), reply.length());
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

static void xiaoxing_beacon_task(void* arg) {
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "beacon socket failed");
        vTaskDelete(NULL);
        return;
    }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char name[24];
    snprintf(name, sizeof(name), "%s%02X%02X", BOARD_TYPE, mac[4], mac[5]);
    struct sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(3650);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    ESP_LOGI(TAG, "UDP beacon %s on port 3650", name);
    while (true) {
        std::string ip = WifiManager::GetInstance().GetIpAddress();
        if (!ip.empty()) {
            char msg[192];
            snprintf(msg, sizeof(msg), "XIAOXING ip=%s name=%s\n", ip.c_str(), name);
            sendto(sock, msg, strlen(msg), 0, reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void hover_debug_server_start(void) {
    if (server != NULL) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 12;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return;
    }

    httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t uri_data = { .uri = "/api/data", .method = HTTP_GET, .handler = data_handler };
    httpd_uri_t uri_set = { .uri = "/api/set", .method = HTTP_GET, .handler = set_handler };
    httpd_uri_t uri_mcp_index = { .uri = "/mcp", .method = HTTP_GET, .handler = mcp_index_handler };
    httpd_uri_t uri_mcp_tools = { .uri = "/api/mcp/tools", .method = HTTP_GET, .handler = mcp_tools_handler };
    httpd_uri_t uri_mcp_call = { .uri = "/api/mcp/call", .method = HTTP_GET, .handler = mcp_call_handler };
    httpd_uri_t uri_emoji_index = { .uri = "/emoji", .method = HTTP_GET, .handler = emoji_index_handler };
    httpd_uri_t uri_emoji_list = { .uri = "/api/emoji/list", .method = HTTP_GET, .handler = emoji_list_handler };
    httpd_uri_t uri_emoji_play = { .uri = "/api/emoji/play", .method = HTTP_GET, .handler = emoji_play_handler };

    httpd_register_uri_handler(server, &uri_index);
    httpd_register_uri_handler(server, &uri_data);
    httpd_register_uri_handler(server, &uri_set);
    httpd_register_uri_handler(server, &uri_mcp_index);
    httpd_register_uri_handler(server, &uri_mcp_tools);
    httpd_register_uri_handler(server, &uri_mcp_call);
    httpd_register_uri_handler(server, &uri_emoji_index);
    httpd_register_uri_handler(server, &uri_emoji_list);
    httpd_register_uri_handler(server, &uri_emoji_play);

    ESP_LOGI(TAG, "Debug server started on port 80");
    xTaskCreate(xiaoxing_beacon_task, "xiaoxing_bcn", 3072, NULL, 3, NULL);
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
