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
.settings{display:grid;gap:10px;margin:16px 0}.settings label{display:flex;align-items:center;justify-content:space-between;gap:8px}
.settings input[type=range]{flex:1;min-width:60px;accent-color:#b0c6ad}
.settings input[type=number],.settings select{width:100px;padding:8px;border:1px solid #829081;border-radius:8px;background:#222a24;color:inherit;font:inherit}
table{width:100%;border-collapse:collapse;text-align:right}th,td{padding:7px 3px;border-bottom:1px solid #5e6a61}
th:first-child{text-align:left}small{color:#d1dacd}
@media(max-width:450px){main{margin:8px;padding:12px}}
</style>
<main><h1>BK370 · Калібрування моторів</h1>
<p>Підніми колеса над опорою й познач їхні початкові положення.</p>
<div class="settings">
 <label>Мотор <select id="motors"><option value="1">M1</option><option value="2">M2</option><option value="3">Обидва</option></select></label>
 <label>Кількість обертів <input id="turns" type="number" min="1" max="10" step="1" value="1" required></label>
 <label>Швидкість (ШІМ) <input id="speed" type="range" min="1" max="100" value="20"><output id="speedValue" for="speed">20%</output></label>
</div>
<div class="buttons"><button data-direction="left" disabled>← Вліво</button><button data-direction="right" disabled>Вправо →</button></div>
<button id="stop">СТОП</button>
<p id="status" role="status">Підключення до ESP32…</p>
<table><thead><tr><th scope="col">Параметр</th><th scope="col">M1</th><th scope="col">M2</th></tr></thead>
<tbody><tr><th scope="row">Швидкість, об/хв</th><td id="rpm0">—</td><td id="rpm1">—</td></tr>
<tr><th scope="row">Макс. швидкість, об/хв</th><td id="peakRpm0">—</td><td id="peakRpm1">—</td></tr>
<tr><th scope="row">Середній час, с/оберт</th><td id="secondsPerTurn0">—</td><td id="secondsPerTurn1">—</td></tr>
<tr><th scope="row">Поточні відліки</th><td id="count0">—</td><td id="count1">—</td></tr>
<tr><th scope="row">Ціль на хід</th><td id="goal0">—</td><td id="goal1">—</td></tr>
<tr><th scope="row">Пройдено від старту, відл.</th><td id="offset0">—</td><td id="offset1">—</td></tr>
<tr><th scope="row">Похибка після гальмування, відл.</th><td id="error0">—</td><td id="error1">—</td></tr></tbody></table>
<p><small>Перевір кількість обертів і мітки фізично. Ціль обчислюється за поточними параметрами енкодерів.</small></p>
</main>
<script>
const el=id=>document.getElementById(id), buttons=[...document.querySelectorAll("[data-direction]")];
let token=0,busy=false,active=false,readyMotors=0;
const format=(value,digits)=>Number.isFinite(value)?value.toFixed(digits):"—";
function updateButtons(){
 const selected=+el("motors").value;
 buttons.forEach(button=>button.disabled=busy||active||(readyMotors&selected)!==selected);
}
async function poll(){
 const requestedToken=token;
 try{
  const response=await fetch("/status?run="+requestedToken,{cache:"no-store"});
  if(!response.ok)throw Error();
  const state=await response.json();
  if(token===requestedToken){
   active=state.active;
   readyMotors=state.readyMotors;
   if(!state.active)token=0;
   if(!busy)el("status").textContent=state.message;
  }
  for(const key of ["count","goal","offset","error"])
   for(let i=0;i<2;i++)el(key+i).textContent=state[key]?.[i]??"—";
  for(const key of ["rpm","peakRpm"])
   for(let i=0;i<2;i++)el(key+i).textContent=format(state[key]?.[i],1);
  for(let i=0;i<2;i++)el("secondsPerTurn"+i).textContent=format(state.secondsPerTurn?.[i],2);
 }catch{readyMotors=0;el("status").textContent="Немає зв’язку з ESP32";}
 updateButtons();
 setTimeout(poll,300);
}
buttons.forEach(button=>button.onclick=async()=>{
 if(!el("turns").reportValidity())return;
 const params=new URLSearchParams({turns:el("turns").value,speed:el("speed").value,
                                  motors:el("motors").value,direction:button.dataset.direction});
 busy=true;updateButtons();
 try{
  const response=await fetch("/start?"+params,{method:"POST"});
  const result=await response.json();
  if(response.ok){token=result.run;active=true}
  else el("status").textContent=result.message;
 }catch{readyMotors=0;el("status").textContent="Не вдалося надіслати команду";}
 finally{busy=false;updateButtons()}
});
el("motors").onchange=updateButtons;
el("speed").oninput=()=>{el("speedValue").textContent=el("speed").value+"%"};
el("stop").onclick=()=>fetch("/stop",{method:"POST"}).catch(()=>{el("status").textContent="Немає зв’язку з ESP32"});
poll();
</script></html>
)HTML";

class MotorCalibration {
  enum class Phase { Idle, Running, Done, Failed };
  enum class WheelPhase { Travel, Brake, Trim };
  static constexpr float NominalCountsPerTurn = 11.0f * 9.3f * 4.0f;
  static constexpr int MaxSpeedPercent = 100, MaxTurns = 10;
  static constexpr uint32_t RunTimeoutMs = 20000, LeaseMs = 1500, QuietMs = 200;
  static constexpr uint32_t SampleMs = 100;
  static constexpr float BrakeLeadTurns = 14.0f / NominalCountsPerTurn;
  static constexpr int ApproachDuty = 153, TrimDuty = 205;  // 15% and 20% on the 10-bit scale.
  static constexpr uint8_t MaxTrimPulses = 16;
  static constexpr uint32_t TrimMinMs = 20, TrimMaxMs = 50;

 public:
  void begin(const char *homeSsid = nullptr, const char *homePassword = nullptr) {
    Serial.begin(115200);
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
    homeWifi_ = homeSsid != nullptr;
    if (homeWifi_) {
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      if (*homeSsid) {
        WiFi.begin(homeSsid, homePassword);
        log("WIFI", "Connecting to home Wi-Fi (2.4 GHz)");
      } else log("WIFI", "Set HOME_WIFI_SSID in the Home sketch");
    } else {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(SSID, Password, Channel);
      announceSite(WiFi.softAPIP());
    }
    server_.on("/", HTTP_GET, [this] { server_.send_P(200, "text/html; charset=utf-8", MotorCalibrationHtml); });
    server_.on("/start", HTTP_POST, [this] { startRequested(); });
    server_.on("/status", HTTP_GET, [this] { sendStatus(); });
    server_.on("/stop", HTTP_POST, [this] {
      if (active()) finish(Phase::Failed, "Тест зупинено");
      server_.send(204);
    });
    server_.begin();
  }

  void tick() {
    const uint32_t now = millis();
    if (homeWifi_) serviceHomeWifi();
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
      if ((readyMotors() & selected_) != selected_) finish(Phase::Failed, "Помилка енкодера або ШІМ вибраного мотора");
      else if (now - leaseAt_ >= LeaseMs) finish(Phase::Failed, "Втрачено зв’язок із браузером");
      else serviceMove(now);
    }
    server_.handleClient();
  }

 private:
  void announceSite(IPAddress ip) {
    mdnsReady_ = MDNS.begin("motor-control");
    if (mdnsReady_) MDNS.addService("http", "tcp", 80);
    log("WEB", "http://%s/%s", ip.toString().c_str(),
        mdnsReady_ ? " or http://motor-control.local/" : "");
  }
  void serviceHomeWifi() {
    const IPAddress ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : IPAddress();
    if (ip == homeIp_) return;
    if (mdnsReady_) {
      MDNS.end();
      mdnsReady_ = false;
    }
    homeIp_ = ip;
    if (uint32_t(ip)) announceSite(ip);
    else {
      log("WIFI", "Home Wi-Fi disconnected");
      if (active()) finish(Phase::Failed, "Втрачено зв’язок із домашнім Wi-Fi");
    }
  }
  bool active() const { return phase_ == Phase::Running; }
  uint8_t readyMotors() const {
    uint8_t mask = 0;
    for (unsigned i = 0; i < 2; ++i)
      if (encoderReady_[i] && pwmReady_[i]) mask |= 1U << i;
    return mask;
  }
  bool selected(unsigned i) const { return selected_ & (1U << i); }
  static float countsPerTurn(unsigned i) {
    return Motors[i].countsPerWheelTurn > 0 ? float(Motors[i].countsPerWheelTurn) : NominalCountsPerTurn;
  }
  void sampleTelemetry(uint32_t now) {
    const uint32_t elapsed = now - sampleAt_;
    if (elapsed < SampleMs) return;
    for (unsigned i = 0; i < 2; ++i) {
      if (!encoderReady_[i]) continue;
      rpm_[i] = float(count_[i] - sampleCount_[i]) * 60000.0f / (elapsed * countsPerTurn(i));
      sampleCount_[i] = count_[i];
      if (active() && selected(i)) peakRpm_[i] = max(peakRpm_[i], fabsf(rpm_[i]));
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
  }
  void startMove(int turns, int speed, uint32_t now) {
    phase_ = Phase::Running;
    phaseAt_ = now;
    sampleAt_ = now;
    for (unsigned i = 0; i < 2; ++i) {
      start_[i] = sampleCount_[i] = count_[i];
      changedAt_[i] = now;
      wheelPhase_[i] = WheelPhase::Travel;
      trimPulses_[i] = 0;
      peakRpm_[i] = secondsPerTurn_[i] = 0;
      goal_[i] = selected(i) ? lroundf(turns * countsPerTurn(i)) : 0;
      maxDuty_[i] = lroundf(1023 * min(speed / 100.0f, constrain(Motors[i].dutyMultiplier, 0.0f, 1.0f)));
    }
    message_ = direction_ > 0 ? "Рух вправо" : "Рух вліво";
    log("ROTATION", "start motors=%u dir=%d PWM<=%d%% goal=%ld/%ld",
        selected_, direction_, speed, long(goal_[0]), long(goal_[1]));
  }
  bool serviceWheel(unsigned i, int64_t progress, uint32_t now) {
    const int64_t remaining = goal_[i] - progress;
    const int ceiling = maxDuty_[i];
    switch (wheelPhase_[i]) {
      case WheelPhase::Travel: {
        if (remaining <= lroundf(BrakeLeadTurns * countsPerTurn(i))) {
          brakeWheel(i, now);
          return false;
        }
        if (now - changedAt_[i] >= 1000) {
          finish(Phase::Failed, i == 0 ? "M1: немає відліків під час руху" : "M2: немає відліків під час руху");
          return false;
        }
        const int approach = max(min(ApproachDuty, ceiling), int(ceiling * min(1.0f, remaining / (0.5f * countsPerTurn(i)))));
        const int ramp = int(min(now - phaseAt_, uint32_t(700)) * ceiling / 700);
        write(i, direction_ * min(approach, ramp));
        return false;
      }
      case WheelPhase::Brake:
        if (now - wheelPhaseAt_[i] < QuietMs || now - changedAt_[i] < QuietMs) return false;
        if (remaining <= 0 || trimPulses_[i] >= MaxTrimPulses) return true;
        ++trimPulses_[i];
        trimStart_[i] = progress;
        wheelPhase_[i] = WheelPhase::Trim;
        wheelPhaseAt_[i] = now;
        write(i, direction_ * min(TrimDuty, ceiling));
        return false;
      case WheelPhase::Trim: {
        const uint32_t pulseMs = min(TrimMaxMs, TrimMinMs + 5U * (trimPulses_[i] - 1));
        if (progress > trimStart_[i] || now - wheelPhaseAt_[i] >= pulseMs) brakeWheel(i, now);
        return false;
      }
    }
    return false;
  }
  void brakeWheel(unsigned i, uint32_t now) {
    wheelPhase_[i] = WheelPhase::Brake;
    wheelPhaseAt_[i] = now;
    write(i, 0);
  }
  void serviceMove(uint32_t now) {
    if (now - phaseAt_ >= RunTimeoutMs) {
      finish(Phase::Failed, "Вичерпано час руху");
      return;
    }
    const int64_t progress[2] = {direction_ * (count_[0] - start_[0]), direction_ * (count_[1] - start_[1])};
    bool settled = true;
    for (unsigned i = 0; i < 2; ++i) {
      if (!selected(i)) continue;
      if (progress[i] < -6) {
        finish(Phase::Failed, i == 0 ? "M1: відліки йдуть у протилежний бік" : "M2: відліки йдуть у протилежний бік");
        return;
      }
      settled &= serviceWheel(i, progress[i], now);
      if (!active()) return;
    }
    if ((readyMotors() & selected_) != selected_) {
      finish(Phase::Failed, "Помилка виходу ШІМ");
      return;
    }
    if (!settled) return;
    bool exact = true;
    for (unsigned i = 0; i < 2; ++i) {
      if (!selected(i)) continue;
      exact &= progress[i] == goal_[i];
      stoppedProgress_[i] = progress[i];
      const float turns = float(progress[i]) / countsPerTurn(i);
      if (turns > 0) secondsPerTurn_[i] = (changedAt_[i] - phaseAt_) / (1000.0f * turns);
      log("ROTATION", "M%u counts=%lld/%ld secondsPerTurn=%.2f", i + 1,
          (long long)progress[i], long(goal_[i]), secondsPerTurn_[i]);
    }
    finish(Phase::Done, exact ? "Ціль досягнуто за енкодером · звір мітки" : "Зупинка з похибкою · перевір відліки й мітки");
  }
  void startRequested() {
    const int turns = boundedValue("turns", MaxTurns);
    const int speed = boundedValue("speed", MaxSpeedPercent);
    const uint8_t motors = boundedValue("motors", 3);
    const String direction = server_.arg("direction");
    const uint32_t now = millis();
    JsonDocument d;
    if (!turns || !speed || !motors || (direction != "right" && direction != "left")) {
      d["message"] = "Перевір кількість обертів, ШІМ, мотор і напрямок";
      sendJson(400, d);
      return;
    }
    if (active() || (readyMotors() & motors) != motors) {
      d["message"] = active() ? "Тест уже триває" : "Помилка енкодера або ШІМ вибраного мотора";
      sendJson(409, d);
      return;
    }
    selected_ = motors;
    direction_ = direction == "right" ? 1 : -1;
    token_ = esp_random() | 1U;
    leaseAt_ = now;
    startMove(turns, speed, now);
    d["run"] = token_;
    sendJson(200, d);
  }
  int boundedValue(const char *name, int maxValue) {
    const String value = server_.arg(name);
    const long number = value.toInt();
    return number >= 1 && number <= maxValue && value == String(number) ? int(number) : 0;
  }
  void sendStatus() {
    const uint32_t now = millis();
    if (active() && strtoul(server_.arg("run").c_str(), nullptr, 10) == token_) leaseAt_ = now;
    JsonDocument d;
    d["readyMotors"] = readyMotors();
    d["active"] = active();
    d["message"] = !readyMotors() && phase_ == Phase::Idle ? "Помилка енкодерів або виходів ШІМ" : message_;
    auto counts = d["count"].to<JsonArray>();
    auto goals = d["goal"].to<JsonArray>();
    auto offsets = d["offset"].to<JsonArray>();
    auto errors = d["error"].to<JsonArray>();
    auto rpm = d["rpm"].to<JsonArray>();
    auto peaks = d["peakRpm"].to<JsonArray>();
    auto seconds = d["secondsPerTurn"].to<JsonArray>();
    for (unsigned i = 0; i < 2; ++i) {
      counts.add(count_[i]);
      if (encoderReady_[i]) rpm.add(rpm_[i]);
      else rpm.add(nullptr);
      if (token_ && selected(i)) {
        peaks.add(peakRpm_[i]);
        goals.add(goal_[i]);
        offsets.add(phase_ == Phase::Done ? direction_ * stoppedProgress_[i] : count_[i] - start_[i]);
      } else {
        peaks.add(nullptr);
        goals.add(nullptr);
        offsets.add(nullptr);
      }
      if (phase_ == Phase::Done && selected(i)) seconds.add(secondsPerTurn_[i]);
      else seconds.add(nullptr);
      if (phase_ == Phase::Done && selected(i)) errors.add(stoppedProgress_[i] - goal_[i]);
      else errors.add(nullptr);
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
  IPAddress homeIp_{};
  bool homeWifi_ = false, mdnsReady_ = false;
  Encoder encoders_[2];
  Phase phase_ = Phase::Idle;
  const char *message_ = "Оберіть кількість обертів";
  bool encoderReady_[2]{}, pwmReady_[2]{};
  WheelPhase wheelPhase_[2]{};
  uint8_t trimPulses_[2]{};
  uint32_t wheelPhaseAt_[2]{};
  uint8_t selected_ = 0;
  int direction_ = 1;
  uint32_t phaseAt_ = 0, changedAt_[2]{}, leaseAt_ = 0, token_ = 0;
  int applied_[2]{}, maxDuty_[2]{};
  int32_t goal_[2]{};
  int64_t count_[2]{}, start_[2]{}, trimStart_[2]{}, stoppedProgress_[2]{};
  uint32_t sampleAt_ = 0;
  int64_t sampleCount_[2]{};
  float rpm_[2]{}, peakRpm_[2]{}, secondsPerTurn_[2]{};
};
}
