#include <MotorCalibration.h>

bk370::MotorCalibration calibration;

void setup() { calibration.begin(); }
void loop() { calibration.tick(); }
