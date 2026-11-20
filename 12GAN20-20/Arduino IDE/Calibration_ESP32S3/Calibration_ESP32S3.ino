#include "calibration_controller.h"

void setup() { calibration::begin(); }
void loop() {
  calibration::server.handleClient();
  delay(1);
}
