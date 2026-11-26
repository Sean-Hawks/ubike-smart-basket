"use strict";

const byId = id => document.getElementById(id);
const developer = location.pathname === "/developer";
const dateFormat = new Intl.DateTimeFormat("zh-TW", {
  month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23"
});
const clockFormat = new Intl.DateTimeFormat("zh-TW", {
  hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23"
});
const labels = {
  boot: ["裝置啟動", "◌"], calibrate: ["記錄空籃基準", "⊙"], card_tap: ["刷卡", "◫"],
  rent: ["借車", "↗"], return: ["還車", "↙"], item_left: ["還車倒數", "◷"],
  pending: ["還車・物品待確認", "◷"], confirm_trash: ["確認垃圾", "✓"],
  confirm_mine: ["確認遺失物", "✓"], penalty: ["留垃圾扣款", "−"],
  reward: ["清理回饋", "+"], warning: ["裝置提醒", "!"]
};
const view = {
  device: sessionStorage.getItem("basket.device") || "",
  card: sessionStorage.getItem("basket.card") || "",
  filter: developer ? "device" : "all",
  pages: [0], nextBefore: null
};
let request = null;
let busy = false;
let liveStream = null;
let liveDevice = null;
let liveFrame = null;
let liveFrames = [];
let liveCount = 0;
let dashboard = null;
let priorReadings = new Map();
let cardLabels = {};
let editingCard = null;

function flash(node, color, duration) {
  if (!node || matchMedia("(prefers-reduced-motion: reduce)").matches) return;
  node.animate([{boxShadow: `0 0 0 4px ${color}`}, {boxShadow: "0 0 0 0 transparent"}],
    {duration, easing: "ease-out"});
}

function drawTrend(id, series, max, unit) {
  const canvas = byId(id);
  const width = canvas.clientWidth, height = canvas.clientHeight;
  if (!width || !height) return;
  const scale = window.devicePixelRatio || 1;
  if (canvas.width !== Math.round(width * scale) || canvas.height !== Math.round(height * scale)) {
    canvas.width = Math.round(width * scale);
    canvas.height = Math.round(height * scale);
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  ctx.clearRect(0, 0, width, height);
  const left = 51, right = width - 6, top = 10, bottom = height - 14;
  ctx.font = "11px system-ui";
  ctx.textBaseline = "middle";
  ctx.fillStyle = "#51645d";
  ctx.strokeStyle = "#dce5e0";
  ctx.lineWidth = 1;
  for (const ratio of [0, 0.5, 1]) {
    const y = bottom - ratio * (bottom - top);
    ctx.beginPath();
    ctx.moveTo(left, y);
    ctx.lineTo(right, y);
    ctx.stroke();
    ctx.fillText(Math.round(max * ratio) + unit, 1, y);
  }
  const end = liveFrames.at(-1)?.observed;
  let hasValues = false;
  for (const [valueOf, color] of series) {
    ctx.fillStyle = ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    let previous = null;
    for (const frame of liveFrames) {
      const value = valueOf(frame.signals);
      if (!Number.isFinite(value)) {
        previous = null;
        continue;
      }
      hasValues = true;
      const x = right - (end - frame.observed) / 10 * (right - left);
      const y = bottom - value / max * (bottom - top);
      if (previous && frame.observed - previous.time < 1.25) {
        ctx.beginPath();
        ctx.moveTo(previous.x, previous.y);
        ctx.lineTo(x, y);
        ctx.stroke();
      }
      ctx.beginPath();
      ctx.arc(x, y, 2, 0, Math.PI * 2);
      ctx.fill();
      previous = {x, y, time: frame.observed};
    }
  }
  if (!hasValues) {
    ctx.fillStyle = "#51645d";
    ctx.fillText("暫無有效資料", left + 7, (top + bottom) / 2);
  }
}

function drawLiveCharts() {
  const peak = liveFrames.reduce((max, {signals}) =>
    Math.max(max, signals.liveSensors?.dist0 ?? 0, signals.liveSensors?.dist1 ?? 0), 200);
  drawTrend("distance-chart", [
    [signals => signals.liveSensors?.dist0, "#158269"],
    [signals => signals.liveSensors?.dist1, "#5a66c0"],
  ], Math.ceil(peak / 100) * 100, " mm");
  drawTrend("ir-chart", [
    [signals => signals.liveSensors?.irValid ? signals.irHits : null, "#158269"],
  ], 4, " 段");
}

function renderHardware() {
  if (!dashboard) return;
  const {data, deviceName, source} = dashboard;
  let current = data;
  if (liveFrame?.online) {
    current = {...data, signals: liveFrame.signals, online: true, observed: liveFrame.observed,
      liveLidClosed: liveFrame.lidClosed};
  } else if (liveFrame?.online === false) {
    current = {...data, online: false};
  }
  const status = data.mode === "demo" ? source : current.online ? "實體裝置在線" : "實體裝置離線";
  renderDeveloper(current, deviceName, status);
}

function liveStatus(label, active) {
  const badge = byId("live-state");
  if (badge.textContent !== label) badge.textContent = label;
  badge.className = "pill" + (active ? "" : " dim");
  byId("live-dot").classList.toggle("active", active);
}

function connectLive(data) {
  const device = data.mode === "live" ? data.device : null;
  byId("live-panel").hidden = !device;
  if (liveDevice === device && (liveStream || !device)) return;
  liveStream?.close();
  liveDevice = device;
  liveFrame = null;
  liveFrames = [];
  liveCount = 0;
  priorReadings.clear();
  if (!device) return;
  liveStatus("正在連接", false);
  byId("live-metrics").textContent = "等待第一筆資料";
  drawLiveCharts();
  const stream = new EventSource("/api/live?" + new URLSearchParams({device}));
  liveStream = stream;
  stream.onmessage = event => {
    if (liveStream !== stream) return;
    const frame = JSON.parse(event.data);
    liveFrame = frame;
    if (!frame.online) {
      liveStatus("ESP32 暫無新資料", false);
      renderHardware();
      return;
    }
    if (liveFrames.at(-1)?.boot !== frame.boot) {
      liveFrames = [];
      liveCount = 0;
      priorReadings.clear();
    }
    liveFrames.push(frame);
    liveFrames = liveFrames.filter(sample => frame.observed - sample.observed <= 10).slice(-60);
    const elapsed = frame.observed - liveFrames[0].observed;
    const rate = elapsed >= 1 ? ((liveFrames.length - 1) / elapsed).toFixed(1) + " 筆／秒" : "";
    byId("live-metrics").textContent = "此頁已收到 " + (++liveCount) + " 筆" + (rate ? " · " + rate : "");
    liveStatus("正在接收實機資料", true);
    flash(byId("live-dot"), "#15826940", 200);
    drawLiveCharts();
    renderHardware();
  };
  stream.onerror = () => {
    if (liveStream !== stream) return;
    liveFrame = null;
    liveStatus("串流中斷・正在重連", false);
    renderHardware();
  };
}

function esc(value) {
  return String(value ?? "").replace(/[&<>"']/g, char => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
  }[char]));
}
function setHtml(id, html) {
  const node = byId(id);
  if (node.dataset.rendered !== html) {
    node.innerHTML = html;
    node.dataset.rendered = html;
  }
}
function show(id, value, style = "") {
  const node = byId(id);
  node.textContent = value;
  node.hidden = !value;
  node.className = "notice " + style;
}
function pill(text, kind = "") {
  return `<span class="pill ${kind}">${esc(text)}</span>`;
}
function date(seconds) {
  return seconds ? dateFormat.format(seconds * 1000) : "—";
}
function cardName(uid) {
  if (!uid) return "—";
  return Object.hasOwn(cardLabels, uid) ? cardLabels[uid] + "（" + uid + "）" : "卡 " + uid;
}

function renderCardRegistration(data) {
  const tap = data.lastTap;
  byId("card-registration").hidden = data.mode !== "live";
  byId("card-identity").textContent = tap
    ? "最近刷卡 " + tap.uid + " · " + (Object.hasOwn(cardLabels, tap.uid) ? cardLabels[tap.uid] : "尚未登記姓名")
    : "請先刷一張學生證";
  const edit = byId("edit-card-label");
  edit.disabled = !tap || editingCard !== null;
  edit.textContent = tap && Object.hasOwn(cardLabels, tap.uid) ? "修改姓名" : "登記這張卡";
}

function closeCardRegistration() {
  editingCard = null;
  byId("card-label-form").hidden = true;
  if (dashboard) renderCardRegistration(dashboard.data);
}

function selectField(id, name, options, value) {
  let select = byId(id);
  if (!select) {
    const label = document.createElement("label");
    label.textContent = name;
    select = document.createElement("select");
    select.id = id;
    label.append(select);
    byId("selectors").append(label);
  }
  const items = options.map(option => `<option value="${esc(option.id)}">${esc(option.name)}</option>`).join("");
  setHtml(id, items);
  select.value = value;
  select.disabled = options.length < 2;
}

function render(data) {
  const {state, device, card, online} = data;
  cardLabels = data.cardLabels ?? {};
  view.card = card || "";
  view.device = device;
  view.nextBefore = data.nextBefore;
  const demo = data.mode === "demo";
  const deviceName = data.devices.find(entry => entry.id === device).name;
  const source = demo ? "模擬裝置" : online ? "實體裝置在線" : "實體裝置離線";
  byId("source-caption").textContent = source;
  selectField("device-selector", "車籃", data.devices, device);
  if (!developer) {
    const cards = data.cards.map(id => ({id, name: cardName(id)}));
    selectField("card-selector", "卡片", cards.length ? cards : [{id: "", name: "尚無卡片"}], view.card);
  }
  show("connection-warning", online ? "" :
    state ? "裝置離線，目前顯示最後收到的狀態。已收錄的歷史紀錄仍可查詢。" :
      "等待裝置連線，暫無即時狀態。");

  if (developer) {
    dashboard = {data, deviceName, source};
    connectLive(data);
    renderCardRegistration(data);
    renderHardware();
  }
  else renderOverview(data, deviceName, source);
  renderEvents(data.events, demo);
}

function basketReadinessMessage(state) {
  if (!state?.hardwareReady || !state.storageHealthy) return "請檢查裝置狀態";
  if (!state.sensors.lidClosed) return "等待關蓋";
  if (!state.hasBaseline) return "尚未記錄空籃基準";
  if (!state.sensors.ready) return "等待感測器讀值穩定";
  return "";
}

function renderOverview(data, deviceName, source) {
  const {state, card, online} = data;
  const readiness = basketReadinessMessage(state);
  const pending = state?.pending.filter(entry => entry.owner === card) || [];
  const ownRide = card && state?.rider === card;
  byId("hero-source").textContent = deviceName + " · " + source;
  let title = "等待裝置";
  let detail = "連線後即可查看目前車籃狀態";
  if (state) {
    if (!online) {
      title = "裝置離線";
      detail = "最後更新於 " + date(data.observed);
    } else if (!state.hardwareReady || !state.storageHealthy) {
      title = "車籃暫停使用";
      detail = "請在開發者模式查看裝置狀態";
    } else if (state.state === "boot") {
      title = "車籃準備中";
      detail = readiness || "等待感測器讀值穩定";
    } else if (ownRide && state.state === "countdown") {
      title = state.countdownLeft ? "請取出物品　" + state.countdownLeft + " 秒" : "請關好車籃";
      detail = "感測器確認籃內已清空後，會完成還車";
    } else if (ownRide) {
      title = "騎乘中";
      detail = "還車時請關好籃蓋，再刷同一張卡";
    } else if (pending.length) {
      title = "有物品待確認";
      detail = "請確認還車後留在籃內的物品";
    } else if (state.rider) {
      title = "車籃使用中";
      detail = "目前由其他卡片借用";
    } else {
      title = "等待借車";
      detail = "到車籃讀卡區刷卡即可借用";
    }
  }
  byId("ride-state").textContent = title;
  byId("ride-detail").textContent = detail;

  const balance = state?.accounts.find(account => account.uid === card)?.balance;
  const item = !state ? "—" : state.hasLost ? "遺失物" : state.hasTrash ? "有垃圾" :
    state.pending.some(entry => !entry.cleared) ? "待辨識" :
    readiness || (state.sensors.present ? "有物品" : "空籃");
  setHtml("summary", [
    ["帳戶餘額", balance ?? "—", balance == null ? "" : "元"],
    ["籃內狀態", item, ""],
    ["我的待確認", pending.length, "筆"]
  ].map(([label, value, unit]) =>
    `<div class="metric"><p class="metric-title">${label}</p><p class="metric-value">${esc(value)}${unit ? `<small>${unit}</small>` : ""}</p></div>`
  ).join(""));

  byId("pending-panel").hidden = !pending.length;
  // Keep confirmation buttons in place when only the remaining time changes.
  const cards = pending.map(entry => `<div class="pending-row">
    <div><strong>${esc(cardName(entry.owner))} 的物品</strong><p class="subtle" data-deadline></p></div>
    <div class="tool-actions">
      <button data-confirm="mine" data-owner="${esc(entry.owner)}"${online ? "" : " disabled"}>是我的東西</button>
      <button class="button-primary" data-confirm="trash" data-owner="${esc(entry.owner)}"${online ? "" : " disabled"}>是垃圾（扣 ${esc(state.config.penalty)} 元）</button>
    </div>
  </div>`).join("");
  setHtml("pending-list", cards);
  byId("pending-list").querySelectorAll("[data-deadline]").forEach((node, i) => {
    const entry = pending[i];
    node.textContent = (entry.cleared ? "物品已被清空 · " : "") + "剩餘 " + entry.leftS + " 秒，逾時視為垃圾";
  });
}

function renderDeveloper(data, deviceName, source) {
  const {state, hardware, signals, online, mode, lastTap} = data;
  const demo = mode === "demo";
  const tofTest = signals.tofTest === true;
  const irPaused = tofTest && signals.irRxPin == null;
  const current = state?.sensors.now || {};
  const live = signals.liveSensors;
  const distance = live ?? current;
  const irValid = live?.irValid ?? state?.sensors.irValid;
  const irBlocked = live ? live.irBlocked : current.ir;
  const irIdleFault = live?.irValid === false && signals.irHits != null;
  const irDetail = [
    tofTest && !irPaused ? "僅供診斷" : "",
    signals.irTxPin != null ? "發射 GPIO" + signals.irTxPin : "",
    signals.irRxPin != null ? "接收 GPIO" + signals.irRxPin : "",
    signals.irHits == null ? "" : (irIdleFault ? "原始計數（無效） " : "收到 ") + signals.irHits + " / 4 段",
    signals.irRxPulses != null ? "接收脈衝累計 " + signals.irRxPulses : "",
    signals.irRxHigh == null ? "" : "腳位 " + (signals.irRxHigh ? "HIGH" : "LOW"),
  ].filter(Boolean).join(" · ");
  const lidClosed = data.liveLidClosed ?? state?.sensors.lidClosed;
  const baseline = state?.sensors.baseline || {};
  const readiness = online ? basketReadinessMessage(state) : "請查看裝置狀態";
  const cutoffs = state?.sensors.distanceCutoffsMm;
  const testNote = tofTest ? " · 借還使用測距判定" : "";
  setHtml("dev-banner", `<div><h2>${esc(deviceName)}</h2><p class="subtle">${esc(source)} · ${readiness || "可接受借還操作"}${testNote}</p></div>
    ${pill(online ? demo ? "模擬中" : "已連線" : "離線", online ? "" : "bad")}`);
  byId("live-ir-plot").hidden = irPaused;
  byId("live-help").textContent = irPaused
    ? "把白紙靠近測距頭，檢查兩路讀值。紅外線在這個測試版停用。"
    : "試著把白紙靠近測距頭，或遮住再移開紅外線。數值改變時，對應的卡片會亮一下。";
  byId("updated").textContent = data.observed
    ? (online ? "自動更新 · " : "最後收集 · ") + clockFormat.format(data.observed * 1000)
    : "等待資料";
  const unit = (value, suffix) => value == null ? "—" : value + suffix;
  const check = accepted => accepted === undefined ? ["未回報", "dim"] :
    accepted ? ["初始化成功", ""] : ["初始化失敗", "bad"];
  const sensor = (accepted, valid) => accepted === false ? check(false) :
    valid === undefined ? ["未回報", "dim"] : valid ? ["讀值有效", ""] : ["暫無有效讀值", "warn"];
  const nfcStage = {spi: "SPI 未啟動", firmware: "未讀到晶片韌體",
    sam_config: "讀卡模式設定失敗", rf_config: "感應設定失敗"};
  const nfcFirmware = signals.nfcFirmware
    ? " · 韌體 " + ((signals.nfcFirmware >>> 16) & 255) + "." + ((signals.nfcFirmware >>> 8) & 255) : "";
  const parts = [
    ...[0, 1].map(index => {
      const key = "dist" + index;
      const reference = cutoffs?.[index] > 0
        ? "有物門檻 ≤ " + unit(cutoffs[index], " mm")
        : "空籃基準 " + unit(baseline[key], " mm");
      return ["測距 " + (index + 1), unit(distance[key], " mm"),
        ...sensor(hardware["tof" + index], state ? distance[key] != null : undefined),
        (live && !lidClosed ? "開蓋讀值 · " : "") + reference];
    }),
    irPaused ? ["紅外線對射", "停用", "僅用測距測試", "dim", ""]
      : ["紅外線對射", irIdleFault ? "接收異常" : irValid ? irBlocked ? "接收不足" : "通過" : "—",
      ...(irIdleFault ? ["閒置時未回到 HIGH", "bad"] :
        irValid ? [irBlocked ? "未收到足夠訊號" : "光路暢通", irBlocked ? "warn" : ""] : sensor(undefined, irValid)),
      irDetail],
    ["NFC 讀卡器", hardware.nfc === false ? "無法讀卡" : lastTap ? lastTap.uid : "等待刷卡", ...check(hardware.nfc),
      hardware.nfc === false
        ? (nfcStage[signals.nfcStage] || "PN532") + (signals.nfcAttempts ? " · 嘗試 " + signals.nfcAttempts + " 次" : "")
        : (lastTap ? (Object.hasOwn(cardLabels, lastTap.uid) ? cardLabels[lastTap.uid] + " · " : "") +
          "最後刷卡 " + date(lastTap.observed) : "PN532") + nfcFirmware],
    ["I²C 多工器", "TCA9548A", ...check(hardware.tca9548a),
      live ? [live.dist0, live.dist1].filter(value => value != null).length + " / 2 路測距有讀值" : ""],
    ["籃蓋", lidClosed == null ? "—" : lidClosed ? "已關閉" : "已開啟",
      data.manualLid ? "手動回報" : state ? "開關訊號" : "未回報", "dim", ""],
    signals.buttonPin === -1 ? ["開蓋按鈕", "停用", "GPIO32 用於紅外線診斷", "dim", ""]
      : ["開蓋按鈕", signals.buttonPressed == null ? "—" : signals.buttonPressed ? "按下" : "未按下",
        demo ? "使用下方模擬按鈕" : "GPIO" + (signals.buttonPin ?? 32) + " 電位", "dim",
        signals.buttonPresses == null ? "" : "本次開機偵測 " + signals.buttonPresses + " 次"],
    ["電控鎖", signals.lockOn == null ? demo ? signals.lockPulses + " 次脈衝" : "—" :
      signals.lockOn ? "輸出 HIGH" : "輸出 LOW", "只回報控制訊號", "dim",
      !demo && signals.lockPulses != null ? "本次開機 " + signals.lockPulses + " 次脈衝" : ""],
    ["蜂鳴器", signals.buzzerHz == null ? "—" : signals.buzzerHz ? signals.buzzerHz + " Hz" : "無聲音輸出", "只回報控制訊號", "dim",
      signals.buzzerTones == null ? "" : "本次開機 " + signals.buzzerTones + " 次" +
        (signals.buzzerLastHz ? " · 最近 " + signals.buzzerLastHz + " Hz" : "")],
    state?.sensors.weightEnabled === false
      ? ["重量感測器", "停用", "展示設定", "dim", "HX711"]
      : ["重量感測器", unit(current.weight, " g"), ...sensor(hardware.hx711, state ? current.weight != null : undefined),
        "基準 " + unit(baseline.weight, " g") + (hardware.hx711cal === false ? " · 待校正" : "")],
    ["OLED", hardware.oled === true ? "I²C 有回應" : hardware.oled === false ? "I²C 初始化失敗" : "—",
      ...check(hardware.oled), "下方為送出的文字，實際畫面請目視確認"],
    ["ESP32", state?.uptime == null ? "—" : state.uptime + " 秒", "開機時間", "dim", ""],
    ["裝置儲存", state?.storageHealthy === undefined ? "—" : state.storageHealthy ? "正常" : "讀寫異常",
      ...check(hardware.storage), "ESP32 內部儲存"]
  ];
  setHtml("sensors", parts.map(([name, value, status, kind, detail]) =>
    `<article class="sensor"><h3>${esc(name)}</h3><strong class="sensor-value">${esc(value)}</strong>
     ${pill(online ? status : "離線", online ? kind : "dim")}${detail ? `<p class="subtle">${esc(detail)}</p>` : ""}</article>`
  ).join(""));
  if (online) {
    const cards = byId("sensors").children;
    for (const [index, [name, value, status, , detail]] of parts.entries()) {
      if (name === "ESP32") continue;
      const reading = JSON.stringify([value, status, detail]);
      if (priorReadings.has(name) && priorReadings.get(name) !== reading) flash(cards[index], "#15826955", 450);
      priorReadings.set(name, reading);
    }
  }
  byId("oled").textContent = state?.display.filter(Boolean).join("\n") || "等待裝置資料";
  byId("lid-controls").hidden = !data.manualLid;
  byId("lid-controls").querySelectorAll("[data-lid]").forEach(button => { button.disabled = !online; });
  byId("lid-controls").querySelector("[data-calibrate]").disabled =
    !online || !state?.sensors.ready || state.sensors.now.ir || !["boot", "idle"].includes(state.state);
  byId("simulation").hidden = !demo;
  setHtml("storage-summary", `<dl class="status-list">
    <div><dt>SQLite</dt><dd>${pill("可讀取")}</dd></div>
    <div><dt>裝置儲存</dt><dd>${state ? state.storageHealthy ? "正常" : "寫入異常" : "—"}</dd></div>
    <div><dt>空籃基準</dt><dd>${state ? state.hasBaseline ? "已記錄" : "待校正" : "—"}</dd></div>
    <div><dt>偵測到的紀錄缺口</dt><dd>${data.archiveGaps} 筆</dd></div>
  </dl>${data.error ? `<p class="subtle">${esc(data.error)}</p>` : ""}`);
}

function renderEvents(events, demo) {
  byId("pagination").hidden = view.pages.length === 1 && !view.nextBefore;
  byId("newer").disabled = view.pages.length === 1;
  byId("older").disabled = !view.nextBefore;
  setHtml("events", events.length ? events.map(event => {
    const [name, symbol] = labels[event.type] || ["其他事件", "·"];
    const money = event.type === "penalty" || event.type === "reward";
    const amount = event.amount > 0 ? "+" + event.amount : event.amount.toString().replace("-", "−");
    const detail = [developer && event.uid ? cardName(event.uid) : "", event.text === name ? "" : event.text]
      .filter(Boolean).join(" · ");
    return `<li class="event">
      <span class="event-symbol ${event.type === "penalty" ? "penalty" : ""}" aria-hidden="true">${symbol}</span>
      <div><strong>${name}</strong>${detail ? `<p class="subtle">${esc(detail)}</p>` : ""}</div>
      <div class="event-end">${money ? `<p class="amount ${event.amount < 0 ? "negative" : ""}">${amount} 元</p>` : ""}
        <time title="${demo ? "模擬紀錄寫入時間" : "伺服器收錄時間"}" datetime="${new Date(event.observed * 1000).toISOString()}">${date(event.observed)}</time></div>
    </li>`;
  }).join("") : '<li class="empty">目前沒有符合條件的紀錄</li>');
}

async function refresh() {
  request?.abort();
  const controller = new AbortController();
  request = controller;
  const params = new URLSearchParams({filter: view.filter, before: view.pages.at(-1)});
  if (view.device) params.set("device", view.device);
  if (view.card) params.set("card", view.card);
  try {
    let response = await fetch("/api/dashboard?" + params, {signal: controller.signal});
    if (response.status === 400 && params.has("device")) {
      // The previous dashboard can have a different set of configured devices.
      params.delete("device");
      response = await fetch("/api/dashboard?" + params, {signal: controller.signal});
    }
    if (!response.ok) throw new Error("無法讀取 Web 服務");
    const result = await response.json();
    if (!controller.signal.aborted) render(result);
  } catch (error) {
    if (error.name !== "AbortError") show("connection-warning", "連不上 Web 服務，正在重新連線…");
  } finally {
    if (request === controller) request = null;
  }
}

async function action(params, message) {
  if (busy) return;
  busy = true;
  show("feedback", "");
  const buttons = document.querySelectorAll("[data-command], [data-scene], [data-confirm], [data-lid], [data-calibrate]");
  buttons.forEach(button => { button.disabled = true; });
  const query = new URLSearchParams({...params, device: view.device});
  try {
    const response = await fetch("/api/action?" + query, {method: "POST"});
    const result = await response.json();
    if (!response.ok) throw new Error(result.error);
    show("feedback", message, "success");
  } catch (error) {
    show("feedback", error.message || "操作未完成");
  } finally {
    buttons.forEach(button => { button.disabled = false; });
    busy = false;
    await refresh();
  }
}

byId(developer ? "developer" : "overview").hidden = false;
byId(developer ? "nav-developer" : "nav-overview").setAttribute("aria-current", "page");
byId("section-label").textContent = developer ? "DEVICE MONITOR" : "RIDER OVERVIEW";
if (developer) {
  document.title = "智慧籃子｜開發者模式";
  byId("title").textContent = "開發者模式";
  byId("activity-title").textContent = "裝置事件";
  byId("activity-caption").textContent = "目前車籃寫入 SQLite 的事件";
  byId("filters").hidden = true;
}
byId("selectors").addEventListener("change", event => {
  const select = event.target;
  if (select.id === "device-selector") {
    closeCardRegistration();
    view.device = select.value;
    view.card = "";
    sessionStorage.setItem("basket.device", view.device);
  } else {
    view.card = select.value;
  }
  sessionStorage.setItem("basket.card", view.card);
  view.pages = [0];
  refresh();
});
byId("edit-card-label").addEventListener("click", () => {
  const tap = dashboard?.data.lastTap;
  if (!tap) return;
  editingCard = {uid: tap.uid, device: dashboard.data.device};
  byId("edit-card-label").disabled = true;
  byId("editing-card-uid").textContent = tap.uid;
  byId("card-label-input").value = Object.hasOwn(cardLabels, tap.uid) ? cardLabels[tap.uid] : "";
  byId("card-label-feedback").textContent = "";
  byId("card-label-form").hidden = false;
  byId("card-label-input").focus();
});
byId("cancel-card-label").addEventListener("click", closeCardRegistration);
byId("card-label-form").addEventListener("submit", async event => {
  event.preventDefault();
  const current = editingCard;
  if (!current) return;
  const button = byId("save-card-label");
  const cancel = byId("cancel-card-label");
  if (button.disabled) return;
  button.disabled = cancel.disabled = true;
  const info = byId("card-label-feedback");
  try {
    const response = await fetch("/api/card-label?" + new URLSearchParams({device: current.device}), {
      method: "POST", headers: {"Content-Type": "application/json"},
      body: JSON.stringify({uid: current.uid, label: byId("card-label-input").value.trim()}),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error);
    if (editingCard === current) {
      closeCardRegistration();
      info.textContent = "已儲存卡號 " + current.uid + " 的姓名";
    }
    await refresh();
  } catch (error) {
    info.textContent = error.message || "無法儲存姓名";
  } finally {
    button.disabled = cancel.disabled = false;
  }
});
byId("filters").addEventListener("click", event => {
  const filter = event.target.dataset.filter;
  if (!filter) return;
  view.filter = filter;
  view.pages = [0];
  byId("filters").querySelectorAll("button").forEach(button => {
    button.setAttribute("aria-pressed", String(button.dataset.filter === filter));
  });
  refresh();
});
byId("older").addEventListener("click", () => {
  if (view.nextBefore) view.pages.push(view.nextBefore);
  refresh();
});
byId("newer").addEventListener("click", () => {
  if (view.pages.length > 1) view.pages.pop();
  refresh();
});
document.addEventListener("click", event => {
  const button = event.target.closest("button");
  if (!button) return;
  const {confirm, owner, command, scene} = button.dataset;
  if (button.dataset.lid !== undefined) {
    action({control: "lid", closed: button.dataset.lid}, "已更新籃蓋狀態");
  } else if (button.hasAttribute("data-calibrate")) {
    if (window.confirm("確認籃子已清空、兩顆測距已就位，並且沒有遮住紅外線？")) {
      action({control: "calibrate"}, "已記錄空籃基準");
    }
  } else if (confirm) {
    const text = confirm === "trash" ? "確認將這筆物品標記為垃圾並扣款？" : "確認這是你的物品？";
    if (window.confirm(text)) action({kind: confirm, owner}, "已送出物品確認");
  } else if (command) {
    const value = command === "tap" ? "tap " + byId("sim-card").value : command;
    action({command: value}, "模擬操作已送出");
  } else if (scene) {
    view.pages = [0];
    action({scene}, "已載入模擬情境");
  }
});
refresh();
if (developer) window.addEventListener("resize", drawLiveCharts);
setInterval(() => { if (!busy && !request) refresh(); }, 1300);
