#pragma once
#include <Arduino.h>

inline const char CalibrationPage[] PROGMEM = R"PAGE(<!doctype html>
<html lang="uk"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Калібрування каретки</title>
<style>
:root{color-scheme:dark;background:#202923;color:#eff2ea;font:16px system-ui,sans-serif}
body{margin:0;padding:16px}main{max-width:37rem;margin:auto}
h1{font-size:1.55rem;margin:.35rem 0 .75rem}h2{font-size:1.1rem;margin:0 0 .75rem}
section{border:1px solid #647467;border-radius:12px;padding:14px;margin:12px 0}
p{line-height:1.45}label{display:grid;gap:5px}input,button{font:inherit;color:inherit;border:1px solid #879486;border-radius:8px;padding:8px;background:#2e3d34;min-height:28px;box-sizing:border-box}
input{width:100%}button{min-height:42px;cursor:pointer}button:disabled{opacity:.5;cursor:default}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:8px 0}
.stop{background:#813b35;border-color:#d78c84;width:100%;font-weight:700}
.wide{width:100%}.message{min-height:1.35em}small{color:#c0cbbd}
table{width:100%;border-collapse:collapse;font-size:.87rem}td,th{text-align:left;border-bottom:1px solid #647467;padding:6px 3px}
</style></head>
<body><main>
<h1>Калібрування каретки</h1>
<p id="status" role="status" class="message">Підключення…</p>
<section>
<h2>Рух за часом</h2>
<div class="row">
<label>Час, с<input id="seconds" type="number" inputmode="decimal" min="0.2" max="10" step="0.1" value="1"></label>
<label>ШІМ, %<input id="speed" type="number" inputmode="numeric" min="1" max="100" step="1" value="50"></label>
</div>
<div class="row"><button id="left" disabled>← Вліво</button><button id="right" disabled>Вправо →</button></div>
<button id="stop" class="stop">СТОП</button>
<p id="run" class="message">Вибери час і напрямок.</p>
</section>
<section>
<h2>Перевірка підсвітки</h2>
<div class="row"><button data-light="red" disabled>Червоний</button><button data-light="green" disabled>Зелений</button></div>
<button class="wide" data-light="mosfet" disabled>Строб MOSFET 12 В</button>
<p id="lightState" class="message" role="status">Кожен тест блимає 3 секунди.</p>
</section>
<section>
<h2>Центр троса</h2>
<p>Вистав каретку на фізичній середині троса короткими рухами, потім познач центр.</p>
<button id="center" class="wide" disabled>Позначити центр (0 м)</button>
<p id="centerState"><small>Позначку центру ще не встановлено.</small></p>
</section>
<section>
<h2>Результат виміру</h2>
<p><small>Після повної зупинки виміряй рулеткою відстань від точки старту до точки зупинки.</small></p>
<div class="row">
<label>Відстань, см<input id="distance" type="number" inputmode="decimal" min="0" max="100000" step="any"></label>
<button id="record" disabled>Записати вимір</button>
</div>
<table><thead><tr><th>Напрямок</th><th>ШІМ</th><th>Час</th><th>Відстань</th><th>Середня</th></tr></thead><tbody id="samples"></tbody></table>
<p><small>Таблиця зберігається в цьому браузері.</small></p>
</section>
</main><script>
"use strict";
const byId = id => document.getElementById(id);
const lightButtons = document.querySelectorAll("[data-light]");
const lightNames = {red: "Червоний LED", green: "Зелений LED", mosfet: "Строб 12 В"};
const names = {
  idle: "Вибери час і напрямок.",
  queued: "Підготовка до запуску…",
  running: "Каретка рухається",
  done: "Вимір завершено.",
  stopped: "Вимір перервано кнопкою СТОП.",
  link_lost: "Зупинено через втрату зв’язку зі сторінкою.",
  fault: "Помилка виходу ШІМ. Перевір ESP32 та драйвер."
};
const storeKey = "small-carriage-calibration-v1";
let samples = [];
try {
  const saved = JSON.parse(localStorage.getItem(storeKey) || "[]");
  if (Array.isArray(saved)) samples = saved;
} catch (_) {}
let current = null, leaseId = 0, busy = false, online = false, polling = false, requestVersion = 0;
const isMoving = state => state?.phase === "running" || state?.phase === "queued";
const directionName = direction => direction < 0 ? "Вліво" : "Вправо";
const seconds = ms => (ms / 1000).toFixed(2) + " с";

function controls() {
  const enabled = online && !busy && current && !isMoving(current);
  byId("left").disabled = byId("right").disabled = !enabled || !current?.ready;
  byId("center").disabled = !enabled;
  for (const button of lightButtons) button.disabled = !enabled;
  byId("record").disabled = !enabled || current?.phase !== "done" ||
    samples.some(sample => sample.id === current.id);
}
function render(state) {
  online = true;
  current = state;
  if (leaseId && (state.id !== leaseId || !isMoving(state))) leaseId = 0;
  byId("status").textContent = state.ready ? "Плата на зв’язку." : "Плата на зв’язку. Привід недоступний.";
  byId("run").textContent = (names[state.phase] || "") + (state.elapsedMs ?
    " " + directionName(state.direction) + " · " + seconds(state.elapsedMs) + " · " + state.speed + "%." : "");
  byId("centerState").textContent = state.center ? "Нульову позначку збережено до перезапуску ESP32." : "Позначку центру ще не встановлено.";
  byId("lightState").textContent = lightNames[state.lightTest] ?
    lightNames[state.lightTest] + " блимає…" : "Кожен тест блимає 3 секунди.";
  controls();
}
function showSamples() {
  const rows = [...samples].reverse().map(sample => {
    const row = document.createElement("tr");
    const values = [directionName(sample.direction), sample.speed + "%", seconds(sample.elapsedMs),
      (sample.cm / 100).toFixed(2) + " м", (sample.cm / 100 / (sample.elapsedMs / 1000)).toFixed(3) + " м/с"];
    for (const value of values) row.insertCell().textContent = value;
    return row;
  });
  byId("samples").replaceChildren(...rows);
}
async function request(path, data) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 700);
  try {
    const response = await fetch(path, {
      method: data ? "POST" : "GET",
      body: data ? new URLSearchParams(data) : undefined,
      cache: "no-store",
      signal: controller.signal
    });
    if (!response.ok) throw new Error(await response.text());
    return await response.json();
  } finally { clearTimeout(timeout); }
}
async function poll() {
  if (busy || polling) return;
  polling = true;
  const version = requestVersion;
  try {
    const state = leaseId ? await request("/api/heartbeat", {id:leaseId}) : await request("/api/state");
    if (version === requestVersion) render(state);
  } catch (_) {
    if (version === requestVersion) {
      online = false;
      byId("status").textContent = "Немає зв’язку з ESP32.";
      controls();
    }
  } finally { polling = false; }
}
async function action(path, args, starts = false) {
  if (busy && path !== "/api/stop") return;
  const version = ++requestVersion;
  busy = true;
  controls();
  try {
    const state = await request(path,args);
    if (version !== requestVersion) return;
    if (starts && isMoving(state)) leaseId = state.id;
    render(state);
  } catch (error) {
    if (version === requestVersion) byId("status").textContent = error.name === "AbortError" ?
      "ESP32 не відповідає." : error.message;
  } finally {
    if (version === requestVersion) { busy = false; controls(); }
  }
}
function move(direction) {
  const duration = byId("seconds"), speed = byId("speed");
  if (!duration.value || !speed.value || !duration.reportValidity() || !speed.reportValidity()) return;
  action("/api/run", {direction, ms:Math.round(Number(duration.value)*1000), speed:speed.value, stopEpoch:current.stopEpoch}, true);
}
byId("left").onclick = () => move("left");
byId("right").onclick = () => move("right");
byId("stop").onclick = () => { leaseId = 0; action("/api/stop", {}); };
byId("center").onclick = () => action("/api/center", {});
for (const button of lightButtons) button.onclick = () =>
  action("/api/light-test", {output:button.dataset.light, stopEpoch:current.stopEpoch});
byId("record").onclick = () => {
  const input = byId("distance");
  if (!input.value || !input.reportValidity() || current?.phase !== "done" || !current.elapsedMs ||
      samples.some(sample => sample.id === current.id)) return;
  samples.push({id:current.id,direction:current.direction,speed:current.speed,elapsedMs:current.elapsedMs,cm:Number(input.value)});
  try { localStorage.setItem(storeKey, JSON.stringify(samples)); } catch (_) {}
  input.value = "";
  showSamples();
  controls();
};
showSamples();
poll();
setInterval(poll, 200);
</script></body></html>)PAGE";
