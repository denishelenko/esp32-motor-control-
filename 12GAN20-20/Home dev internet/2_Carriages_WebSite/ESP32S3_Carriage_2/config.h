#pragma once
#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error Select ESP32S3 Dev Module in Arduino IDE.
#endif

// Home profile: connect to the same external 2.4 GHz access point as carriage 1.
constexpr uint8_t WIFI_CHANNEL = 6;
constexpr uint32_t ESPNOW_GROUP_ID = 0x43525247UL;
constexpr char MDNS_HOSTNAME[] = "motor-carriage-2";
constexpr char OTA_HOSTNAME[] = "motor-carriage-2";
constexpr char OTA_PASSWORD[] = "motor8266";
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t WIFI_ATTEMPT_TIMEOUT_MS = 15000;

// One motor per bridge. Both bridges receive the same direction and PWM.
constexpr uint8_t MOTOR_IN1_PIN = 1;      // GPIO1 -> TB6612FNG AIN1 and BIN1
constexpr uint8_t MOTOR_IN2_PIN = 2;      // GPIO2 -> TB6612FNG AIN2 and BIN2
constexpr bool MOTOR_DIRECTION_REVERSED = false;
constexpr uint8_t MOTOR_PWM_PIN = 42;     // GPIO42 -> PWMA and PWMB
constexpr uint8_t SERVO_SIGNAL_PIN = 21;  // GPIO21 -> TXS0108E A1; B1 -> servo signal.
constexpr uint8_t MOSFET_PIN = 47;         // Servo-controlled strobe on the MOSFET module
constexpr uint8_t LIGHT_RED_PIN = 41;      // Red RGB channel through resistor
constexpr uint8_t LIGHT_GREEN_PIN = 40;   // Green RGB channel through resistor
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
constexpr uint8_t SERVO_CLOSED_ANGLE = 0;
constexpr uint8_t SERVO_PHYSICAL_MAX_ANGLE = 90;
constexpr uint8_t SERVO_COMMAND_PER_PHYSICAL_DEGREE = 2;
constexpr uint16_t SERVO_MIN_PULSE_US = 520;
constexpr uint16_t SERVO_MAX_PULSE_US = 2520;
constexpr uint16_t SERVO_FREQUENCY_HZ = 333;
constexpr uint16_t PWM_MAX = 1023;
constexpr uint16_t PWM_FREQUENCY_HZ = 1000;
constexpr uint32_t LIGHT_BLINK_HALF_PERIOD_MS = 125;

constexpr uint32_t STATUS_INTERVAL_MS = 300;
constexpr uint32_t COMMAND_TIMEOUT_MS = 700;
constexpr uint32_t RAMP_INTERVAL_MS = 10;
constexpr int16_t RAMP_STEP = 30;
