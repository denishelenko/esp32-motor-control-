#include <WebServer.h>
#include <WiFi.h>

#ifdef GAN_CALIBRATION
#include "../../12GAN20-20/Arduino IDE/Calibration_ESP32S3/Calibration_ESP32S3.ino"
#else
#include <ESPmDNS.h>
#include <Wire.h>
#include <BK370Carriage.h>
void setup() { bk370::begin<BK_ROLE, false>(); }
void loop() { bk370::tick<BK_ROLE, false>(); }
#endif
