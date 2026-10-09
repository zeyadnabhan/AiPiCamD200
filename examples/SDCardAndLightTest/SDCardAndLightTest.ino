/*
 * SDCardAndLightTest - SD card + light (GPIO16) test for AiPi-Cam-D200
 * Prints the SD result every few seconds and blinks the light.
 * Board: AiPi-Cam-D200  |  Tools > Board: BL618G0  |  Serial 115200
 * Tested: SD (FAT32, 8 GB) mount + write OK, light on GPIO16 OK.
 */
#include <AiPiCamD200.h>

const int LIGHT_GPIO = 16;
bool sdOk = false;
bool wrote = false;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Light.begin(LIGHT_GPIO);
  sdOk = SD.begin();
  if (sdOk) {
    const char msg[] = "Hello from AiPi-Cam-D200\r\n";
    wrote = SD.writeFile("test.txt", (const uint8_t *)msg, sizeof(msg) - 1);
  }
}

void loop() {
  Serial.println("=== SD card + light test ===");
  if (sdOk) {
    Serial.print("SD OK  total ");
    Serial.print(SD.totalMB());
    Serial.print(" MB  free ");
    Serial.print(SD.freeMB());
    Serial.println(" MB");
    Serial.println(wrote ? "wrote /sd/test.txt" : "write FAILED");
    if (!wrote) { Serial.print("FatFs error "); Serial.println(SD.lastError()); }
  } else {
    Serial.print("SD mount FAILED, FatFs error ");
    Serial.println(SD.lastError());
  }
  Light.on();
  Serial.println("light ON");
  delay(1500);
  Light.off();
  Serial.println("light OFF");
  delay(1500);
}
