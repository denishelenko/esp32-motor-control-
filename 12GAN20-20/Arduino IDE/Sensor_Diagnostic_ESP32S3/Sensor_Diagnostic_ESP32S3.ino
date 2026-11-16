#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include <math.h>

// Diagnostic only: does not control either motor or servo.
constexpr int SDA_PIN = 8, SCL_PIN = 9;
constexpr int I2S_BCLK = 10, I2S_WS = 11, I2S_DATA = 12;
constexpr int LIGHT_PIN = 16;
constexpr uint8_t INA_ADDRESS = 0x40;
I2SClass microphone;
bool micReady = false;
bool blink = false;
uint32_t lastSample = 0, lastBlink = 0;

bool readRegister(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(INA_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(INA_ADDRESS, uint8_t(2)) != 2) return false;
  value = (uint16_t(Wire.read()) << 8) | Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(30);
  microphone.setPins(I2S_BCLK, I2S_WS, -1, I2S_DATA);
  micReady = microphone.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT,
                              I2S_SLOT_MODE_STEREO);
  Serial.println("Sensor diagnostic: send 1 to blink LED, 0 to stop.");
  Serial.printf("INMP441 I2S initialization: %s\n", micReady ? "OK" : "FAILED");
}

void loop() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == '1') blink = true;
    if (c == '0') { blink = false; digitalWrite(LIGHT_PIN, LOW); }
  }
  const uint32_t now = millis();
  if (blink && now - lastBlink >= 500) {
    lastBlink = now;
    digitalWrite(LIGHT_PIN, !digitalRead(LIGHT_PIN));
  }
  if (now - lastSample >= 1000) {
    lastSample = now;
    uint16_t bus, shunt;
    if (readRegister(0x02, bus) && readRegister(0x01, shunt)) {
      Serial.printf("INA bus: %.3f V; shunt: %.3f mV (current requires shunt calibration)\n",
                    bus * 0.00125f, int16_t(shunt) * 0.0025f);
    } else Serial.println("INA226 not found at 0x40; check wiring/address");
    if (micReady) {
      // L/R tied to GND: use the left slot of interleaved stereo frames.
      int32_t samples[256];
      const size_t bytes = microphone.readBytes(reinterpret_cast<char *>(samples), sizeof(samples));
      const size_t frames = bytes / (2 * sizeof(int32_t));
      double sum = 0;
      for (size_t i = 0; i < frames; ++i) {
        const double normalized = double(samples[2 * i]) / 2147483648.0;
        sum += normalized * normalized;
      }
      if (frames) Serial.printf("Mic RMS (relative, uncalibrated): %.6f, frames=%u\n",
                                sqrt(sum / frames), unsigned(frames));
      else Serial.println("Mic: no audio samples");
    }
  }
  yield();
}
