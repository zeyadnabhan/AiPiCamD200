/*
 * WebCamSD - live video + "Save to SD" and "Light" buttons on the page
 * Board: AiPi-Cam-D200  |  Tools > Board: BL618G0
 * Hotspot mode: connect to HOTSPOT_NAME and open http://192.168.169.1/
 */
#include <AiPiCamD200.h>

const char *HOTSPOT_NAME = "AiPi-Cam";
const char *HOTSPOT_PASS = "";      // empty = open, or 8+ characters
const int   LIGHT_GPIO   = 16;      // on-board light (D200: GPIO16)

void setup() {
  Serial.begin(115200);
  delay(1500);

  if (!Camera.begin(50)) {
    Serial.println("Camera not found!");
    while (true) delay(1000);
  }
  if (SD.begin()) {
    Serial.print("SD OK, free MB: ");
    Serial.println(SD.freeMB());
  } else {
    Serial.println("No SD card (Save to SD will report it)");
  }
  if (LIGHT_GPIO >= 0) Light.begin(LIGHT_GPIO);

  Hotspot.begin(HOTSPOT_NAME, HOTSPOT_PASS);
  WebCam.setLog(true);
  WebCam.begin(80);
  Serial.print("Open http://");
  Serial.println(Hotspot.ipString());
}

void loop() {
  delay(5000);
  Serial.print("page: http://");
  Serial.println(Hotspot.ipString());
}
