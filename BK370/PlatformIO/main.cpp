#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#if !defined(BK_MOTOR_CALIBRATION) && !defined(BK_MOTOR_CALIBRATION_HOME)
#include <Wire.h>
#endif

#if defined(BK_MOTOR_CALIBRATION_HOME)
#include "../Arduino IDE/BK370_Motor_Calibration_Home/BK370_Motor_Calibration_Home.ino"
#elif defined(BK_MOTOR_CALIBRATION)
#include "../Arduino IDE/BK370_Motor_Calibration/BK370_Motor_Calibration.ino"
#elif defined(BK_NO_INA226) && BK_ROLE == 1
#include "../Arduino IDE/BK370_Main_No_INA226/BK370_Main_No_INA226.ino"
#elif defined(BK_NO_INA226) && BK_ROLE == 2
#include "../Arduino IDE/BK370_Remote_No_INA226/BK370_Remote_No_INA226.ino"
#elif BK_ROLE == 0
#include "../Arduino IDE/BK370_Single/BK370_Single.ino"
#elif BK_ROLE == 1
#include "../Arduino IDE/BK370_Main/BK370_Main.ino"
#elif BK_ROLE == 2
#include "../Arduino IDE/BK370_Remote/BK370_Remote.ino"
#else
#error Unknown BK370 role
#endif
