#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#if BK_ROLE == 0
#include "../Arduino IDE/BK370_Single/BK370_Single.ino"
#elif BK_ROLE == 1
#include "../Arduino IDE/BK370_Main/BK370_Main.ino"
#elif BK_ROLE == 2
#include "../Arduino IDE/BK370_Remote/BK370_Remote.ino"
#else
#error Unknown BK370 role
#endif
