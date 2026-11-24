#include <MotorCalibration.h>

constexpr char HOME_WIFI_SSID[] = "";
constexpr char HOME_WIFI_PASSWORD[] = "";

constexpr bool CALIBRATE_CARRIAGE_2 = false;
bk370::MotorCalibration calibration(CALIBRATE_CARRIAGE_2);

void setup() { calibration.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD); }
void loop() { calibration.tick(); }
