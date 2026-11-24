#include <MotorCalibration.h>

constexpr bool CALIBRATE_CARRIAGE_2 = false;
bk370::MotorCalibration calibration(CALIBRATE_CARRIAGE_2);

void setup() { calibration.begin(); }
void loop() { calibration.tick(); }
