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
#include "LogPage.h"
#ifdef BK_MIXED_GAN
#include <PairSettings.h>
#if BK_ROLE == 2
#include <GanCarriage.h>
#endif
#endif

namespace bk370 {
constexpr uint32_t RadioMagic = 0x424b370d;
#ifdef BK_HOME_WIFI
constexpr bool HomeWifi = true;
#else
constexpr bool HomeWifi = false;
#endif
#ifdef BK_SHARED_CABLE
constexpr bool SharedCable = true;
#else
constexpr bool SharedCable = false;
#endif
#ifdef BK_MIXED_GAN
constexpr bool MixedGan = true;
constexpr auto &StationSsid = pair::WifiSsid;
constexpr auto &StationPassword = pair::WifiPassword;
#else
constexpr bool MixedGan = false;
constexpr auto &StationSsid = HOME_WIFI_SSID;
constexpr auto &StationPassword = HOME_WIFI_PASSWORD;
#endif
static_assert(!MixedGan || !SharedCable, "The GAN requires a separate cable");
constexpr uint8_t PairProfile = (HomeWifi ? 1 : 0) | (SharedCable ? 2 : 0) | (MixedGan ? 4 : 0);
constexpr uint32_t LinkTimeout = 600;
struct Packet {
  uint32_t magic = RadioMagic, sender = 0, receiver = 0, sequence = 0;
  Command command{};
  Status status{};
  uint32_t logSequence = 0;
  uint8_t statusPacket = 0, accepted = 0, profile = PairProfile, pairPermit = 0;
  char logLine[EventLineSize]{};
};
static_assert(sizeof(Packet) <= 250, "ESP-NOW packet exceeds the v1 payload limit");
struct Frame { Packet packet; uint32_t receivedAt; int8_t rssi; };

template<int Role, bool UseIna226>
class Firmware {
 public:
  void begin() {
    eventLog.begin(Role == 2 ? 2 : 1);
    log("BOOT", "role=%d Wi-Fi=%s INA226=%s serial=115200", Role, HomeWifi ? "STA" : "AP", UseIna226 ? "on" : "off");
    local_.begin(SharedCable && Role != 0 ? (Role == 1 ? -1 : 1) : 0, Role == 2);
    boot_ = esp_random() | 1U;
    WiFi.mode(HomeWifi || Role == 2 ? WIFI_STA : WIFI_AP_STA);
    WiFi.setSleep(false);
    const String mac = WiFi.macAddress();
    log("WIFI", "station MAC=%s", mac.c_str());
    if constexpr (HomeWifi) {
      WiFi.setAutoReconnect(true);
      if (StationSsid[0]) WiFi.begin(StationSsid, StationPassword);
      else log("WIFI", "%s", MixedGan ? "set WifiSsid and WifiPassword in PairSettings.h" :
          "set HOME_WIFI_SSID and HOME_WIFI_PASSWORD in Settings.h");
    }
    if constexpr (Role != 2) {
      if constexpr (!HomeWifi) {
        WiFi.softAP(SSID, Password, Channel);
        startMdns();
      }
      server_.on("/", [this] { String html(WEB_HTML); html.replace("__SINGLE__", Role == 0 ? "true" : "false"); html.replace("__MIXED_GAN__", MixedGan ? "true" : "false"); html.replace("__SHARED__", SharedCable && Role != 0 ? "true" : "false"); server_.send(200, "text/html; charset=utf-8", html); });
      server_.on("/logs", HTTP_GET, [this] { server_.send(200, "text/html; charset=utf-8", LOG_HTML); });
      server_.on("/logs/events", HTTP_GET, [this] { serveEventLog(); });
      server_.begin();
      socket_.begin();
      socket_.onEvent([this](uint8_t client, WStype_t type, uint8_t *data, size_t length) {
        if (type == WStype_TEXT) receiveUi(client, data, length);
        else if (type == WStype_DISCONNECTED && client == owner_) owner_ = NoOwner;
      });
      if constexpr (!HomeWifi) log("WEB", "http://motor-control.local/ or http://192.168.4.1/");
    }
    if constexpr (Role != 0) startRadio();
  }

  void tick() {
    if constexpr (HomeWifi) serviceHomeWifi();
    if constexpr (Role != 2) {
      server_.handleClient();
      socket_.loop();
      if (owner_ != NoOwner && millis() - ownerAt_ >= 500) owner_ = NoOwner;
    }
    const uint32_t now = millis();
    if constexpr (Role != 0) {
      if (networkReady()) receiveRadio(now);
    }
    const bool online = networkReady() && remoteBoot_ && now - radioAt_ < LinkTimeout;
    if constexpr (Role == 2) {
      if (!online) remoteArmed_ = false;
      local_.tick(now, online && remoteArmed_ && (!SharedCable || pairPermit_));
      if (now - sentAt_ >= 80) sendStatus(now);
    } else {
      if constexpr (Role == 1) {
        if (!online && wasOnline_) {
          log("RADIO", "BK2 disconnected");
          if (linked_ || SharedCable) stopBoth(now, "paired carriage lost");
          linked_ = false;
          pending_ = Command{};
          sequence_++;
        }
        if (pending_.op != Op::Idle && pending_.op != Op::Abort && now - pendingAt_ >= 500) {
          notifyClient("Немає підтвердження від другої каретки");
          if (pending_.op != Op::LightTest) {
            if (linked_ || SharedCable) stopBoth(now, "command acknowledgment timeout");
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
      const bool permit = !SharedCable || Role == 0 || (online && remoteHandshake_ && remoteState_.powered);
      local_.tick(now, permit);
      if constexpr (UseIna226) recordPower(0, local_.status(now), now);
      if constexpr (Role == 1 && SharedCable) {
        if (surveying_ && pending_.op != Op::Survey) {
          const auto s = local_.status(now);
          if (s.surveyed && remoteState_.surveyed) {
            surveying_ = false;
            notifyClient("Обидві каретки завершили калібрування.");
          } else if ((s.mode != Mode::Survey && !s.surveyed) ||
                     (remoteState_.mode != Mode::Survey && !remoteState_.surveyed)) {
            stopBoth(now, "shared cable survey interrupted");
            notifyClient("Калібрування перервано. Поверніть обидві каретки до крайніх міток.");
          }
        }
      }
      if constexpr (Role == 1) {
        const auto localState = local_.status(now);
        if (linked_ && (!localState.powered || !remoteState_.powered)) {
          stopBoth(now, "linked interlock");
          linked_ = false;
        }
      }
      if constexpr (Role == 1 && SharedCable) serviceParking(now, online && remoteHandshake_);
      if (now - broadcastAt_ >= 100) broadcast(now);
    }
    wasOnline_ = online;
  }
 private:
  bool networkReady() const { return !HomeWifi || WiFi.status() == WL_CONNECTED; }
  void startMdns() {
    if (MDNS.begin("motor-control")) MDNS.addService("http", "tcp", 80);
  }
  void serviceHomeWifi() {
    const IPAddress current = networkReady() ? WiFi.localIP() : IPAddress();
    if (current == homeIp_) return;
    if constexpr (Role != 2) {
      if (homeIp_ != IPAddress()) MDNS.end();
      if (current != IPAddress()) startMdns();
    }
    homeIp_ = current;
    if (current == IPAddress()) log("WIFI", "home Wi-Fi disconnected");
    else log("WIFI", "home Wi-Fi channel=%ld IP=%s", WiFi.channel(), current.toString().c_str());
  }
  static constexpr uint8_t NoOwner = 255;
  static constexpr const uint8_t *peer() {
#ifdef BK_MIXED_GAN
    return Role == 1 ? pair::GanMac : pair::BkMac;
#else
    return Role == 1 ? RemoteMac : MainMac;
#endif
  }
  static inline QueueHandle_t frames_ = nullptr;
  static void onRadio(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
    if (!frames_ || !info || !data || length != sizeof(Packet) ||
        memcmp(info->src_addr, peer(), 6) != 0) return;
    Frame frame{};
    memcpy(&frame.packet, data, sizeof(Packet));
    if (frame.packet.magic != RadioMagic || !frame.packet.sender || frame.packet.profile != PairProfile) return;
    frame.receivedAt = millis();
    frame.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -127;
    xQueueSend(frames_, &frame, 0);
  }
  void startRadio() {
    if constexpr (Role == 2 && !HomeWifi) esp_wifi_set_channel(Channel, WIFI_SECOND_CHAN_NONE);
    const uint8_t empty[6]{};
    if (memcmp(peer(), empty, 6) == 0) {
      log("RADIO", "set station MAC addresses in Settings.h");
      return;
    }
    frames_ = xQueueCreate(10, sizeof(Frame));
    if (!frames_ || esp_now_init() != ESP_OK) return;
    esp_now_peer_info_t p{};
    memcpy(p.peer_addr, peer(), 6);
    p.channel = HomeWifi ? 0 : Channel;
    p.ifidx = WIFI_IF_STA;
    radioReady_ = esp_now_add_peer(&p) == ESP_OK &&
      esp_now_register_recv_cb(onRadio) == ESP_OK;
    log("RADIO", "ready=%d", radioReady_);
  }
  void send(Packet &packet, uint32_t now) {
    sentAt_ = now;
    if (!radioReady_ || !networkReady()) return;
    packet.sender = boot_;
    packet.receiver = remoteBoot_;
    esp_now_send(peer(), reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  }
  void sendCommand(uint32_t now) {
    if (!remoteBoot_) return;
    Packet p;
    p.sequence = sequence_;
    p.command = pending_;
    p.logSequence = remoteLogSequence_;
    p.pairPermit = local_.status(now).powered && remoteHandshake_ && now - radioAt_ < LinkTimeout;
    send(p, now);
  }
  void sendStatus(uint32_t now) {
    Packet p;
    p.statusPacket = 1;
    p.sequence = appliedSeq_;
    p.accepted = commandAccepted_;
    p.status = local_.status(now);
    const char *line = nullptr;
    p.logSequence = eventLog.next(acknowledgedLogSequence_, line);
    if (p.logSequence) snprintf(p.logLine, sizeof(p.logLine), "%s", line);
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
          acknowledgedLogSequence_ = 0;
          appliedSeq_ = p.sequence;
          remoteArmed_ = true;
          commandAccepted_ = true;
        }
        if (int32_t(p.sequence - appliedSeq_) < 0) continue;
        radioAt_ = receivedAt;
        pairPermit_ = p.pairPermit;
        if (p.logSequence <= eventLog.latest()) acknowledgedLogSequence_ = p.logSequence;
        if (p.sequence == appliedSeq_) continue;
        appliedSeq_ = p.sequence;
        commandAccepted_ = p.command.op == Op::Idle || local_.execute(p.command, now);
        if (!commandAccepted_) log("RADIO", "rejected operation=%u", unsigned(p.command.op));
      } else {
        if (!p.statusPacket || (p.receiver != 0 && p.receiver != boot_)) continue;
        remoteHandshake_ = p.receiver == boot_;
        if (remoteBoot_ != p.sender) {
          if (linked_ || SharedCable) local_.stop(now, "remote restarted");
          surveying_ = false;
          parking_ = false;
          armParking(false);
          linked_ = false;
          remoteBoot_ = p.sender;
          remoteLogSequence_ = 0;
          powerSeq_[1] = 0;
          log("RADIO", "BK2 session online");
          pending_ = Command{};
          sequence_++;
        }
        if (p.logSequence > remoteLogSequence_) {
          remoteLogSequence_ = p.logSequence;
          char line[EventLineSize];
          memcpy(line, p.logLine, sizeof(line));
          line[sizeof(line) - 1] = '\0';
          eventLog.append(line);
        }
        remoteState_ = p.status;
        recordPower(1, remoteState_, receivedAt);
        remoteRssi_ = f.rssi;
        radioAt_ = receivedAt;
        if (p.receiver == boot_ && p.sequence == sequence_ && pending_.op != Op::Idle) {
          if (!p.accepted) {
            notifyClient("Р”СЂСѓРіР° РєР°СЂРµС‚РєР° РІС–РґС…РёР»РёР»Р° РєРѕРјР°РЅРґСѓ");
            if ((linked_ || SharedCable) && pending_.op != Op::LightTest) {
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
  struct PowerPoint {
    uint32_t at;
    float amps;
    InaPeaks peaks;
    uint16_t mv;
    uint8_t carriage;
    bool averageValid;
  };
  static constexpr uint32_t PowerCapacity = 160;

  void recordPower(uint8_t index, const Status &s, uint32_t now) {
    if (!s.hasIna226 || s.inaPeaks.sequence == powerSeq_[index]) return;
    powerSeq_[index] = s.inaPeaks.sequence;
    powerHistory_[powerEnd_ % PowerCapacity] = {now, s.amps, s.inaPeaks, s.mv, index, s.currentValid};
    ++powerEnd_;
  }

  void serializePower(JsonDocument &result, bool sameBoot) {
    const uint8_t sources = (UseIna226 ? 1 : 0) | (Role == 1 && remoteState_.hasIna226 ? 2 : 0);
    result["inaSources"] = sources;
    if (!sources) return;
    auto rows = result["power"].to<JsonArray>();
    uint32_t cursor = sameBoot ? strtoul(server_.arg("powerAfter").c_str(), nullptr, 10) : 0;
    const uint32_t oldest = powerEnd_ > PowerCapacity ? powerEnd_ - PowerCapacity : 0;
    cursor = cursor <= powerEnd_ ? max(cursor, oldest) : oldest;
    for (uint8_t i = 0; i < 32 && cursor < powerEnd_; ++i, ++cursor) {
      const auto &point = powerHistory_[cursor % PowerCapacity];
      auto row = rows.add<JsonArray>();
      row.add(point.at);
      row.add(point.carriage);
      if (point.averageValid) row.add(point.amps); else row.add(nullptr);
      if (point.peaks.flags & InaInterval) row.add(point.peaks.interval); else row.add(nullptr);
      if (point.mv) row.add(point.mv / 1000.0f); else row.add(nullptr);
      if (point.peaks.flags & InaStartup) row.add(point.peaks.startup); else row.add(nullptr);
      row.add(point.peaks.flags);
      if (point.mv && (point.peaks.flags & InaBatteryFeed)) row.add(batteryChargePercent(point.mv));
      else row.add(nullptr);
    }
    result["powerCursor"] = cursor;
  }

  void serveEventLog() {
    const auto requestedBoot = strtoul(server_.arg("boot").c_str(), nullptr, 10);
    uint32_t cursor = requestedBoot == boot_ ? strtoul(server_.arg("after").c_str(), nullptr, 10) : 0;
    if (cursor > eventLog.latest()) cursor = 0;
    JsonDocument result;
    result["boot"] = boot_;
    auto lines = result["lines"].to<JsonArray>();
    for (uint8_t i = 0; i < 24; ++i) {
      const char *line = nullptr;
      const uint32_t next = eventLog.next(cursor, line);
      if (!next) break;
      lines.add(line);
      cursor = next;
    }
    result["cursor"] = cursor;
    serializePower(result, requestedBoot == boot_);
    String body;
    serializeJson(result, body);
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json; charset=utf-8", body);
  }
  void notifyClient(const char *msg) {
    log("NOTICE", "%s", msg);
    if (owner_ == NoOwner) return;
    JsonDocument d;
    d["type"] = "notice";
    d["message"] = msg;
    String payload;
    serializeJson(d, payload);
    socket_.sendTXT(owner_, payload);
  }
  void armParking(bool armed) {
    parkArmed_ = armed;
    parkCounting_ = false;
  }
  void serviceParking(uint32_t now, bool online) {
    if (!parkArmed_ && !parking_) return;
    const auto own = local_.status(now);
    if (!online || !own.ready || !remoteState_.ready || !own.surveyed || !remoteState_.surveyed) {
      if (parking_) stopBoth(now, "parking interlock");
      armParking(false);
      return;
    }
    if (parking_) {
      if (pending_.op == Op::Park) return;
      if ((own.mode != Mode::Park && !own.parked) || (remoteState_.mode != Mode::Park && !remoteState_.parked)) {
        stopBoth(now, "parking incomplete");
        notifyClient("Каретки не підтвердили зупинку на мітках. Перевірте позицію.");
      } else if (own.parked && remoteState_.parked) {
        parking_ = false;
        log("PARK", "both carriages at their outer marks");
      }
      return;
    }
    if (pending_.op != Op::Idle || own.mode != Mode::Idle || remoteState_.mode != Mode::Idle) {
      parkCounting_ = false;
      return;
    }
    if (own.parked && remoteState_.parked) { armParking(false); return; }
    if (!parkCounting_) {
      parkCounting_ = true;
      parkIdleAt_ = now;
    }
    if (now - parkIdleAt_ < ParkIdleDelayMs) return;
    armParking(false);
    Command command;
    command.op = Op::Park;
    if (!local_.execute(command, now)) {
      notifyClient("Повернення до міток заблоковано.");
      return;
    }
    parking_ = true;
    pending_ = command;
    pendingAt_ = now;
    sequence_++;
    sendCommand(now);
    log("PARK", "returning both carriages to outer marks");
  }
  void stopBoth(uint32_t now, const char *reason, Op op = Op::Abort) {
    surveying_ = false;
    parking_ = false;
    armParking(false);
    if (op == Op::Stop) {
      Command stop;
      stop.op = op;
      local_.execute(stop, now);
    } else {
      local_.stop(now, reason);
    }
    if constexpr (Role == 1) {
      pending_ = Command{};
      pending_.op = op;
      pendingAt_ = now;
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
    const bool online = networkReady() && remoteHandshake_ && remoteBoot_ && now - radioAt_ < LinkTimeout;
    const bool requestingLink = !strcmp(type, "link");
    if (requestingLink) {
      if constexpr (Role == 1) linked_ = online && (d["enabled"] | false);
      return;
    }
    Command c;
    const auto current = selected == 1 ? local_.status(now) : remoteState_;
    const int speed = d["speed"] | int(current.speed);
    if (speed == SpeedByPercent || speedPreset(speed)) c.speed = speed;
    const char *direction = d["direction"] | "";
    c.direction = !strcmp(direction, "right") ? 1 : !strcmp(direction, "left") ? -1 : 0;
    if (!strcmp(type, "stop")) c.op = Op::Stop;
    else if (!strcmp(type, "speed")) {
      if (speed != SpeedByPercent && !speedPreset(speed)) return;
      c.op = Op::Speed;
    }
    else if (!strcmp(type, "pwm_limit")) {
      const int percent = d["percent"] | 0;
      if (percent < 1 || percent > 100 || (MixedGan && selected == 2)) return;
      c.op = Op::PwmLimit;
      c.value = percent;
    }
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
    else if (!strcmp(type, "clear_reference") && SharedCable && Role == 1) c.op = Op::ClearReference;
    else if (!strcmp(type, "survey") && SharedCable && Role == 1) c.op = Op::Survey;
    else if (!strcmp(type, "battery_reset")) c.op = Op::Reset;
    else if (!strcmp(type, "timed_test")) {
      c.op = Op::Timed;
      c.duration = d["durationMs"] | 0UL;
      if (c.duration != 1000 && c.duration != 2000 && c.duration != 5000 && c.duration != 10000) return;
    } else return;

    if (c.op == Op::Stop && SharedCable) {
      const bool schedulePark = !parking_ && !parkCounting_;
      stopBoth(now, "shared stop", Op::Stop);
      armParking(schedulePark);
      return;
    }
    if (c.op == Op::Stop && owner_ != client) { stopBoth(now, "stop from another client"); return; }
    if (c.op == Op::Timed && linked_) { stopBoth(now, "calibration"); linked_ = false; }
    if constexpr (Role == 1 && SharedCable) {
      const auto own = local_.status(now);
      const bool run = c.op == Op::Start || c.op == Op::Program || c.op == Op::Target ||
                       (c.op == Op::Jog && current.surveyed);
      if ((run && (!online || !own.surveyed || !remoteState_.surveyed)) ||
          (c.op == Op::Survey && (!online || !own.atOuterMark || !remoteState_.atOuterMark ||
                                !own.ready || !remoteState_.ready || pending_.op != Op::Idle)) ||
          (c.op == Op::ClearReference && (!online || own.mode != Mode::Idle || remoteState_.mode != Mode::Idle)) ||
          (surveying_ && c.op != Op::MotorStop)) {
        notifyClient("Потрібні зв’язок з обома каретками та правильний етап калібрування.");
        return;
      }
      if (c.op == Op::MotorStop && surveying_) { stopBoth(now, "shared survey stopped"); return; }
      if (c.op == Op::Timed) return;
      if (c.op == Op::Target) {
        const float side = selected == 1 ? -c.metres : c.metres;
        if (!isfinite(side) || side < SharedInnerM || side > SharedOuterM) {
          notifyClient("Ціль за межами зони цієї каретки.");
          return;
        }
      }
    }
    const bool synchronized = c.op == Op::Survey || (linked_ && c.op != Op::LightTest && !(MixedGan && c.op == Op::PwmLimit) &&
        (!SharedCable || (c.op != Op::Jog && c.op != Op::Zero && c.op != Op::MotorStop && c.op != Op::ClearReference)));
    const bool local = selected == 1 || synchronized;
    const bool remote = selected == 2 || synchronized;
    const bool speedSetting = c.op == Op::Speed || c.op == Op::PwmLimit;
    if (remote && (pending_.op == Op::Speed || pending_.op == Op::PwmLimit) &&
        c.op != Op::Stop && c.op != Op::MotorStop && !speedSetting) {
      notifyClient("Дочекайтеся підтвердження налаштування швидкості від другої каретки.");
      return;
    }
    if (remote && !online && c.op != Op::Stop) {
      notifyClient("Р”СЂСѓРіР° РєР°СЂРµС‚РєР° РѕС„Р»Р°Р№РЅ");
      return;
    }
    const bool stopsMotors = c.op == Op::Stop || c.op == Op::MotorStop;
    if (synchronized && !stopsMotors && !speedSetting && c.op != Op::Reset && c.op != Op::Zero && c.op != Op::Servo &&
        (!local_.status(now).ready || !remoteState_.ready)) {
      notifyClient("Р СѓС… Р·Р°Р±Р»РѕРєРѕРІР°РЅРѕ РЅР° РѕРґРЅС–Р№ С–Р· РєР°СЂРµС‚РѕРє");
      return;
    }
    auto localCommand = c;
    if (SharedCable && synchronized && c.op == Op::Target) localCommand.metres = -fabsf(c.metres);
    const bool programCommand = c.op == Op::Start || c.op == Op::Program;
    const bool manualCommand = c.op == Op::Target || c.op == Op::Jog || c.op == Op::MotorStop ||
        c.op == Op::Servo || c.op == Op::Speed || c.op == Op::PwmLimit || c.op == Op::LightTest;
    if (SharedCable && parking_ && (programCommand || manualCommand))
      stopBoth(now, "parking interrupted by operator");
    if (local && !local_.execute(localCommand, now)) {
      notifyClient("РљРѕРјР°РЅРґСѓ РІС–РґС…РёР»РµРЅРѕ. РџРµСЂРµРІС–СЂС‚Рµ Р¶РёРІР»РµРЅРЅСЏ, РєР°Р»С–Р±СЂСѓРІР°РЅРЅСЏ С‚Р° СЃС‚Р°РЅ РїСЂРёРІРѕРґС–РІ.");
      if (synchronized) stopBoth(now, "local rejected command");
      return;
    }
    if (SharedCable && (programCommand || manualCommand))
      armParking(true);
    if (remote) {
      if (c.op == Op::Survey) surveying_ = true;
      if (SharedCable && synchronized && c.op == Op::Target) c.metres = fabsf(c.metres);
      pending_ = c;
      pendingAt_ = now;
      sequence_++;
      sendCommand(now);
    }
  }
  void serializeStatus(JsonObject obj, const Status &s, bool online, int8_t rssi, bool carriage2 = false) {
    obj["online"] = online;
    obj["rssi"] = rssi;
    obj["position"] = s.position;
    obj["target"] = s.target;
    const bool estimated = MixedGan && carriage2;
#ifdef BK_MIXED_GAN
    if (estimated) obj["estimatedTravelLimit"] = s.estimatedTravelLimit;
#endif
    obj["estimatedPosition"] = estimated;
    if (online && s.centered && s.calibrated) {
      obj["fromReference"] = s.fromReference;
      if (!estimated) {
        obj["leftWheelM"] = s.wheelPosition[0];
        obj["rightWheelM"] = s.wheelPosition[1];
      }
    }
    obj["outputPwm"] = s.pwm[0] ? s.pwm[0] : s.pwm[1];
    obj["speed"] = s.speed;
    if (!estimated) obj["maxPwm"] = s.maxPwm;
    obj["program"] = s.program;
    obj["servoAngle"] = s.servoSelected;
    obj["servoPosition"] = s.servoTarget;
    obj["servoEnabled"] = s.servo;
    obj["servoTravel90Ms"] = ServoTravel90Ms;
    obj["lightTest"] = uint8_t(s.lightTest);
    obj["mode"] = s.mode == Mode::Idle ? "СЃС‚РѕРї" : s.mode == Mode::Timed ? "С‚РµСЃС‚ С‡Р°СЃСѓ" : "СЂСѓС…";
    obj["testRemainingMs"] = s.remainingMs;
    obj["surveyed"] = s.surveyed;
    obj["atOuterMark"] = s.atOuterMark;
    obj["parked"] = s.parked;
    obj["moving"] = s.mode != Mode::Idle;
    obj["centered"] = s.centered;
    obj["calibrated"] = s.calibrated;
    obj["ready"] = s.ready;
    obj["powered"] = s.powered;
    obj["hasIna226"] = s.hasIna226;
    obj["locked"] = s.locked;
    if (s.fault != MotorFault::None) {
      obj["fault"] = motorFaultMessage(s.fault);
      obj["faultMotor"] = s.faultMotor;
    }
    if (!online) return;
    if (s.maxMps > 0) obj["maxSpeed"] = s.maxMps;
    if (estimated) return;
    obj["enc1"] = s.count[0];
    obj["enc2"] = s.count[1];
    obj["countsPerSec1"] = s.countsPerSecond[0];
    obj["countsPerSec2"] = s.countsPerSecond[1];
    if (s.calibrated) {
      obj["wheelSpeed1"] = s.wheelMps[0];
      obj["wheelSpeed2"] = s.wheelMps[1];
      const auto &motors = motorSettingsFor(carriage2);
      if (motors[0].wheelCircumferenceMm > 0) obj["wheelRpm1"] = s.wheelMps[0] * 60000.0f / motors[0].wheelCircumferenceMm;
      if (motors[1].wheelCircumferenceMm > 0) obj["wheelRpm2"] = s.wheelMps[1] * 60000.0f / motors[1].wheelCircumferenceMm;
    }
    if (s.currentValid) {
      obj["amps"] = s.amps;
      obj["watts"] = s.amps * s.mv / 1000.0f;
    }
    if (s.mv) {
      obj["volts"] = s.mv / 1000.0f;
      if (s.inaPeaks.flags & InaBatteryFeed) obj["charge"] = batteryChargePercent(s.mv);
    }
  }
  void broadcast(uint32_t now) {
    broadcastAt_ = now;
    if (!socket_.connectedClients()) return;
    JsonDocument d;
    d["type"] = "state";
    d["linked"] = linked_;
    d["surveying"] = surveying_;
    d["parking"] = parking_;
    if (parkCounting_) {
      const uint32_t elapsed = now - parkIdleAt_;
      if (elapsed < ParkIdleDelayMs) d["parkWaitMs"] = ParkIdleDelayMs - elapsed;
    }
    if (SharedCable) {
      d["outerM"] = SharedOuterM;
      d["innerM"] = SharedInnerM;
      d["railM"] = SharedCableM / 2;
      d["wheelbaseM"] = 2 * HalfWheelbaseM;
    }
    const bool online = networkReady() && remoteBoot_ && now - radioAt_ < LinkTimeout;
    d["nodeOnline"] = online && remoteHandshake_;
    auto a = d["carriages"].to<JsonArray>();
    serializeStatus(a.add<JsonObject>(), local_.status(now), true, -55);
    if constexpr (Role == 1) serializeStatus(a.add<JsonObject>(), remoteState_, online && remoteHandshake_, remoteRssi_, true);
    String msg;
    serializeJson(d, msg);
    socket_.broadcastTXT(msg);
  }
#if defined(BK_MIXED_GAN) && BK_ROLE == 2
  GanCarriage local_;
#else
  Carriage<UseIna226> local_;
#endif
  WebServer server_{80};
  WebSocketsServer socket_{81};
  Command pending_{};
  Status remoteState_{};
  uint32_t boot_ = 0, remoteBoot_ = 0, radioAt_ = 0, appliedSeq_ = 0, sequence_ = 1;
  uint32_t pendingAt_ = 0, sentAt_ = 0, broadcastAt_ = 0, ownerAt_ = 0;
  uint8_t owner_ = NoOwner;
  int8_t remoteRssi_ = -127;
  IPAddress homeIp_;
  bool surveying_ = false, pairPermit_ = false, parking_ = false, parkCounting_ = false;
  bool parkArmed_ = false;
  uint32_t parkIdleAt_ = 0;
  uint32_t remoteLogSequence_ = 0, acknowledgedLogSequence_ = 0;
  PowerPoint powerHistory_[Role == 2 ? 1 : PowerCapacity]{};
  uint32_t powerEnd_ = 0;
  uint16_t powerSeq_[2]{};
  bool remoteHandshake_ = false;
  bool linked_ = false, wasOnline_ = false, radioReady_ = false, remoteArmed_ = false, commandAccepted_ = false;
};

template<int Role, bool UseIna226> inline Firmware<Role, UseIna226> instance;
template<int Role, bool UseIna226 = true> void begin() { instance<Role, UseIna226>.begin(); }
template<int Role, bool UseIna226 = true> void tick() { instance<Role, UseIna226>.tick(); }
}
