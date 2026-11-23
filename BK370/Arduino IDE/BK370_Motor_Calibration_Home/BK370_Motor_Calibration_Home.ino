#include <MotorCalibration.h>

constexpr char HOME_WIFI_SSID[] = "";
constexpr char HOME_WIFI_PASSWORD[] = "";

bk370::MotorCalibration calibration;

void setup() { calibration.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD); }
void loop() { calibration.tick(); }
