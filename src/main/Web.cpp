#include "Web.h"

#include <Arduino.h>
#include <Json.h>
#include <WebServer.h>
#include <esp_system.h>
#include <string_view>

namespace {

WebServer server(80);
core::Basket* bk = nullptr;
std::string hw;
std::string telemetryPrefix;
DeviceSignals getSignals;
SetLid changeLid;

const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>智慧 Ubike 籃子</title>
<style>
:root{--bg:#f4f6f8;--card:#fff;--fg:#1d2329;--mute:#6b7680;--line:#e2e6ea;--ok:#1a7f4b;--warn:#b35c00;--bad:#c62828;--accent:#f5a623}
@media (prefers-color-scheme:dark){:root{--bg:#121417;--card:#1c2025;--fg:#e8ebee;--mute:#9aa4ad;--line:#2c3238}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,-apple-system,"Noto Sans TC",sans-serif}
header{background:var(--accent);color:#222;padding:12px 16px;font-weight:700;font-size:18px}
main{max-width:900px;margin:auto;padding:12px 16px;display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(280px,1fr))}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px}
h2{margin:0 0 8px;font-size:15px;color:var(--mute)}
.big{font-size:22px;font-weight:700}.mute{color:var(--mute)}
.tag{display:inline-block;padding:1px 8px;border-radius:99px;font-size:13px;margin:2px 4px 2px 0;border:1px solid}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
.oled{background:#000;color:#8fd3ff;font-family:ui-monospace,monospace;padding:8px;border-radius:6px;white-space:pre;min-height:110px}
table{width:100%;border-collapse:collapse}td,th{padding:4px 2px;border-bottom:1px solid var(--line);text-align:left}
td.n,th.n{text-align:right;font-variant-numeric:tabular-nums}
button{font:inherit;padding:6px 12px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button.trash{background:var(--bad);color:#fff;border:0}button.mine{background:var(--ok);color:#fff;border:0}
.pend{border:1px solid var(--warn);border-radius:8px;padding:8px;margin-bottom:8px}
#events{max-height:320px;overflow:auto;font-size:14px}
input{width:80px;font:inherit;padding:3px;background:var(--bg);color:var(--fg);border:1px solid var(--line);border-radius:4px}
.wide{grid-column:1/-1}
</style></head><body>
<header>智慧 Ubike 籃子</header>
<main>
<section><h2>目前狀態</h2><div id="state" class="big">連線中…</div><div id="marks"></div><div id="sensors" class="mute"></div></section>
<section><h2>車籃訊息</h2><div id="oled" class="oled"></div>
<p id="lid-controls" hidden>手動回報蓋子
<button onclick="post('/api/lid',{closed:'0'})">已打開</button>
<button onclick="post('/api/lid',{closed:'1'})">已關上</button></p></section>
<section class="wide"><h2>待確認（上一位使用者請確認籃內物品）</h2><div id="pending" class="mute">沒有待確認的物品</div></section>
<section><h2>帳戶餘額</h2><table id="accounts"></table></section>
<section><h2>設定</h2><table id="config"></table>
<p><button onclick="if(confirm('請確認籃子已清空、蓋子已關好，且目前無人借車。要記錄空籃基準嗎？'))post('/api/calibrate')">記錄空籃基準</button></p><p class="mute" id="hw"></p></section>
<section class="wide"><h2>事件紀錄</h2><div id="events"></div></section>
</main>
<script>
const NAMES={penalty:'罰款（元）',reward:'回饋金（元）',initial:'新卡初始餘額',countdown_s:'倒數秒數',timeout_s:'確認時限（秒）',weight_g:'重量門檻（g）',dist_mm:'測距門檻（mm）',lock_ms:'開鎖通電（ms）'};
const STATES={boot:'開機中',idle:'等待借車',riding:'借車中',countdown:'倒數中：請取出物品'};
const $=id=>document.getElementById(id);
const el=(tag,text,cls)=>{const e=document.createElement(tag);if(text!=null)e.textContent=text;if(cls)e.className=cls;return e};
const fmt=ms=>{const s=Math.floor(ms/1000);return String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0')};
let editing=false;
async function post(url,params){const r=await fetch(url+(params?'?'+new URLSearchParams(params):''),{method:'POST'});const j=await r.json().catch(()=>({}));if(!j.ok)alert(j.error||'失敗');refresh();}
function render(s){
  $('state').textContent=STATES[s.state]+(s.rider?'（'+s.rider+'）':'')+(s.state==='countdown'?' '+s.countdownLeft+' 秒':'');
  if(!s.storageHealthy)$('state').textContent+='｜儲存失敗，請維修並重啟';
  else if(!s.hardwareReady)$('state').textContent+='｜硬體初始化失敗，請檢查下方狀態並重啟';
  else if(!s.sensors.ready)$('state').textContent+='｜請關蓋，等待感測器有效讀數';
  else if(!s.hasBaseline)$('state').textContent+='｜首次使用：清空後按「記錄空籃基準」';
  const m=$('marks');m.replaceChildren();
  if(s.hasTrash)m.append(el('span','有垃圾（'+s.trashOwner+' 留下）','tag bad'));
  if(s.hasLost)m.append(el('span','有遺失物','tag warn'));
  if(s.pending.length)m.append(el('span','待確認 '+s.pending.length+' 筆','tag warn'));
  if(!s.hasTrash&&!s.hasLost&&!s.pending.length)m.append(el('span',!s.sensors.ready||!s.hasBaseline?'尚無有效判定':s.sensors.present?'籃內有物品':'籃子乾淨','tag'));
  const n=s.sensors.now,b=s.sensors.baseline;
  const cutoffs=s.sensors.distanceCutoffsMm;
  const fixedDistance=cutoffs?.every(value=>value>0);
  const distanceRule=fixedDistance?'｜有物門檻：測距 1 ≤ '+cutoffs[0]+' mm 或測距 2 ≤ '+cutoffs[1]+' mm':'';
  const weight=s.sensors.weightEnabled?'｜重量 '+(n.weight??'-')+' g（基準 '+(b.weight??'-')+'）':'';
  $('sensors').textContent=(s.sensors.lidClosed?'蓋子關':'蓋子開')+weight+'｜距離 '+(n.dist0??'-')+'/'+(n.dist1??'-')+' mm（基準 '+(b.dist0??'-')+'/'+(b.dist1??'-')+'）'+distanceRule+'｜紅外線 '+(s.sensors.irEnabled===false?'暫停使用':!s.sensors.irValid?'尚無讀值':n.ir?'擋住':'通')+'｜判定：'+(!s.sensors.ready||!s.hasBaseline?'暫停判定':s.sensors.present?'有東西':'空的');
  $('oled').textContent=s.display.join('\n');
  const p=$('pending');p.replaceChildren();
  if(!s.pending.length)p.textContent='沒有待確認的物品';
  for(const x of s.pending){
    const d=el('div',null,'pend');
    d.append(el('div','卡號 '+x.owner+' 留下的物品'+(x.cleared?'（已被下一位清空）':'')+'，剩 '+x.leftS+' 秒，逾時視為垃圾並扣 '+s.config.penalty+' 元'));
    const t=el('button','是垃圾（扣 '+s.config.penalty+' 元）','trash');t.onclick=()=>post('/api/confirm',{owner:x.owner,kind:'trash'});
    const k=el('button','是我的東西（不扣錢）','mine');k.onclick=()=>post('/api/confirm',{owner:x.owner,kind:'mine'});
    d.append(t,' ',k);p.append(d);
  }
  const a=$('accounts');a.replaceChildren();a.append(Object.assign(el('tr'),{innerHTML:'<th>卡號</th><th class="n">餘額</th>'}));
  for(const x of s.accounts){const r=el('tr');r.append(el('td',x.uid),el('td',x.balance+' 元','n'+(x.balance<0?' bad':'')));a.append(r);}
  if(!editing){const c=$('config');c.replaceChildren();
    for(const k in NAMES){if(k==='weight_g'&&!s.sensors.weightEnabled)continue;const r=el('tr');const i=el('input');i.value=s.config[k];i.onfocus=()=>editing=true;
      i.onchange=()=>{editing=false;post('/api/config',{key:k,value:i.value});};i.onblur=()=>editing=false;
      const td=el('td');td.append(i);r.append(el('td',k==='dist_mm'&&fixedDistance?'借出時已有物品的變化門檻（mm）':NAMES[k]),td);c.append(r);}}
  const e=$('events');e.replaceChildren();
  for(const x of s.events.slice().reverse()){
    const cls=x.type==='penalty'?'bad':x.type==='reward'?'ok':(x.type==='pending'||x.type==='item_left')?'warn':'';
    e.append(el('div',fmt(x.t)+'  '+(x.uid?x.uid+' ':'')+x.text+(x.amount?'（'+(x.amount>0?'+':'')+x.amount+' 元）':''),cls));
  }
}
async function refresh(){try{render(await (await fetch('/api/state')).json());}catch(e){$('state').textContent='連不上籃子';}}
fetch('/api/telemetry').then(r=>r.json()).then(t=>{
  $('lid-controls').hidden=!t.manualLid;
  $('hw').textContent='硬體：'+Object.entries(t.hardware).map(([k,v])=>k+(v?' ✓':' ✗')).join('  ');
});
refresh();setInterval(refresh,1000);
</script></body></html>)HTML";

void sendJson(const std::string& body, int code = 200) {
    server.send(code, "application/json; charset=utf-8", body.c_str());
}

std::string resultJson(bool ok, const char* err) {
    return ok ? "{\"ok\":true}" : std::string("{\"ok\":false,\"error\":") + core::jsonEscape(err) + "}";
}

void sendResult(bool ok, const char* err) {
    sendJson(resultJson(ok, err), ok ? 200 : 400);
}

std::string telemetryJson() {
    const uint32_t now = millis();
    return telemetryPrefix + std::to_string(now) + ",\"hardware\":" + hw +
           ",\"manualLid\":" + (changeLid ? "true" : "false") +
           ",\"signals\":" + getSignals() + ",\"state\":" + core::stateJson(*bk, now) + "}";
}

const char* kCalibrateError = "請清空關蓋、確認紅外線暢通，並等兩顆測距讀值有效後再試";

std::string serialCommand(std::string_view line) {
    if (line == "telemetry") return telemetryJson();
    if (line == "calibrate") return resultJson(bk->calibrate(millis()), kCalibrateError);
#ifdef BASKET_TOF_TEST
    if (line.substr(0, 9) == "distance ") {
        const auto digits = line.substr(9);
        if (digits.empty() || digits.size() > 4 || digits.find_first_not_of("0123456789") != std::string_view::npos) {
            return resultJson(false, "測距門檻須為正整數");
        }
        int mm = 0;
        for (char digit : digits) mm = mm * 10 + (digit - '0');
        return resultJson(mm >= 1 && mm <= 8189 && bk->setConfig("dist_mm", mm),
                          "請在無借用、倒數或待確認物品時設定有效的測距門檻");
    }
#endif
    if (changeLid && (line == "lid open" || line == "lid closed")) {
        changeLid(line == "lid closed");
        return resultJson(true, "");
    }
    const bool trash = line.substr(0, 6) == "trash ";
    if (trash || line.substr(0, 5) == "mine ") {
        const auto uid = line.substr(trash ? 6 : 5);
        if (!uid.empty() && uid.size() <= 32 && uid.find_first_not_of(
                "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") == std::string_view::npos) {
            return resultJson(bk->confirm(std::string(uid), trash ? core::ConfirmKind::Trash : core::ConfirmKind::Mine, millis()),
                              "這筆待確認物品已處理，或裝置暫時無法寫入");
        }
    }
    return resultJson(false, "不支援的指令");
}

}  // namespace

void webBegin(core::Basket& basket, const std::string& hwStatusJson, DeviceSignals signals, SetLid setLid) {
    bk = &basket;
    hw = hwStatusJson;
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", kIndexHtml); });
    server.on("/api/state", HTTP_GET, [] { sendJson(core::stateJson(*bk, millis())); });
    server.on("/api/accounts", HTTP_GET, [] { sendJson(core::accountsJson(*bk)); });
    server.on("/api/events", HTTP_GET, [] { sendJson(core::eventsJson(*bk)); });
    server.on("/api/hw", HTTP_GET, [] { sendJson(hw); });
    char deviceId[13], bootId[17];
    snprintf(deviceId, sizeof deviceId, "%012llx", (unsigned long long)(ESP.getEfuseMac() & 0xffffffffffffULL));
    const uint32_t first = esp_random(), second = esp_random();
    snprintf(bootId, sizeof bootId, "%08lx%08lx", (unsigned long)first, (unsigned long)second);
    telemetryPrefix = std::string("{\"deviceId\":\"") + deviceId + "\",\"bootId\":\"" + bootId + "\",\"clockMs\":";
    getSignals = signals;
    changeLid = setLid;
    server.on("/api/telemetry", HTTP_GET, [] { sendJson(telemetryJson()); });
    if (changeLid) server.on("/api/lid", HTTP_POST, [] {
        const String value = server.arg("closed");
        if (value != "0" && value != "1") return sendResult(false, "請選擇蓋子開或關");
        changeLid(value == "1");
        sendResult(true, "");
    });
    server.on("/api/confirm", HTTP_POST, [] {
        String kind = server.arg("kind");
        if (kind != "trash" && kind != "mine") return sendResult(false, "kind 要是 trash 或 mine");
        bool ok = bk->confirm(server.arg("owner").c_str(),
                              kind == "trash" ? core::ConfirmKind::Trash : core::ConfirmKind::Mine, millis());
        sendResult(ok, "找不到這張卡的待確認物品");
    });
    server.on("/api/config", HTTP_POST, [] {
        String input = server.arg("value");
        char* end;
        double value = strtod(input.c_str(), &end);
        bool ok = end != input.c_str() && *end == '\0' && bk->setConfig(server.arg("key").c_str(), value);
        sendResult(ok, "設定不合法、儲存失敗或仍有借用／待確認（回饋須小於罰款，開鎖最多 1000 ms）");
    });
    server.on("/api/calibrate", HTTP_POST, [] { sendResult(bk->calibrate(millis()), "請清空關蓋、等待全部感測器有效，且無借用／物品標記／待確認；檢查儲存狀態"); });
    server.onNotFound([] { server.send(404, "text/plain", "not found"); });
    server.begin();
}

void webLoop() { server.handleClient(); }

void webSerialLoop() {
    static char line[64];
    static size_t length = 0;
    static bool overflow = false;
    for (int budget = 64; budget > 0 && Serial.available(); --budget) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\n') {
            if (!overflow && length > 9 && line[8] == ' ') {
                const std::string_view request(line, length);
                if (request.substr(0, 8).find_first_not_of("0123456789abcdef") == std::string_view::npos) {
                    Serial.printf("BASKET %.8s %s\n", line, serialCommand(request.substr(9)).c_str());
                }
            }
            length = 0;
            overflow = false;
        } else if (length < sizeof(line)) {
            line[length++] = ch;
        } else {
            overflow = true;
        }
    }
}
