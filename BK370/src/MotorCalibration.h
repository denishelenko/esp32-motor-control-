#pragma once
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include "Hardware.h"

namespace bk370 {
constexpr char MotorCalibrationHtml[] PROGMEM = R"HTML(
<!doctype html><html lang="uk"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BK370 · Калібрування моторів</title>
<style>
body{margin:0;background:#1a1f1b;color:#eff0e9;font:16px system-ui,sans-serif}
main{max-width:680px;margin:20px auto;padding:18px;background:#303a32;border-radius:16px}
h1{margin-top:0;font-size:1.5rem}p{line-height:1.5}.buttons{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
button{min-height:48px;border:1px solid #829081;border-radius:11px;background:#222a24;color:inherit;font:inherit;font-weight:700}
button:disabled{opacity:.45}#stop{width:100%;margin-top:12px;background:#78382e}
table{width:100%;border-collapse:collapse;text-align:right}th,td{padding:7px 3px;border-bottom:1px solid #5e6a61}
th:first-child{text-align:left}small{color:#d1dacd}
@media(max-width:450px){main{margin:8px;padding:12px}}
</style>
<main><h1>BK370 · Калібрування моторів</h1>
<p>Вправо → пауза 5 с → вліво. Підніми колеса над опорою й познач їхні початкові положення.</p>
<div class="buttons"><button data-turns="1" disabled>1 оберт</button><button data-turns="5" disabled>5 обертів</button>
<button data-turns="7" disabled>7 обертів</button><button data-turns="10" disabled>10 обертів</button></div>
<button id="stop">СТОП</button>
<p id="status" role="status">Підключення до ESP32…</p>
<p id="power">INA226 · немає показів</p>
<table><thead><tr><th scope="col">Параметр</th><th scope="col">M1</th><th scope="col">M2</th></tr></thead>
<tbody><tr><th scope="row">Швидкість, об/хв</th><td id="rpm0">—</td><td id="rpm1">—</td></tr>
<tr><th scope="row">Макс. швидкість, об/хв</th><td id="peakRpm0">—</td><td id="peakRpm1">—</td></tr>
<tr><th scope="row">Поточні відліки</th><td id="count0">—</td><td id="count1">—</td></tr>
<tr><th scope="row">Ціль на хід</th><td id="goal0">—</td><td id="goal1">—</td></tr>
<tr><th scope="row">Вправо</th><td id="right0">—</td><td id="right1">—</td></tr>
<tr><th scope="row">Вліво</th><td id="left0">—</td><td id="left1">—</td></tr>
<tr><th scope="row">Зсув від старту</th><td id="offset0">—</td><td id="offset1">—</td></tr></tbody></table>
<p><small>Перевір кількість обертів і мітки фізично. Ціль обчислюється за поточними параметрами енкодерів.</small></p>
</main>
<script>
const el=id=>document.getElementById(id), buttons=[...document.querySelectorAll("[data-turns]")];
let token=0,busy=false,ready=false;
const format=(value,digits)=>Number.isFinite(value)?value.toFixed(digits):"—";
function updateButtons(){buttons.forEach(button=>button.disabled=busy||!ready)}
async function poll(){
 const requestedToken=token;
 try{
  const response=await fetch("/status?run="+requestedToken,{cache:"no-store"});
  if(!response.ok)throw Error();
  const state=await response.json();
  if(token===requestedToken){
   ready=state.ready&&!state.active;
   if(!state.active)token=0;
   if(!busy)el("status").textContent=state.message;
  }
  for(const key of ["count","goal","right","left","offset"])
   for(let i=0;i<2;i++)el(key+i).textContent=state[key]?.[i]??"—";
  for(const key of ["rpm","peakRpm"])
   for(let i=0;i<2;i++)el(key+i).textContent=format(state[key]?.[i],1);
  el("power").textContent="INA226 · "+format(state.volts,2)+" В · "+format(state.amps,2)+" А · "+
    format(state.watts,1)+" Вт"+(Number.isFinite(state.peakAmps)?" · максимум за тест "+format(state.peakAmps,2)+" А":"");
 }catch{ready=false;el("status").textContent="Немає зв’язку з ESP32";}
 updateButtons();
 setTimeout(poll,300);
}
buttons.forEach(button=>button.onclick=async()=>{
 busy=true;updateButtons();
 try{
  const response=await fetch("/start?turns="+button.dataset.turns,{method:"POST"});
  const result=await response.json();
  if(response.ok){token=result.run;ready=false}
  else el("status").textContent=result.message;
 }catch{ready=false;el("status").textContent="Не вдалося надіслати команду";}
 finally{busy=false;updateButtons()}
});
el("stop").onclick=()=>fetch("/stop",{method:"POST"}).catch(()=>{el("status").textContent="Немає зв’язку з ESP32"});
poll();
</script></html>
)HTML";

class MotorCalibration {
  enum class Phase { Idle, Right, Wait, Left, Done, Failed };
  static constexpr float NominalCountsPerTurn = 11.0f * 9.3f * 4.0f;
  static constexpr int MaxPwm = 205;  // 20% on the 10-bit output scale.
  static constexpr uint32_t RunTimeoutMs = 20000, LeaseMs = 1500, QuietMs = 200, PauseMs = 5000;
  static constexpr uint32_t SampleMs = 100;

 public:
  void begin() {
    Serial.begin(115200);
    battery_.begin();
    for (unsigned i = 0; i < 2; ++i) {
      const auto &cfg = Motors[i];
      // Start both driver inputs LOW and attach separate channels.
      for (uint8_t pin : {cfg.pwmA, cfg.pwmB}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
      }
      const bool a = ledcAttachChannel(cfg.pwmA, 1000, 10, i * 2);
      const bool b = ledcAttachChannel(cfg.pwmB, 1000, 10, i * 2 + 1);
      pwmReady_[i] = a && b;
      if (pwmReady_[i]) pwmReady_[i] = ledcWrite(cfg.pwmA, 0) && ledcWrite(cfg.pwmB, 0);
      if (!pwmReady_[i]) {
        if (a) ledcDetach(cfg.pwmA);
        if (b) ledcDetach(cfg.pwmB);
        setPinsLow(i);
      }
      encoderReady_[i] = encoders_[i].begin(cfg);
    }
    WiFi.mode(WIFI_AP);
    WiFi.softAP(SSID, Password, Channel);
    if (MDNS.begin("motor-control")) MDNS.addService("http", "tcp", 80);
    server_.on("/", HTTP_GET, [this] { server_.send_P(200, "text/html; charset=utf-8", MotorCalibrationHtml); });
    server_.on("/start", HTTP_POST, [this] { startRequested(); });
    server_.on("/status", HTTP_GET, [this] { sendStatus(); });
    server_.on("/stop", HTTP_POST, [this] {
      if (active()) finish(Phase::Failed, "Тест зупинено");
      server_.send(204);
    });
    server_.begin();
    log("ROTATION", "http://motor-control.local/ or http://192.168.4.1/");
  }

  void tick() {
    const uint32_t now = millis();
    battery_.tick(now);
    for (unsigned i = 0; i < 2; ++i) {
      int64_t value;
      if (encoderReady_[i] && !encoders_[i].read(value)) encoderReady_[i] = false;
      if (encoderReady_[i] && value != count_[i]) {
        count_[i] = value;
        changedAt_[i] = now;
      }
    }
    sampleTelemetry(now);
    if (active()) {
      if (!ready(now)) finish(Phase::Failed, readyMessage(now));
      else if (now - leaseAt_ >= LeaseMs) finish(Phase::Failed, "Втрачено зв’язок із браузером");
      else if (phase_ == Phase::Wait) {
        if (now - phaseAt_ >= PauseMs) startLeg(Phase::Left, now);
      } else serviceLeg(now);
    }
    server_.handleClient();
  }

 private:
  bool active() const { return phase_ == Phase::Right || phase_ == Phase::Wait || phase_ == Phase::Left; }
  const char *readyMessage(uint32_t now) const {
    if (!encoderReady_[0] || !encoderReady_[1]) return "Помилка енкодера";
    if (!pwmReady_[0] || !pwmReady_[1]) return "Помилка виходу ШІМ";
    if (!DriveCommissioned) return "Рух заблоковано · DriveCommissioned у Settings.h";
    if (!battery_.ready(now)) return "Перевір живлення та INA226";
    return nullptr;
  }
  bool ready(uint32_t now) const { return readyMessage(now) == nullptr; }
  static float countsPerTurn(unsigned i) {
    return Motors[i].countsPerWheelTurn > 0 ? float(Motors[i].countsPerWheelTurn) : NominalCountsPerTurn;
  }
  void sampleTelemetry(uint32_t now) {
    const uint32_t elapsed = now - sampleAt_;
    if (elapsed < SampleMs) return;
    const bool movingLeg = phase_ == Phase::Right || phase_ == Phase::Left;
    for (unsigned i = 0; i < 2; ++i) {
      if (!encoderReady_[i]) continue;
      rpm_[i] = float(count_[i] - sampleCount_[i]) * 60000.0f / (elapsed * countsPerTurn(i));
      sampleCount_[i] = count_[i];
      if (movingLeg) peakRpm_[i] = max(peakRpm_[i], fabsf(rpm_[i]));
    }
    if (movingLeg && battery_.valid && battery_.currentValid) {
      peakAmps_ = hasPeakAmps_ ? max(peakAmps_, battery_.amps) : battery_.amps;
      hasPeakAmps_ = true;
    }
    sampleAt_ = now;
  }
  void setPinsLow(unsigned i) {
    for (uint8_t pin : {Motors[i].pwmA, Motors[i].pwmB}) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
  }
  void write(unsigned i, int pwm) {
    if (!pwmReady_[i] || pwm == applied_[i]) return;
    const auto &cfg = Motors[i];
    const int physical = cfg.reverseMotor ? -pwm : pwm;
    const bool ok = physical > 0 ? ledcWrite(cfg.pwmB, 0) && ledcWrite(cfg.pwmA, physical) :
                                  ledcWrite(cfg.pwmA, 0) && ledcWrite(cfg.pwmB, -physical);
    if (!ok) {
      ledcDetach(cfg.pwmA);
      ledcDetach(cfg.pwmB);
      setPinsLow(i);
      pwmReady_[i] = false;
    }
    applied_[i] = ok ? pwm : 0;
  }
  void brake() { write(0, 0); write(1, 0); }
  void finish(Phase result, const char *message) {
    brake();
    phase_ = result;
    message_ = message;
    log("ROTATION", "ended phase=%u", unsigned(result));
    log("CALIBRATION", "max output rpm M1=%.1f M2=%.1f", peakRpm_[0], peakRpm_[1]);
    if (hasPeakAmps_) log("CALIBRATION", "max sampled current=%.2f A", peakAmps_);
  }
  void startLeg(Phase phase, uint32_t now) {
    phase_ = phase;
    phaseAt_ = now;
    for (unsigned i = 0; i < 2; ++i) {
      legStart_[i] = count_[i];
      changedAt_[i] = now;
      braking_[i] = false;
    }
    message_ = phase == Phase::Right ? "Рух вправо" : "Рух вліво";
    log("ROTATION", "start dir=%d goal=%ld/%ld", phase == Phase::Right ? 1 : -1, long(goal_[0]), long(goal_[1]));
  }
  void serviceLeg(uint32_t now) {
    if (now - phaseAt_ >= RunTimeoutMs) {
      finish(Phase::Failed, "Вичерпано час руху");
      return;
    }
    const bool right = phase_ == Phase::Right;
    const int direction = right ? 1 : -1;
    const int64_t progress[2] = {direction * (count_[0] - legStart_[0]), direction * (count_[1] - legStart_[1])};
    const float skew = fabsf(progress[0] / countsPerTurn(0) - progress[1] / countsPerTurn(1));
    if (progress[0] < -6 || progress[1] < -6 || skew > 0.25f) {
      finish(Phase::Failed, "Напрямок або відліки коліс не збігаються");
      return;
    }
    bool settled = true;
    for (unsigned i = 0; i < 2; ++i) {
      const int64_t remaining = goal_[i] - progress[i];
      if (!braking_[i] && remaining <= 0) {
        braking_[i] = true;
        changedAt_[i] = now;
        write(i, 0);
      }
      if (braking_[i]) {
        settled &= now - changedAt_[i] >= QuietMs;
        continue;
      }
      settled = false;
      if (now - changedAt_[i] >= 1000) {
        finish(Phase::Failed, "Колесо не рухається або енкодер не дає відліків");
        return;
      }
      const int ceiling = min(MaxPwm, int(1023 * constrain(Motors[i].dutyMultiplier, 0.0f, 1.0f)));
      const int approach = constrain(int(remaining) * 2, min(120, ceiling), ceiling);
      const int ramp = int(min(now - phaseAt_, uint32_t(700)) * ceiling / 700);
      write(i, direction * min(approach, ramp));
    }
    if (!pwmReady_[0] || !pwmReady_[1]) {
      finish(Phase::Failed, "Помилка виходу ШІМ");
      return;
    }
    if (!settled) return;
    auto &result = right ? right_ : left_;
    for (unsigned i = 0; i < 2; ++i) result[i] = count_[i] - legStart_[i];
    log("ROTATION", "settled dir=%d count=%lld/%lld offset=%lld/%lld", direction,
        (long long)result[0], (long long)result[1], (long long)(count_[0] - start_[0]), (long long)(count_[1] - start_[1]));
    if (right) {
      rightDone_ = true;
      phase_ = Phase::Wait;
      phaseAt_ = now;
      message_ = "Колеса зупинилися · пауза 5 секунд";
    } else {
      leftDone_ = true;
      finish(Phase::Done, "Тест завершено · звір результати й мітки");
    }
  }
  void startRequested() {
    const String turns = server_.arg("turns");
    const uint32_t now = millis();
    JsonDocument d;
    if (active() || !ready(now) || (turns != "1" && turns != "5" && turns != "7" && turns != "10")) {
      d["message"] = active() ? "Тест уже триває" : !ready(now) ? readyMessage(now) : "Оберіть 1, 5, 7 або 10 обертів";
      sendJson(409, d);
      return;
    }
    rightDone_ = leftDone_ = false;
    sampleAt_ = now;
    peakAmps_ = 0;
    hasPeakAmps_ = false;
    for (unsigned i = 0; i < 2; ++i) {
      start_[i] = count_[i];
      sampleCount_[i] = count_[i];
      peakRpm_[i] = 0;
      goal_[i] = lroundf(turns.toInt() * countsPerTurn(i));
    }
    token_ = esp_random() | 1U;
    leaseAt_ = now;
    startLeg(Phase::Right, now);
    d["run"] = token_;
    sendJson(200, d);
  }
  void sendStatus() {
    const uint32_t now = millis();
    if (active() && strtoul(server_.arg("run").c_str(), nullptr, 10) == token_) leaseAt_ = now;
    JsonDocument d;
    d["ready"] = ready(now);
    d["active"] = active();
    const char *blocker = readyMessage(now);
    d["message"] = blocker && phase_ != Phase::Failed ? blocker : message_;
    if (battery_.valid) d["volts"] = battery_.millivolts / 1000.0f;
    if (battery_.valid && battery_.currentValid) {
      d["amps"] = battery_.amps;
      d["watts"] = battery_.amps * battery_.millivolts / 1000.0f;
    }
    if (hasPeakAmps_) d["peakAmps"] = peakAmps_;
    auto counts = d["count"].to<JsonArray>();
    auto goals = d["goal"].to<JsonArray>();
    auto right = d["right"].to<JsonArray>();
    auto left = d["left"].to<JsonArray>();
    auto offsets = d["offset"].to<JsonArray>();
    auto rpm = d["rpm"].to<JsonArray>();
    auto peaks = d["peakRpm"].to<JsonArray>();
    for (unsigned i = 0; i < 2; ++i) {
      counts.add(count_[i]);
      if (encoderReady_[i]) rpm.add(rpm_[i]);
      else rpm.add(nullptr);
      if (token_) peaks.add(peakRpm_[i]);
      if (token_) { goals.add(goal_[i]); offsets.add(count_[i] - start_[i]); }
      if (rightDone_) right.add(right_[i]);
      if (leftDone_) left.add(left_[i]);
    }
    sendJson(200, d);
  }
  void sendJson(int code, const JsonDocument &d) {
    String data;
    serializeJson(d, data);
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(code, "application/json; charset=utf-8", data);
  }

  WebServer server_{80};
  BatteryMonitor battery_;
  Encoder encoders_[2];
  Phase phase_ = Phase::Idle;
  const char *message_ = "Оберіть кількість обертів";
  bool encoderReady_[2]{}, pwmReady_[2]{}, braking_[2]{}, rightDone_ = false, leftDone_ = false;
  uint32_t phaseAt_ = 0, changedAt_[2]{}, leaseAt_ = 0, token_ = 0;
  int applied_[2]{};
  int32_t goal_[2]{};
  int64_t count_[2]{}, start_[2]{}, legStart_[2]{}, right_[2]{}, left_[2]{};
  uint32_t sampleAt_ = 0;
  int64_t sampleCount_[2]{};
  float rpm_[2]{}, peakRpm_[2]{}, peakAmps_ = 0;
  bool hasPeakAmps_ = false;
};
}
