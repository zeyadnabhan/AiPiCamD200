/*
 * WebCamHotspot - live camera video in the browser
 * Board: AiPi-Cam-D200  |  Tools > Board: BL618G0
 *
 * 1) Upload: hold S2, press RST, release both -> Upload.
 * 2) Press RST to run.
 * 3) Connect your phone to the hotspot, open http://192.168.169.1/
 */
#include <AiPiCamD200.h>

const char *HOTSPOT_NAME = "AiPi-Cam";
const char *HOTSPOT_PASS = "";   // empty = open, or 8+ characters

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== AiPi-Cam-D200 WebCam ===");

  if (!Camera.begin(50)) {
    Serial.print("Camera not found! sensor id = 0x");
    Serial.println(Camera.sensorId(), HEX);
    while (true) delay(1000);
  }
  Serial.print("Camera: ");
  Serial.print(Camera.sensorName());
  Serial.print(" ");
  Serial.print(Camera.width());
  Serial.print("x");
  Serial.println(Camera.height());

  if (!Hotspot.begin(HOTSPOT_NAME, HOTSPOT_PASS)) {
    Serial.println("Hotspot failed to start");
    while (true) delay(1000);
  }

  WebCam.setTitle("AiPi-Cam-D200");
  WebCam.setLog(true);
  WebCam.begin(80);

  Serial.print("Connect to '");
  Serial.print(HOTSPOT_NAME);
  Serial.print("' and open http://");
  Serial.println(Hotspot.ipString());
}

void loop() {
  static uint32_t last = 0;
  delay(5000);
  uint32_t now = Camera.frameCount();
  Serial.print("fps: ");
  Serial.print((now - last) / 5.0);
  Serial.print("   page: http://");
  Serial.println(Hotspot.ipString());
  last = now;
}
