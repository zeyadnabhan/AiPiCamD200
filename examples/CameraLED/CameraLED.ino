/*
 * CameraLED - live camera in the browser + a button for the on-board LED
 * Board: AiPi-Cam-D200 (Ai-M61-32S / BL618)  |  Tools > Board > bouffalolab > BL618G0
 *
 * 1) Upload: hold S2, press RST, release S2, then click Upload.
 * 2) Press RST to run.
 * 3) Connect your phone/PC to the Wi-Fi "AiPi-Cam" and open http://192.168.169.1
 */
#include <AiPiCamD200.h>
#include "page.h"

const char *HOTSPOT_NAME = "AiPi-Cam";
const char *HOTSPOT_PASS = "";   // empty = open, or 8+ characters (WPA2)
const int LED_GPIO = 16;         // on-board LED of the D200

// GET /led?on=1 | /led?on=0 | /led  ->  "1" (on) or "0" (off)
String onLed(const char *query) {
  int on = D200WebCam::param(query, "on", -1);
  if (on == 1) Light.on();
  else if (on == 0) Light.off();
  return Light.isOn() ? "1" : "0";
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Light.begin(LED_GPIO);

  if (!Camera.begin(50)) {          // JPEG quality 1..100
    Serial.println("Camera not found!");
    while (true) delay(1000);
  }
  if (!Hotspot.begin(HOTSPOT_NAME, HOTSPOT_PASS)) {
    Serial.println("Hotspot failed to start");
    while (true) delay(1000);
  }
  WebCam.setPage(PAGE);             // our page instead of the built-in one
  WebCam.on("/led", onLed);
  WebCam.begin(80);

  Serial.print("Connect to Wi-Fi '");
  Serial.print(HOTSPOT_NAME);
  Serial.print("' and open http://");
  Serial.println(Hotspot.ipString());
}

void loop() {
  delay(1000);
}
