/*
 * WebCamWiFi - live camera video in the browser over your home WiFi
 * Board: AiPi-Cam-D200  |  Tools > Board: BL618G0
 *
 * 1) Put your WiFi name/password below (2.4 GHz network only).
 * 2) Upload: hold S2, press RST, release both -> Upload. Press RST to run.
 * 3) Open the address printed in the Serial Monitor (115200)
 *    from any phone/PC on the same WiFi.
 */
#include <AiPiCamD200.h>

const char *WIFI_NAME = "YOUR_WIFI_NAME";
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== AiPi-Cam-D200 WebCam (WiFi) ===");

  if (!Camera.begin(50)) {
    Serial.print("Camera not found! sensor id = 0x");
    Serial.println(Camera.sensorId(), HEX);
    while (true) delay(1000);
  }
  Serial.println("Camera OK (800x600)");

  Serial.print("Connecting to ");
  Serial.println(WIFI_NAME);
  if (!Station.begin(WIFI_NAME, WIFI_PASS, 30000)) {
    Serial.println("WiFi failed: check name/password, and that it is 2.4 GHz");
    while (true) delay(1000);
  }

  WebCam.setLog(true);
  WebCam.begin(80);

  Serial.print("Open: http://");
  Serial.println(Station.ipString());
}

void loop() {
  static uint32_t last = 0;
  delay(5000);
  uint32_t now = Camera.frameCount();
  Serial.print("fps: ");
  Serial.print((now - last) / 5.0);
  Serial.print("   page: http://");
  Serial.println(Station.ipString());
  last = now;
}
