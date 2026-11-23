#pragma once
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "Motion.h"
#include "WebUi.h"

namespace bk370 {
constexpr uint32_t RadioMagic = 0x424b3702;
constexpr uint32_t LinkTimeout = 600;
struct Packet {
  uint32_t magic = RadioMagic, sender = 0, receiver = 0, sequence = 0;
  Command command{};
  Status status{};
  uint8_t statusPacket = 0, accepted = 0;
};
static_assert(sizeof(Packet) <= 250, "ESP-NOW packet exceeds the v1 payload limit");
struct Frame { Packet packet; uint32_t receivedAt; int8_t rssi; };

template<int Role>
class Firmware {
 public:
  void begin() {
    Serial.begin(115200);
    local_.begin();
    boot_ = esp_random() | 1U;
    WiFi.mode(Role == 2 ? WIFI_STA : WIFI_AP_STA);
    WiFi.setSleep(false);
    const String mac = WiFi.macAddress();
    log("WIFI", "station MAC=%s", mac.c_str());
    if constexpr (Role != 2) {
      WiFi.softAP(SSID, Password, Channel);
      if (MDNS.begin("motor-control")) MDNS.addService("http", "tcp", 80);
      server_.on("/", [this] { String html(WEB_HTML); html.replace("__SINGLE__", Role == 0 ? "true" : "false"); server_.send(200, "text/html; charset=utf-8", html); });
      server_.begin();
      socket_.begin();
      socket_.onEvent([this](uint8_t client, WStype_t type, uint8_t *data, size_t length) {
        if (type == WStype_TEXT) receiveUi(client, data, length);
        else if (type == WStype_DISCONNECTED && client == owner_) {
          stopBoth(millis(), "web disconnect");
          owner_ = NoOwner;
        }
      });
      log("WEB", "http://motor-control.local/ or http://192.168.4.1/");
    }
    if constexpr (Role != 0) startRadio();
  }

  void tick() {
    if constexpr (Role != 2) {
      server_.handleClient();
      socket_.loop();
      if (owner_ != NoOwner && millis() - ownerAt_ > 1500) {
        stopBoth(millis(), "web timeout");
        owner_ = NoOwner;
      }
    }
    const uint32_t now = millis();
    if constexpr (Role != 0) receiveRadio(now);
    const bool online = remoteBoot_ && now - radioAt_ < LinkTimeout;
    if constexpr (Role == 2) {
      if (!online) remoteArmed_ = false;
      local_.tick(now, online && remoteArmed_);
      if (now - sentAt_ >= 80) sendStatus(now);
    } else {
      if constexpr (Role == 1) {
        if (!online && wasOnline_) {
          if (linked_) local_.stop(now, "linked carriage lost");
          linked_ = false;
          pending_ = Command{};
          sequence_++;
        }
        if (pending_.op != Op::Idle && pending_.op != Op::Abort && now - pendingAt_ >= 500) {
          notifyClient("Немає підтвердження від другої каретки");
          if (pending_.op != Op::LightTest) {
            if (linked_) local_.stop(now, "command acknowledgment timeout");
            linked_ = false;
            pending_ = Command{};
            pending_.op = Op::Abort;
          } else {
            pending_ = Command{};
          }
          sequence_++;
        }
        if (now - sentAt_ >= 45) sendCommand(now);
      }
      local_.tick(now, true);
      if constexpr (Role == 1) {
        const auto localState = local_.status(now);
        if (linked_ && (!localState.powered || !remoteState_.powered)) {
          stopBoth(now, "linked interlock");
          linked_ = false;
        }
      }
      if (now - broadcastAt_ >= 100) broadcast(now);
    }
    wasOnline_ = online;
  }
 private:
  static constexpr uint8_t NoOwner = 255;
  static constexpr const uint8_t *peer() { return Role == 1 ? RemoteMac : MainMac; }
  static inline QueueHandle_t frames_ = nullptr;
  static void onRadio(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
    if (!frames_ || !info || !data || length != sizeof(Packet) ||
        memcmp(info->src_addr, peer(), 6) != 0) return;
    Frame frame{};
    memcpy(&frame.packet, data, sizeof(Packet));
    if (frame.packet.magic != RadioMagic || !frame.packet.sender) return;
    frame.receivedAt = millis();
    frame.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -127;
    xQueueSend(frames_, &frame, 0);
  }
  void startRadio() {
    if constexpr (Role == 2) esp_wifi_set_channel(Channel, WIFI_SECOND_CHAN_NONE);
    const uint8_t empty[6]{};
    if (memcmp(peer(), empty, 6) == 0) {
      log("RADIO", "set station MAC addresses in Settings.h");
      return;
    }
    frames_ = xQueueCreate(10, sizeof(Frame));
    if (!frames_ || esp_now_init() != ESP_OK) return;
    esp_now_peer_info_t p{};
    memcpy(p.peer_addr, peer(), 6);
    p.channel = Channel;
    p.ifidx = WIFI_IF_STA;
    radioReady_ = esp_now_add_peer(&p) == ESP_OK &&
      esp_now_register_recv_cb(onRadio) == ESP_OK;
    log("RADIO", "ready=%d", radioReady_);
  }
  void send(Packet &packet, uint32_t now) {
    sentAt_ = now;
    if (!radioReady_) return;
    packet.sender = boot_;
    packet.receiver = remoteBoot_;
    esp_now_send(peer(), reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  }
  void sendCommand(uint32_t now) {
    if (!remoteBoot_) return;
    Packet p;
    p.sequence = sequence_;
    p.command = pending_;
    send(p, now);
  }
  void sendStatus(uint32_t now) {
    Packet p;
    p.statusPacket = 1;
    p.sequence = appliedSeq_;
    p.accepted = commandAccepted_;
    p.status = local_.status(now);
    send(p, now);
  }
  void receiveRadio(uint32_t now) {
    Frame f{};
    if (!frames_) return;
    for (unsigned i = 0; i < 10 && xQueueReceive(frames_, &f, 0) == pdTRUE; ++i) {
      if (int32_t(now - f.receivedAt) > 200) continue;
      const uint32_t receivedAt = int32_t(now - f.receivedAt) < 0 ? now : f.receivedAt;
      const auto &p = f.packet;
      if constexpr (Role == 2) {
        if (p.statusPacket || p.receiver != boot_) continue;
        if (p.sender != remoteBoot_ || !remoteArmed_) {
          if (p.command.op != Op::Idle && p.command.op != Op::Stop && p.command.op != Op::Abort) continue;
          local_.stop(now, "radio session established");
          remoteBoot_ = p.sender;
          appliedSeq_ = p.sequence;
          remoteArmed_ = true;
          commandAccepted_ = true;
        }
        if (int32_t(p.sequence - appliedSeq_) < 0) continue;
        radioAt_ = receivedAt;
        if (p.sequence == appliedSeq_) continue;
        appliedSeq_ = p.sequence;
        commandAccepted_ = p.command.op == Op::Idle || local_.execute(p.command, now);
        if (!commandAccepted_) log("RADIO", "rejected operation=%u", unsigned(p.command.op));
      } else {
        if (!p.statusPacket || (p.receiver != 0 && p.receiver != boot_)) continue;
        remoteHandshake_ = p.receiver == boot_;
        if (remoteBoot_ != p.sender) {
          if (linked_) local_.stop(now, "remote restarted");
          linked_ = false;
          remoteBoot_ = p.sender;
          pending_ = Command{};
          sequence_++;
        }
        remoteState_ = p.status;
        remoteRssi_ = f.rssi;
        radioAt_ = receivedAt;
        if (p.receiver == boot_ && p.sequence == sequence_ && pending_.op != Op::Idle) {
          if (!p.accepted) {
            notifyClient("Р”СЂСѓРіР° РєР°СЂРµС‚РєР° РІС–РґС…РёР»РёР»Р° РєРѕРјР°РЅРґСѓ");
            if (linked_ && pending_.op != Op::LightTest) {
              stopBoth(now, "remote rejected command");
              linked_ = false;
              continue;
            }
          }
          pending_ = Command{};
          sequence_++;
        }
      }
    }
  }
  void notifyClient(const char *msg) {
    if (owner_ == NoOwner) return;
    JsonDocument d;
    d["type"] = "notice";
    d["message"] = msg;
    String payload;
    serializeJson(d, payload);
    socket_.sendTXT(owner_, payload);
  }
  void stopBoth(uint32_t now, const char *reason) {
    local_.stop(now, reason);
    if constexpr (Role == 1) {
      pending_ = Command{};
      pending_.op = Op::Abort;
      sequence_++;
      sendCommand(now);
    }
  }
  void receiveUi(uint8_t client, uint8_t *data, size_t length) {
    if (length > 384) return;
    JsonDocument d;
    if (deserializeJson(d, data, length)) return;
    const char *type = d["type"] | "";
    const uint32_t now = millis();
    if (owner_ != NoOwner && owner_ != client && strcmp(type, "stop")) return;
    if (owner_ == NoOwner) owner_ = client;
    if (owner_ == client) ownerAt_ = now;
    if (!strcmp(type, "heartbeat")) return;
    const int selected = d["carriage"] | 1;
    if (selected < 1 || selected > (Role == 0 ? 1 : 2)) return;
    const bool online = remoteHandshake_ && remoteBoot_ && now - radioAt_ < LinkTimeout;
    const bool requestingLink = !strcmp(type, "link");
    if (requestingLink) {
      if constexpr (Role == 1) linked_ = online && (d["enabled"] | false);
      return;
    }
    Command c;
    const auto current = selected == 1 ? local_.status(now) : remoteState_;
    c.speed = constrain(d["speed"] | int(current.speed), 0, 100);
    const char *direction = d["direction"] | "";
    c.direction = !strcmp(direction, "right") ? 1 : !strcmp(direction, "left") ? -1 : 0;
    if (!strcmp(type, "stop")) c.op = Op::Stop;
    else if (!strcmp(type, "speed")) c.op = Op::Speed;
    else if (!strcmp(type, "target")) {
      c.op = Op::Target;
      if (!d["position"].is<float>()) return;
      c.metres = d["position"].as<float>();
    } else if (!strcmp(type, "start")) c.op = Op::Start;
    else if (!strcmp(type, "program")) {
      c.op = Op::Program;
      c.value = constrain(d["id"] | 1, 1, 4);
    } else if (!strcmp(type, "servo")) {
      c.op = Op::Servo;
      const int angle = d["angle"] | 45;
      if (angle != 45 && angle != 90) return;
      c.value = (d["enabled"] | true) ? angle : 0;
    } else if (!strcmp(type, "jog")) c.op = c.direction ? Op::Jog : Op::MotorStop;
    else if (!strcmp(type, "brake")) c.op = Op::MotorStop;
    else if (!strcmp(type, "light_test")) {
      const char *output = d["output"] | "";
      if (!strcmp(output, "red")) c.value = uint8_t(LightTest::Red);
      else if (!strcmp(output, "green")) c.value = uint8_t(LightTest::Green);
      else if (!strcmp(output, "mosfet")) c.value = uint8_t(LightTest::Strobe);
      else return;
      c.op = Op::LightTest;
    }
    else if (!strcmp(type, "zero")) c.op = Op::Zero;
    else if (!strcmp(type, "battery_reset")) c.op = Op::Reset;
    else if (!strcmp(type, "timed_test")) {
      c.op = Op::Timed;
      c.duration = d["durationMs"] | 0UL;
      if (c.duration != 1000 && c.duration != 2000 && c.duration != 5000 && c.duration != 10000) return;
    } else return;

    if (c.op == Op::Stop && owner_ != client) { stopBoth(now, "stop from another client"); return; }
    if (c.op == Op::Timed && linked_) { stopBoth(now, "calibration"); linked_ = false; }
    const bool synchronized = linked_ && c.op != Op::LightTest;
    const bool local = selected == 1 || synchronized;
    const bool remote = selected == 2 || synchronized;
    if (remote && !online && c.op != Op::Stop) {
      notifyClient("Р”СЂСѓРіР° РєР°СЂРµС‚РєР° РѕС„Р»Р°Р№РЅ");
      return;
    }
    const bool stopsMotors = c.op == Op::Stop || c.op == Op::MotorStop || (c.op == Op::Speed && !c.speed);
    if (synchronized && !stopsMotors && c.op != Op::Reset && c.op != Op::Zero && c.op != Op::Servo &&
        (!local_.status(now).ready || !remoteState_.ready)) {
      notifyClient("Р СѓС… Р·Р°Р±Р»РѕРєРѕРІР°РЅРѕ РЅР° РѕРґРЅС–Р№ С–Р· РєР°СЂРµС‚РѕРє");
      return;
    }
    if (local && !local_.execute(c, now)) {
      notifyClient("РљРѕРјР°РЅРґСѓ РІС–РґС…РёР»РµРЅРѕ. РџРµСЂРµРІС–СЂС‚Рµ Р¶РёРІР»РµРЅРЅСЏ, РєР°Р»С–Р±СЂСѓРІР°РЅРЅСЏ С‚Р° СЃС‚Р°РЅ РїСЂРёРІРѕРґС–РІ.");
      if (synchronized) stopBoth(now, "local rejected command");
      return;
    }
    if (remote) {
      pending_ = c;
      pendingAt_ = now;
      sequence_++;
      sendCommand(now);
    }
  }
  void serializeStatus(JsonObject obj, const Status &s, bool online, int8_t rssi) {
    obj["online"] = online;
    obj["rssi"] = rssi;
    obj["position"] = s.position;
    obj["target"] = s.target;
    obj["outputPwm"] = s.pwm[0] ? s.pwm[0] : s.pwm[1];
    obj["speed"] = s.speed;
    obj["program"] = s.program;
    obj["servoAngle"] = s.servoSelected;
    obj["servoPosition"] = s.servoTarget;
    obj["servoEnabled"] = s.servo;
    obj["servoTravel90Ms"] = ServoTravel90Ms;
    obj["lightTest"] = uint8_t(s.lightTest);
    obj["mode"] = s.mode == Mode::Idle ? "СЃС‚РѕРї" : s.mode == Mode::Timed ? "С‚РµСЃС‚ С‡Р°СЃСѓ" : "СЂСѓС…";
    obj["testRemainingMs"] = s.remainingMs;
    obj["centered"] = s.centered;
    obj["calibrated"] = s.calibrated;
    obj["ready"] = s.ready;
    obj["powered"] = s.powered;
    obj["locked"] = s.locked;
    obj["fault"] = s.fault;
    if (!online) return;
    obj["enc1"] = s.count[0];
    obj["enc2"] = s.count[1];
    obj["countsPerSec1"] = s.countsPerSecond[0];
    obj["countsPerSec2"] = s.countsPerSecond[1];
    if (s.calibrated) {
      obj["wheelSpeed1"] = s.wheelMps[0];
      obj["wheelSpeed2"] = s.wheelMps[1];
    }
    if (s.maxMps > 0) obj["maxSpeed"] = s.maxMps;
    if (s.currentValid) obj["amps"] = s.amps;
    if (s.mv) {
      obj["volts"] = s.mv / 1000.0f;
      obj["charge"] = constrain(100.0f * (int(s.mv) - EmptyMv) / (FullMv - EmptyMv), 0.0f, 100.0f);
    }
  }
  void broadcast(uint32_t now) {
    broadcastAt_ = now;
    if (!socket_.connectedClients()) return;
    JsonDocument d;
    d["type"] = "state";
    d["linked"] = linked_;
    const bool online = remoteBoot_ && now - radioAt_ < LinkTimeout;
    d["nodeOnline"] = online && remoteHandshake_;
    auto a = d["carriages"].to<JsonArray>();
    serializeStatus(a.add<JsonObject>(), local_.status(now), true, -55);
    if constexpr (Role == 1) serializeStatus(a.add<JsonObject>(), remoteState_, online && remoteHandshake_, remoteRssi_);
    String msg;
    serializeJson(d, msg);
    socket_.broadcastTXT(msg);
  }
  Carriage local_;
  WebServer server_{80};
  WebSocketsServer socket_{81};
  Command pending_{};
  Status remoteState_{};
  uint32_t boot_ = 0, remoteBoot_ = 0, radioAt_ = 0, appliedSeq_ = 0, sequence_ = 1;
  uint32_t pendingAt_ = 0, sentAt_ = 0, broadcastAt_ = 0, ownerAt_ = 0;
  uint8_t owner_ = NoOwner;
  int8_t remoteRssi_ = -127;
  bool remoteHandshake_ = false;
  bool linked_ = false, wasOnline_ = false, radioReady_ = false, remoteArmed_ = false, commandAccepted_ = false;
};

template<int Role> inline Firmware<Role> instance;
template<int Role> void begin() { instance<Role>.begin(); }
template<int Role> void tick() { instance<Role>.tick(); }
}
