/*
 * CameraTest - start the camera and print each JPEG frame size (no WiFi)
 * Board: AiPi-Cam-D200  |  Tools > Board: BL618G0
 */
#include <AiPiCamD200.h>

void setup() {
  Serial.begin(115200);
  delay(1500);
  if (!Camera.begin(50)) {
    Serial.print("Camera not found! sensor id = 0x");
    Serial.println(Camera.sensorId(), HEX);
    while (true) delay(1000);
  }
  Serial.println("Camera OK");
}

void loop() {
  static uint32_t seq = 0;
  const uint8_t *jpg;
  uint32_t len;
  if (Camera.getFrame(jpg, len, seq, 1000)) {
    // jpg[0..len-1] is a complete JPEG image (starts with FF D8)
    Serial.print("frame ");
    Serial.print(seq);
    Serial.print("  size ");
    Serial.print(len);
    Serial.print(" bytes  header ");
    Serial.print(jpg[0], HEX);
    Serial.println(jpg[1], HEX);
    Camera.releaseFrame(jpg);
  }
  delay(500);
}
