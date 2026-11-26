#pragma once
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_timer.h>
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
#noConnection[hidden],.driveFault[hidden]{display:none!important}
#noConnection{position:fixed;inset:0;z-index:200;display:grid;place-items:center;background:#3d4240a9;backdrop-filter:blur(5px) grayscale(.8);color:#fff;text-align:center}
#noConnection strong{font-size:clamp(1.35rem,5vw,2rem);text-shadow:0 1px 7px #0008}
.connectionPulse{position:absolute;top:22px;right:22px;width:13px;height:13px;border-radius:50%;background:#ff8989;animation:offlinePulse 1.8s ease-in-out infinite}
@keyframes offlinePulse{50%{opacity:.3;box-shadow:0 0 13px 4px #ff898977}}
@media(prefers-reduced-motion:reduce){.connectionPulse{animation:none}}
.driveFault{padding:9px 11px;margin-top:6px;border:1px solid #ce6860;border-radius:9px;color:#ffaaaa;background:#4c2826;font-weight:700;white-space:pre-line}
</style>
<main><h1>BK370 · Калібрування моторів</h1>
<p>Закріпи каретку, підніми колеса й познач початок оберту.</p>
<div class="settings">
 <label>Мотор <select id="motors"><option value="1">M1</option><option value="2">M2</option><option value="3">Обидва</option></select></label>
 <label>Потрібно обертів <input id="turns" type="number" min="1" max="1000000" step="1" value="1" required></label>
 <label>Швидкість <input id="speed" type="range" min="0" max="100" value="20"><output id="speedValue" for="speed">20%</output></label>
 <label>Плавність <input id="smooth" type="checkbox" checked></label>
</div>
<details id="settings"><summary>Калібрування M1 і M2</summary>
<div class="settings">
 <label>Гальмувати M1 раніше, тіків <input id="brakeM1" type="number" min="0" max="2147483647" step="1" value="__BRAKE_M1__" required></label>
 <label>Гальмувати M2 раніше, тіків <input id="brakeM2" type="number" min="0" max="2147483647" step="1" value="__BRAKE_M2__" required></label>
 <label>M1 · тіків за повний оберт <input id="ticksM1" type="number" min="1" max="1000000" step="any" value="__TICKS_M1__" required></label>
 <label>M2 · тіків за повний оберт <input id="ticksM2" type="number" min="1" max="1000000" step="any" value="__TICKS_M2__" required></label>
 <label>M1 · ШІМ на підході, % <input id="approachM1" type="number" min="5" max="100" step="1" value="__APPROACH_M1__" required></label>
 <label>M2 · ШІМ на підході, % <input id="approachM2" type="number" min="5" max="100" step="1" value="__APPROACH_M2__" required></label>
 <label>Відстань замість обертів, м <input id="metres" type="number" min="0.01" max="100000" step="0.01"></label>
</div>
<p><small>ШІМ на підході діє з увімкненою плавністю. Для першого заміру залиш випередження 0.</small></p>
</details>
<div class="buttons"><button data-direction="left" disabled>← Вліво</button><button data-direction="right" disabled>Вправо →</button></div>
<button id="stop">СТОП</button>
<p id="status" role="status">Підключення до ESP32…</p>
<p id="accepted" role="status"></p>
<p id="warning" role="status" style="color:#ffd08a"></p>
<div id="driveFault" class="driveFault" role="alert" hidden></div>
<table><thead><tr><th scope="col">Параметр</th><th scope="col">M1</th><th scope="col">M2</th></tr></thead>
<tbody><tr><th scope="row">Фактично обертів за енкодером</th><td id="actualTurns0">—</td><td id="actualTurns1">—</td></tr>
<tr><th scope="row">Поточна швидкість, об/хв</th><td id="rpm0">—</td><td id="rpm1">—</td></tr>
<tr><th scope="row">Макс. швидкість за хід, об/хв</th><td id="peakRpm0">—</td><td id="peakRpm1">—</td></tr>
<tr><th scope="row">Рекомендоване випередження, тіків</th><td id="nextBrake0">—</td><td id="nextBrake1">—</td></tr>
<tr><th scope="row">Кінцева похибка, тіків (+ переліт, − недохід)</th><td id="error0">—</td><td id="error1">—</td></tr>
</tbody></table>
<details><summary>Відліки енкодера</summary>
<table aria-label="Відліки енкодера"><thead><tr><th scope="col">Параметр</th><th scope="col">M1</th><th scope="col">M2</th></tr></thead>
<tbody><tr><th scope="row">Поточні відліки</th><td id="count0">—</td><td id="count1">—</td></tr>
<tr><th scope="row">Ціль на хід</th><td id="goal0">—</td><td id="goal1">—</td></tr>
<tr><th scope="row">Застосоване випередження, тіків</th><td id="advance0">—</td><td id="advance1">—</td></tr>
<tr><th scope="row">Плановий відлік гальма</th><td id="brakeTarget0">—</td><td id="brakeTarget1">—</td></tr>
<tr><th scope="row">Фактичний відлік гальма</th><td id="brakeCount0">—</td><td id="brakeCount1">—</td></tr>
<tr><th scope="row">Пройдено від старту, відл.</th><td id="offset0">—</td><td id="offset1">—</td></tr>
<tr><th scope="row">Фактичний хід по колесу, см</th><td id="travelCm0">—</td><td id="travelCm1">—</td></tr>
<tr><th scope="row">Хід після команди гальма, тіків</th><td id="brakeTravel0">—</td><td id="brakeTravel1">—</td></tr></tbody></table>
</details>
</main><div id="noConnection" role="status"><i class="connectionPulse" aria-hidden="true"></i><strong>Немає зв’язку</strong></div>
<script>
const el=id=>document.getElementById(id), buttons=[...document.querySelectorAll("[data-direction]")];
let token=0,busy=false,active=false,readyMotors=0,lastStateAt=0,connected=false;
function setConnected(value){
 connected=value;
 el("noConnection").hidden=value;
 document.querySelector("main").inert=!value;
}
const format=(value,digits)=>Number.isFinite(value)?value.toFixed(digits):"—";
function updateButtons(){
 const selected=+el("motors").value;
 buttons.forEach(button=>button.disabled=!connected||busy||active||+el("speed").value===0||(readyMotors&selected)!==selected);
 el("turns").disabled=el("metres").value!=="";
}
async function poll(){
 const requestedToken=token,controller=new AbortController();
 const timeout=setTimeout(()=>controller.abort(),500);
 try{
  const response=await fetch("/status",{cache:"no-store",signal:controller.signal});
  if(!response.ok)throw Error();
  const state=await response.json();
  lastStateAt=performance.now();
  if(!connected)setConnected(true);
  const faults=(state.motorFaults||[]).flatMap((reason,i)=>reason?["M"+(i+1)+" · "+reason]:[]);
  const message=faults.join("\n");
  if(el("driveFault").textContent!==message)el("driveFault").textContent=message;
  el("driveFault").hidden=!faults.length;
  const target=state.requestedMetres?state.requestedMetres+" м":state.requestedTurns?state.requestedTurns+" обертів":"";
  el("accepted").textContent=target?"Задано "+target+" · "+(state.direction>0?"вправо":"вліво"):"";
  el("warning").textContent=state.directionWarning||"";
  if(token===requestedToken){
   active=state.active;
   readyMotors=state.readyMotors;
   if(!state.active)token=0;
   if(!busy)el("status").textContent=state.message;
  }
  for(const key of ["count","goal","offset","error","brakeTravel","nextBrake","advance","brakeTarget","brakeCount"])
   for(let i=0;i<2;i++)el(key+i).textContent=state[key]?.[i]??"—";
  for(let i=0;i<2;i++){
   const ticks=state.ticksPerTurn?.[i], offset=state.offset?.[i], rpm=state.rpm?.[i];
   el("actualTurns"+i).textContent=format(ticks>0&&Number.isFinite(offset)?offset/ticks:NaN,3);
   el("rpm"+i).textContent=format(Math.abs(rpm??NaN),1);
   el("peakRpm"+i).textContent=format(state.peakRpm?.[i],1);
   el("travelCm"+i).textContent=format(state.travelCm?.[i],2);
  }
 }catch{}
 finally{clearTimeout(timeout)}
 updateButtons();
 setTimeout(poll,150);
}
buttons.forEach(button=>button.onclick=async()=>{
 const fields=["turns","speed","motors","brakeM1","brakeM2","ticksM1","ticksM2","approachM1","approachM2","metres"];
 const invalid=fields.map(el).find(field=>!field.validity.valid);
 if(invalid){
  if(invalid.closest("details"))el("settings").open=true;
  invalid.reportValidity();
  return;
 }
 const params=new URLSearchParams(fields.map(key=>[key,el(key).value]));
 params.set("direction",button.dataset.direction);
 params.set("smooth",el("smooth").checked?"1":"0");
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
el("metres").oninput=updateButtons;
function updateSpeed(){
 const speed=+el("speed").value;
 const pwm=speed===0?0:5+95*(speed-1)/99;
 el("speedValue").textContent=speed+"% · ШІМ "+pwm.toFixed(1)+"%";
 updateButtons();
}
el("speed").oninput=updateSpeed;
el("stop").onclick=()=>fetch("/stop",{method:"POST"}).catch(()=>{el("status").textContent="Немає зв’язку з ESP32"});
setConnected(false);
setInterval(()=>{
 if(connected&&performance.now()-lastStateAt>=500){setConnected(false);updateButtons()}
},100);
updateSpeed();
poll();
</script></html>
)HTML";

class MotorCalibration {
  enum class Phase { Idle, Running, Done, Failed };
  static constexpr int MaxSpeedPercent = 100, MaxTurns = 1000000;
  static constexpr uint32_t MinRunTimeoutMs = 20000, TimeoutPerTurnMs = 10000;
  static constexpr uint32_t QuietMs = 200;
  static constexpr uint32_t SampleMs = 100;
  static constexpr uint32_t WifiReportMs = 10000;
  static constexpr int MinDrivePercent = 5;
  static constexpr int ApproachPwmPercent[2] = {15, 15};
  // Encoder counts before the final target at which each motor starts braking.
  // Increase to brake earlier. Set to zero to brake at the target count.
  static constexpr int32_t M1BrakeAdvanceTicks = 0;
  static constexpr int32_t M2BrakeAdvanceTicks = 0;
  static_assert(M1BrakeAdvanceTicks >= 0 && M2BrakeAdvanceTicks >= 0,
                "Brake advance must be nonnegative");
  static constexpr int32_t BrakeAdvanceTicks[2] = {M1BrakeAdvanceTicks, M2BrakeAdvanceTicks};

 public:
  explicit MotorCalibration(bool carriage2 = false) : settings_(motorSettingsFor(carriage2)) {}

  void begin(const char *homeSsid = nullptr, const char *homePassword = nullptr) {
    Serial.begin(115200);
    homeWifi_ = homeSsid != nullptr;
    homeSsidConfigured_ = homeWifi_ && *homeSsid;
    log("BOOT", "%s", homeWifi_ ? "motor_calibration_home (STA)" : "motor_calibration (AP)");
    for (unsigned i = 0; i < 2; ++i) {
      const auto &cfg = settings_[i];
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
      if (!pwmReady_[i]) hardwareFault(i, MotorFault::PwmOutput);
      else if (!encoderReady_[i]) hardwareFault(i, MotorFault::EncoderRead);
    }
    if (homeWifi_) {
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      if (homeSsidConfigured_) {
        WiFi.begin(homeSsid, homePassword);
        log("WIFI", "Connecting to home Wi-Fi (2.4 GHz)");
      } else log("WIFI", "HOME_WIFI_SSID is empty");
    } else {
      WiFi.mode(WIFI_AP);
      apStarted_ = WiFi.softAP(SSID, Password, Channel);
      if (apStarted_) announceSite(WiFi.softAPIP());
      else log("WIFI", "Failed to start Motor-Control AP");
    }
    server_.on("/", HTTP_GET, [this] {
      String html(MotorCalibrationHtml);
      html.replace("__BRAKE_M1__", String(BrakeAdvanceTicks[0]));
      html.replace("__BRAKE_M2__", String(BrakeAdvanceTicks[1]));
      html.replace("__TICKS_M1__", String(defaultCountsPerTurn(0), 12));
      html.replace("__TICKS_M2__", String(defaultCountsPerTurn(1), 12));
      html.replace("__APPROACH_M1__", String(ApproachPwmPercent[0]));
      html.replace("__APPROACH_M2__", String(ApproachPwmPercent[1]));
      server_.sendHeader("Cache-Control", "no-store");
      server_.send(200, "text/html; charset=utf-8", html);
    });
    server_.on("/start", HTTP_POST, [this] { startRequested(); });
    server_.on("/status", HTTP_GET, [this] { sendStatus(); });
    server_.on("/stop", HTTP_POST, [this] {
      if (active()) finish(Phase::Failed, "Тест зупинено");
      for (auto &hasTarget : hasDistanceTarget_) hasTarget = false;
      server_.send(204);
    });
    server_.begin();
  }

  void tick() {
    const uint32_t now = millis();
    if (homeWifi_) serviceHomeWifi();
    for (unsigned i = 0; i < 2; ++i) {
      if (!encoderReady_[i]) continue;
      int64_t value;
      if (!encoders_[i].read(value)) {
        encoderReady_[i] = false;
        hardwareFault(i, MotorFault::EncoderRead);
      } else if (value != count_[i]) {
        count_[i] = value;
        changedAt_[i] = now;
      }
    }
    sampleTelemetry(now);
    if (active()) {
      if ((readyMotors() & selected_) != selected_) finish(Phase::Failed, selectedFault(selected_));
      else serviceMove(now);
    }
    if (!active() && now - wifiReportAt_ >= WifiReportMs) {
      wifiReportAt_ = now;
      reportWifi();
    }
    server_.handleClient();
  }

 private:
  void announceSite(IPAddress ip) {
    mdnsReady_ = MDNS.begin("motor-control");
    if (mdnsReady_) MDNS.addService("http", "tcp", 80);
    log("WEB", "http://%s/ | motor-control.local %s", ip.toString().c_str(),
        mdnsReady_ ? "OK" : "failed");
    wifiReportAt_ = millis();
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
    else log("WIFI", "Home Wi-Fi disconnected");
  }
  void reportWifi() {
    if (homeWifi_ && !homeSsidConfigured_) {
      log("WIFI", "Home STA: HOME_WIFI_SSID is empty");
      return;
    }
    const IPAddress ip = homeWifi_ ? homeIp_ : apStarted_ ? WiFi.softAPIP() : IPAddress();
    if (uint32_t(ip)) {
      if (!mdnsReady_) announceSite(ip);
      else log("WEB", "%s http://%s/ | motor-control.local OK", homeWifi_ ? "STA" : "AP", ip.toString().c_str());
      return;
    }
    if (!homeWifi_) {
      log("WIFI", "Motor-Control AP unavailable");
      return;
    }
    const wl_status_t status = WiFi.status();
    const char *detail = "waiting for connection";
    switch (status) {
      case WL_NO_SSID_AVAIL: detail = "SSID not found"; break;
      case WL_CONNECT_FAILED: detail = "connection failed"; break;
      case WL_CONNECTION_LOST: detail = "connection lost"; break;
      case WL_DISCONNECTED: detail = "disconnected"; break;
      case WL_CONNECTED: detail = "waiting for DHCP IP"; break;
      default: break;
    }
    log("WIFI", "Home STA: %s (%d)", detail, int(status));
  }
  void hardwareFault(unsigned i, MotorFault reason) {
    motorFaults_[i] = reason;
    log("FAULT", "M%u %s", i + 1, motorFaultMessage(reason));
  }
  const char *selectedFault(uint8_t mask) const {
    for (unsigned i = 0; i < 2; ++i)
      if (mask & (1U << i)) {
        if (!encoderReady_[i]) return motorFaultMessage(MotorFault::EncoderRead);
        if (!pwmReady_[i]) return motorFaultMessage(MotorFault::PwmOutput);
      }
    return "";
  }
  bool active() const { return phase_ == Phase::Running; }
  uint8_t readyMotors() const {
    uint8_t mask = 0;
    for (unsigned i = 0; i < 2; ++i)
      if (encoderReady_[i] && pwmReady_[i]) mask |= 1U << i;
    return mask;
  }
  bool selected(unsigned i) const { return selected_ & (1U << i); }
  double defaultCountsPerTurn(unsigned i) const {
    return settings_[i].countsPerWheelTurn > 0 ? settings_[i].countsPerWheelTurn : NominalCountsPerWheel;
  }
  double countsPerTurn(unsigned i) const {
    return runCountsPerTurn_[i] > 0 ? runCountsPerTurn_[i] : defaultCountsPerTurn(i);
  }
  void sampleTelemetry(uint32_t now) {
    const uint32_t elapsed = now - sampleAt_;
    if (elapsed < SampleMs) return;
    for (unsigned i = 0; i < 2; ++i) {
      if (!encoderReady_[i]) continue;
      rpm_[i] = float(count_[i] - sampleCount_[i]) * 60000.0f / (elapsed * countsPerTurn(i));
      sampleCount_[i] = count_[i];
      if (active() && selected(i) && !braking_[i]) peakRpm_[i] = max(peakRpm_[i], fabsf(rpm_[i]));
    }
    sampleAt_ = now;
  }
  void setPinsLow(unsigned i) {
    for (uint8_t pin : {settings_[i].pwmA, settings_[i].pwmB}) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
  }
  void write(unsigned i, int pwm) {
    if (!pwmReady_[i] || pwm == applied_[i]) return;
    const auto &cfg = settings_[i];
    const int physical = cfg.reverseMotor ? -pwm : pwm;
    const bool ok = physical > 0 ? ledcWrite(cfg.pwmB, 0) && ledcWrite(cfg.pwmA, physical) :
                                  ledcWrite(cfg.pwmA, 0) && ledcWrite(cfg.pwmB, -physical);
    if (!ok) {
      ledcDetach(cfg.pwmA);
      ledcDetach(cfg.pwmB);
      setPinsLow(i);
      pwmReady_[i] = false;
      hardwareFault(i, MotorFault::PwmOutput);
    }
    applied_[i] = ok ? pwm : 0;
    feedback_[i].update(applied_[i], count_[i], millis());
  }
  void brake() { write(0, 0); write(1, 0); }
  void finish(Phase result, const char *message) {
    brake();
    phase_ = result;
    message_ = message;
    log("ROTATION", "ended phase=%u", unsigned(result));
    log("ROTATION", "reason=%s", message);
    for (unsigned i = 0; i < 2; ++i) {
      if (result == Phase::Failed && selected(i)) hasDistanceTarget_[i] = false;
      if (selected(i)) log("ROTATION", "M%u final=%lld target=%lld elapsed=%llu ms", i + 1,
          (long long)llabs(count_[i] - start_[i]), (long long)goal_[i], (unsigned long long)elapsedMs());
    }
    log("CALIBRATION", "max output rpm M1=%.1f M2=%.1f", peakRpm_[0], peakRpm_[1]);
  }
  static int64_t roundTicks(double value) {
    const double lower = floor(value);
    return int64_t(lower + (value - lower > 0.5 ? 1 : 0));
  }
  double countsPerDistanceMetre(unsigned i, double ticksPerTurn) const {
    if (settings_[i].countsPerMeter > 0) return settings_[i].countsPerMeter;
    return ticksPerTurn * 1000.0 / settings_[i].wheelCircumferenceMm;
  }
  void startMove(int turns, double metres, int speed, const int64_t (&goals)[2], uint32_t now) {
    requestedTurns_ = turns;
    requestedMetres_ = metres;
    phase_ = Phase::Running;
    runStartedUs_ = esp_timer_get_time();
    const uint64_t estimatedTurns = metres > 0 ? uint64_t(ceil(metres * 1000 / WheelCircumferenceMm)) : uint64_t(turns);
    runTimeoutMs_ = max(uint64_t(MinRunTimeoutMs), estimatedTurns * TimeoutPerTurnMs);
    const double pwm = (MinDrivePercent + (100.0 - MinDrivePercent) * (speed - 1) / (MaxSpeedPercent - 1)) / 100.0;
    sampleAt_ = now;
    for (unsigned i = 0; i < 2; ++i) {
      start_[i] = sampleCount_[i] = count_[i];
      changedAt_[i] = now;
      if (encoderReady_[i] && pwmReady_[i]) motorFaults_[i] = MotorFault::None;
      braking_[i] = false;
      stopRecorded_[i] = false;
      brakeProgress_[i] = 0;
      directionWarning_[i] = false;
      peakRpm_[i] = secondsPerTurn_[i] = 0;
      goal_[i] = goals[i];
      maxDuty_[i] = lround(1023 * min(pwm, double(constrain(settings_[i].dutyMultiplier, 0.0f, 1.0f))));
      if (selected(i) && !metres) hasDistanceTarget_[i] = false;
    }
    message_ = direction_ > 0 ? "Рух вправо" : "Рух вліво";
    log("ROTATION", "start motors=%u dir=%d PWM<=%.1f%% goal=%lld/%lld",
        selected_, direction_, pwm * 100, (long long)goal_[0], (long long)goal_[1]);
  }
  uint64_t elapsedMs() const { return uint64_t(esp_timer_get_time() - runStartedUs_) / 1000; }

  bool settleWheel(unsigned i, int64_t progress, uint32_t now) {
    if (now - brakeAt_[i] < QuietMs || now - changedAt_[i] < QuietMs) return false;
    if (!stopRecorded_[i]) {
      stopRecorded_[i] = true;
      const double turns = progress / countsPerTurn(i);
      const uint64_t elapsed = elapsedMs();
      const uint32_t quiet = now - changedAt_[i];
      if (turns > 0 && elapsed >= quiet) secondsPerTurn_[i] = (elapsed - quiet) / (1000.0 * turns);
    }
    return true;
  }
  int64_t brakeAdvance(unsigned i) const {
    return min(goal_[i] - 1, int64_t(runBrakeTicks_[i]));
  }
  bool serviceWheel(unsigned i, int64_t progress, uint32_t now) {
    const int64_t remaining = goal_[i] - progress;
    const int64_t advance = brakeAdvance(i);
    if (braking_[i])
      return settleWheel(i, progress, now);
    if (remaining <= advance) {
      braking_[i] = true;
      brakeAt_[i] = now;
      brakeProgress_[i] = progress;
      brakeRpm_[i] = fabsf(rpm_[i]);
      encoderDirection_[i] = count_[i] < start_[i] ? -1 : 1;
      write(i, 0);
      log("BRAKE", "M%u LOW/LOW counts=%lld/%lld", i + 1, (long long)progress, (long long)goal_[i]);
      log("BRAKE", "M%u advance=%lld ticks", i + 1, (long long)advance);
      return false;
    }
    int64_t feedbackProgress;
    if (feedback_[i].sample(count_[i], now, feedbackProgress) && feedbackProgress == 0) {
      hardwareFault(i, MotorFault::NoFeedback);
      finish(Phase::Failed, motorFaultMessage(MotorFault::NoFeedback));
      return false;
    }
    const int ceiling = maxDuty_[i];
    if (!smooth_) {
      write(i, direction_ * ceiling);
      return false;
    }
    const double approachFraction = min(1.0, (remaining - advance) / (0.5 * countsPerTurn(i)));
    const int approach = max(min(runApproachDuty_[i], ceiling), int(ceiling * approachFraction));
    const int startDuty = min(ceiling, (1023 * MinDrivePercent + 50) / 100);
    const int ramp = startDuty + int(min(elapsedMs(), uint64_t(700)) * (ceiling - startDuty) / 700);
    write(i, direction_ * min(approach, ramp));
    return false;
  }
  void serviceMove(uint32_t now) {
    if (elapsedMs() >= runTimeoutMs_) {
      finish(Phase::Failed, "Вичерпано час руху");
      return;
    }
    const int64_t signedProgress[2] = {direction_ * (count_[0] - start_[0]), direction_ * (count_[1] - start_[1])};
    int64_t progress[2]{};
    bool settled = true;
    for (unsigned i = 0; i < 2; ++i) {
      if (!selected(i)) continue;
      progress[i] = braking_[i] ? encoderDirection_[i] * (count_[i] - start_[i]) : llabs(signedProgress[i]);
      if (!braking_[i] && signedProgress[i] < -6 && !directionWarning_[i]) {
        directionWarning_[i] = true;
        log("WARNING", "M%u encoder sign reversed; calibration continues using absolute displacement", i + 1);
      }
      settled &= serviceWheel(i, progress[i], now);
      if (!active()) return;
    }
    if ((readyMotors() & selected_) != selected_) {
      finish(Phase::Failed, selectedFault(selected_));
      return;
    }
    if (!settled) return;
    bool exact = true;
    for (unsigned i = 0; i < 2; ++i) {
      if (!selected(i)) continue;
      exact &= progress[i] == goal_[i];
      stoppedProgress_[i] = progress[i];
      if (requestedMetres_ > 0) {
        if (!hasDistanceTarget_[i]) {
          distanceEncoderSign_[i] = encoderDirection_[i] * direction_;
          idealDistanceTicks_[i] = start_[i];
        }
        idealDistanceTicks_[i] += encoderDirection_[i] * (requestedMetres_ * countsPerDistanceMetre(i, countsPerTurn(i)));
        hasDistanceTarget_[i] = true;
      }
      log("ROTATION", "M%u counts=%lld/%lld secondsPerTurn=%.2f", i + 1,
          (long long)progress[i], (long long)goal_[i], secondsPerTurn_[i]);
      log("BRAKE", "M%u travel after brake=%lld ticks", i + 1, (long long)(progress[i] - brakeProgress_[i]));
    }
    finish(Phase::Done, exact ? "Ціль досягнуто за енкодером · звір мітки" : "Зупинка з похибкою · перевір відліки й мітки");
  }
  void startRequested() {
    const int turns = boundedValue("turns", MaxTurns);
    const String metresArgument = server_.arg("metres");
    char *metresEnd = nullptr;
    const double metres = metresArgument.isEmpty() ? 0 : strtod(metresArgument.c_str(), &metresEnd);
    const int speed = boundedValue("speed", MaxSpeedPercent);
    const uint8_t motors = boundedValue("motors", 3);
    const int32_t brakeM1 = brakeTicksArgument("brakeM1");
    const int32_t brakeM2 = brakeTicksArgument("brakeM2");
    const double ticksPerTurn[2] = {ticksPerTurnArgument("ticksM1"), ticksPerTurnArgument("ticksM2")};
    const int approachPercent[2] = {boundedValue("approachM1", 100), boundedValue("approachM2", 100)};
    const String smooth = server_.arg("smooth");
    const String direction = server_.arg("direction");
    const uint32_t now = millis();
    JsonDocument d;
    const bool validDistance = metresArgument.isEmpty() ||
      (metresEnd && !*metresEnd && isfinite(metres) && metres >= 0.01 && metres <= 100000);
    if (!validDistance || (!metres && !turns) || !speed || !motors || brakeM1 < 0 || brakeM2 < 0 ||
        !ticksPerTurn[0] || !ticksPerTurn[1] || approachPercent[0] < MinDrivePercent || approachPercent[1] < MinDrivePercent ||
        (smooth != "0" && smooth != "1") || (direction != "right" && direction != "left")) {
      d["message"] = "Перевір відстань або оберти, мотор, ШІМ, тіки за оберт і випередження гальма";
      sendJson(400, d);
      return;
    }
    if (active() || (readyMotors() & motors) != motors) {
      d["message"] = active() ? "Тест уже триває" : selectedFault(motors);
      sendJson(409, d);
      return;
    }
    const int directionSign = direction == "right" ? 1 : -1;
    int64_t goals[2]{};
    for (unsigned i = 0; i < 2; ++i) {
      if (!(motors & (1U << i))) continue;
      const double ticksPerMetre = countsPerDistanceMetre(i, ticksPerTurn[i]);
      if (metres > 0 && hasDistanceTarget_[i] && ticksPerTurn[i] == countsPerTurn(i)) {
        const int encoderSign = distanceEncoderSign_[i];
        const double ideal = encoderSign * idealDistanceTicks_[i] + directionSign * metres * ticksPerMetre;
        goals[i] = directionSign * (roundTicks(ideal) - encoderSign * count_[i]);
      } else {
        goals[i] = roundTicks(metres > 0 ? metres * ticksPerMetre : turns * ticksPerTurn[i]);
      }
      if (goals[i] <= 0) {
        d["message"] = "Попередня похибка перекриває цей хід. Задай більшу відстань або скинь серію кнопкою СТОП.";
        sendJson(400, d);
        return;
      }
    }
    selected_ = motors;
    for (unsigned i = 0; i < 2; ++i) {
      if (!selected(i)) continue;
      if (countsPerTurn(i) != ticksPerTurn[i]) hasDistanceTarget_[i] = false;
      runCountsPerTurn_[i] = ticksPerTurn[i];
      runApproachDuty_[i] = (1023 * approachPercent[i] + 50) / 100;
    }
    runBrakeTicks_[0] = brakeM1;
    runBrakeTicks_[1] = brakeM2;
    smooth_ = smooth == "1";
    direction_ = directionSign;
    token_ = esp_random() | 1U;
    startMove(metres > 0 ? 0 : turns, metres, speed, goals, now);
    d["run"] = token_;
    sendJson(200, d);
  }
  static constexpr double MaxTicksPerTurn = 1000000;
  double ticksPerTurnArgument(const char *name) {
    const String value = server_.arg(name);
    char *end = nullptr;
    const double ticks = strtod(value.c_str(), &end);
    return end && !*end && ticks >= 1 && ticks <= MaxTicksPerTurn ? ticks : 0;
  }
  int32_t brakeTicksArgument(const char *name) {
    const String value = server_.arg(name);
    if (value.isEmpty()) return -1;
    char *end = nullptr;
    const long long number = strtoll(value.c_str(), &end, 10);
    if (!end || *end || number < 0 || number > INT32_MAX || value != String(long(number))) return -1;
    return int32_t(number);
  }
  int boundedValue(const char *name, int maxValue) {
    const String value = server_.arg(name);
    const long number = value.toInt();
    return number >= 1 && number <= maxValue && value == String(number) ? int(number) : 0;
  }
  void sendStatus() {
    JsonDocument d;
    d["readyMotors"] = readyMotors();
    d["active"] = active();
    d["requestedTurns"] = requestedTurns_;
    d["requestedMetres"] = requestedMetres_;
    d["direction"] = direction_;
    d["directionWarning"] = directionWarning_[0] && directionWarning_[1] ? "M1 і M2: знак енкодера протилежний команді. Тест продовжується." :
      directionWarning_[0] ? "M1: знак енкодера протилежний команді. Тест продовжується." :
      directionWarning_[1] ? "M2: знак енкодера протилежний команді. Тест продовжується." : "";
    d["message"] = message_;
    auto faults = d["motorFaults"].to<JsonArray>();
    for (MotorFault reason : motorFaults_) faults.add(motorFaultMessage(reason));
    auto counts = d["count"].to<JsonArray>();
    auto goals = d["goal"].to<JsonArray>();
    auto ticksPerTurn = d["ticksPerTurn"].to<JsonArray>();
    auto advances = d["advance"].to<JsonArray>();
    auto brakeTargets = d["brakeTarget"].to<JsonArray>();
    auto brakeCounts = d["brakeCount"].to<JsonArray>();
    auto offsets = d["offset"].to<JsonArray>();
    auto errors = d["error"].to<JsonArray>();
    auto errorsMm = d["errorMm"].to<JsonArray>();
    auto travelCm = d["travelCm"].to<JsonArray>();
    auto brakeTravel = d["brakeTravel"].to<JsonArray>();
    auto nextBrake = d["nextBrake"].to<JsonArray>();
    auto brakeRpm = d["brakeRpm"].to<JsonArray>();
    auto rpm = d["rpm"].to<JsonArray>();
    auto peaks = d["peakRpm"].to<JsonArray>();
    auto peakKmh = d["peakKmh"].to<JsonArray>();
    auto seconds = d["secondsPerTurn"].to<JsonArray>();
    for (unsigned i = 0; i < 2; ++i) {
      counts.add(count_[i]);
      if (encoderReady_[i]) rpm.add(rpm_[i]);
      else rpm.add(nullptr);
      if (token_ && selected(i)) {
        peaks.add(peakRpm_[i]);
        peakKmh.add(peakRpm_[i] * settings_[i].wheelCircumferenceMm * 0.00006);
        goals.add(goal_[i]);
        ticksPerTurn.add(countsPerTurn(i));
        advances.add(brakeAdvance(i));
        brakeTargets.add(goal_[i] - brakeAdvance(i));
        offsets.add(phase_ == Phase::Done ? stoppedProgress_[i] : llabs(count_[i] - start_[i]));
      } else {
        peaks.add(nullptr);
        peakKmh.add(nullptr);
        goals.add(nullptr);
        ticksPerTurn.add(nullptr);
        advances.add(nullptr);
        brakeTargets.add(nullptr);
        offsets.add(nullptr);
      }
      if (phase_ == Phase::Done && selected(i)) seconds.add(secondsPerTurn_[i]);
      else seconds.add(nullptr);
      if (selected(i) && braking_[i]) {
        brakeRpm.add(brakeRpm_[i]);
        brakeCounts.add(brakeProgress_[i]);
      } else {
        brakeRpm.add(nullptr);
        brakeCounts.add(nullptr);
      }
      if (phase_ == Phase::Done && selected(i)) {
        const int64_t error = stoppedProgress_[i] - goal_[i];
        brakeTravel.add(stoppedProgress_[i] - brakeProgress_[i]);
        nextBrake.add(constrain(brakeAdvance(i) + error, int64_t(0), min(goal_[i] - 1, int64_t(INT32_MAX))));
        errors.add(error);
        const double ticksPerMetre = countsPerDistanceMetre(i, countsPerTurn(i));
        errorsMm.add(error * 1000.0 / ticksPerMetre);
        travelCm.add(stoppedProgress_[i] * 100.0 / ticksPerMetre);
      } else {
        brakeTravel.add(nullptr);
        nextBrake.add(nullptr);
        errors.add(nullptr);
        errorsMm.add(nullptr);
        travelCm.add(nullptr);
      }
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
  bool homeWifi_ = false, homeSsidConfigured_ = false, apStarted_ = false, mdnsReady_ = false;
  uint32_t wifiReportAt_ = 0;
  const MotorSettings (&settings_)[2];
  Encoder encoders_[2];
  MotorFeedbackCheck feedback_[2];
  MotorFault motorFaults_[2]{};
  Phase phase_ = Phase::Idle;
  const char *message_ = "Оберіть кількість обертів";
  bool encoderReady_[2]{}, pwmReady_[2]{}, braking_[2]{};
  bool directionWarning_[2]{}, stopRecorded_[2]{};
  bool smooth_ = true;
  bool hasDistanceTarget_[2]{};
  int8_t distanceEncoderSign_[2]{};
  double idealDistanceTicks_[2]{}, requestedMetres_ = 0;
  int8_t encoderDirection_[2]{};
  uint32_t brakeAt_[2]{};
  uint8_t selected_ = 0;
  int direction_ = 1;
  int requestedTurns_ = 0;
  int32_t runBrakeTicks_[2] = {M1BrakeAdvanceTicks, M2BrakeAdvanceTicks};
  int64_t runStartedUs_ = 0;
  uint64_t runTimeoutMs_ = 0;
  uint32_t changedAt_[2]{}, token_ = 0;
  int applied_[2]{}, maxDuty_[2]{};
  int runApproachDuty_[2]{};
  double runCountsPerTurn_[2]{};
  int64_t goal_[2]{}, count_[2]{}, start_[2]{}, stoppedProgress_[2]{}, brakeProgress_[2]{};
  uint32_t sampleAt_ = 0;
  int64_t sampleCount_[2]{};
  float rpm_[2]{}, peakRpm_[2]{}, brakeRpm_[2]{}, secondsPerTurn_[2]{};
};
}
