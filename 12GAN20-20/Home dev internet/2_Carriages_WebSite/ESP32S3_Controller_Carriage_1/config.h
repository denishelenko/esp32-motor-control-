#pragma once
#include <Arduino.h>
#include <IPAddress.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error Select ESP32S3 Dev Module in Arduino IDE.
#endif

// Home profile: the ESP32-S3 joins an existing 2.4 GHz access point.
// Both carriages must connect to the same 2.4 GHz access point.
#include "wifi_settings.h"  // Edit the Wi-Fi name and password here.
constexpr const char* WIFI_SSID = HOME_WIFI_SSID;
constexpr const char* WIFI_PASSWORD = HOME_WIFI_PASSWORD;
constexpr uint8_t WIFI_CHANNEL = 6;
constexpr uint32_t ESPNOW_GROUP_ID = 0x43525247UL;
constexpr char MDNS_HOSTNAME[] = "motor-control";
constexpr char OTA_HOSTNAME[] = "motor-control";
constexpr char OTA_PASSWORD[] = "motor8266";
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t WIFI_ATTEMPT_TIMEOUT_MS = 15000;
constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t WEBSOCKET_PORT = 81;

// One motor per bridge. Both bridges receive the same direction and PWM.
constexpr uint8_t MOTOR_IN1_PIN = 1;      // GPIO1 -> TB6612FNG AIN1 and BIN1
constexpr uint8_t MOTOR_IN2_PIN = 2;      // GPIO2 -> TB6612FNG AIN2 and BIN2
constexpr bool MOTOR_DIRECTION_REVERSED = false;
constexpr uint8_t MOTOR_PWM_PIN = 42;     // GPIO42 -> PWMA and PWMB
constexpr uint8_t SERVO_SIGNAL_PIN = 21;  // GPIO21 -> TXS0108E A1; B1 -> servo signal.
constexpr uint8_t MOSFET_PIN = 47;         // Servo-controlled strobe on the MOSFET module
constexpr uint8_t LIGHT_RED_PIN = 41;      // Red RGB channel through resistor
constexpr uint8_t LIGHT_GREEN_PIN = 40;   // Green RGB channel through resistor
constexpr uint8_t SERVO_DEFAULT_ANGLE = 45;  // Physical degrees; command is doubled.
constexpr uint16_t SERVO_MIN_PULSE_US = 520;
constexpr uint16_t SERVO_MAX_PULSE_US = 2520;
constexpr uint16_t SERVO_FREQUENCY_HZ = 333;
constexpr uint32_t SERVO_OPEN_HOLD_MS = 2500;
constexpr uint32_t SERVO_CLOSED_HOLD_MS = 2500;
constexpr uint16_t SERVO_TRAVEL_90_MS[2] = {350, 350};  // Visual estimates for carriages 1/2.
static_assert(MOTOR_PWM_PIN != MOTOR_IN1_PIN && MOTOR_PWM_PIN != MOTOR_IN2_PIN &&
              SERVO_SIGNAL_PIN != MOTOR_PWM_PIN && SERVO_SIGNAL_PIN != MOTOR_IN1_PIN &&
              SERVO_SIGNAL_PIN != MOTOR_IN2_PIN &&
              LIGHT_RED_PIN != MOTOR_IN1_PIN && LIGHT_RED_PIN != MOTOR_IN2_PIN &&
              LIGHT_RED_PIN != SERVO_SIGNAL_PIN && LIGHT_RED_PIN != MOTOR_PWM_PIN &&
              LIGHT_GREEN_PIN != MOTOR_IN1_PIN && LIGHT_GREEN_PIN != MOTOR_IN2_PIN &&
              LIGHT_GREEN_PIN != SERVO_SIGNAL_PIN && LIGHT_GREEN_PIN != MOTOR_PWM_PIN && LIGHT_GREEN_PIN != LIGHT_RED_PIN &&
              MOSFET_PIN != MOTOR_IN1_PIN && MOSFET_PIN != MOTOR_IN2_PIN &&
              MOSFET_PIN != MOTOR_PWM_PIN && MOSFET_PIN != SERVO_SIGNAL_PIN &&
              MOSFET_PIN != LIGHT_RED_PIN && MOSFET_PIN != LIGHT_GREEN_PIN,
              "ESP32-S3 output pins must be distinct");
constexpr uint16_t PWM_MAX = 1023;
constexpr uint16_t PWM_FREQUENCY_HZ = 1000;
constexpr uint32_t LIGHT_BLINK_HALF_PERIOD_MS = 125;

// A 20 m cable with 2 m reserve at each end leaves 16 m of working travel.
// The centre is 0 and the working range is -8..+8 m.
constexpr float TRACK_LENGTH_M = 20.0f;
constexpr float SAFE_MARGIN_M = 2.0f;
constexpr float TRACK_LIMIT_M = TRACK_LENGTH_M * 0.5f - SAFE_MARGIN_M;
constexpr float POSITION_TOLERANCE_M = 0.02f;
constexpr float UNCALIBRATED_SPEED_AT_100_PERCENT_MPS = 0.80f;  // Placeholder; not measured.
constexpr uint8_t TRAVEL_CALIBRATION_SPEED_PERCENT = 50;
struct TravelTimeCalibration {
  float right16mSeconds;
  float left16mSeconds;
};
// Fill in measured times at the speed above. Zero means not measured yet.
constexpr TravelTimeCalibration TRAVEL_TIMES[2] = {
  {0.0f, 0.0f},  // ESP32 carriage 1: right, left.
  {0.0f, 0.0f},  // ESP32-S3 carriage 2: right, left.
};
constexpr uint32_t COMMAND_INTERVAL_MS = 50;
constexpr uint32_t NODE_LINK_TIMEOUT_MS = 1200;
constexpr uint32_t NODE_ACK_TIMEOUT_MS = 500;
constexpr uint8_t NODE_MAX_ACK_LAG_PACKETS = 10;
constexpr uint32_t UI_BROADCAST_INTERVAL_MS = 70;
constexpr uint32_t JOG_TIMEOUT_MS = 650;
constexpr uint32_t LADDER_PAUSE_MS = 350;
constexpr uint32_t JERK_PAUSE_MS = 150;
constexpr uint32_t RANDOM_PAUSE_MS = 500;
constexpr uint32_t RAMP_INTERVAL_MS = 10;
constexpr int16_t RAMP_STEP = 30;
