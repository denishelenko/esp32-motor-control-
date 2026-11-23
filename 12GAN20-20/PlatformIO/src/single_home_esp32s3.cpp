#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// Set the name and password of your 2.4 GHz network before uploading.
constexpr char HOME_WIFI_SSID[] = "YOUR_WIFI_NAME";
constexpr char HOME_WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

#define SINGLE_CARRIAGE_HOME_WIFI
#include "../../Arduino IDE/1_Carriage_WebSite/ESP32S3/ESP32S3.ino"
