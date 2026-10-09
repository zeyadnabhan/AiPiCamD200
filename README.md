# AiPiCamD200

Arduino library for the **Ai-Thinker AiPi-Cam-D200** camera board (Ai-M61-32S module, **BL618** chip).

The board ships without any Arduino library. This one gives you, in a few lines of code:

- **Camera** – GC2145 sensor with the chip's hardware MJPEG encoder, 800×600, ~18 fps
- **WebCam** – web server: live video (`/stream`), snapshot (`/capture`), your own page and your own URLs
- **Hotspot** – the board makes its own Wi-Fi (with DHCP), default address `192.168.169.1`
- **Station** – or join your home Wi-Fi (2.4 GHz)
- **SD** – microSD card (FAT32): save photos, write files
- **Light** – the on-board LED (GPIO16)

```cpp
#include <AiPiCamD200.h>

void setup() {
  Camera.begin(50);                // JPEG quality 1..100
  Hotspot.begin("AiPi-Cam", "");  // "" = open Wi-Fi, or a password of 8+ characters
  WebCam.begin(80);                // open http://192.168.169.1 in the browser
}

void loop() {}
```

## 1. Install

**Board support (bouffalolab core)**
1. Arduino IDE → *File → Preferences → Additional boards manager URLs*, add:
   `https://github.com/bouffalolab/arduino-bouffalo/releases/latest/download/package_bouffalolab_index.json`
2. *Tools → Board → Boards Manager*, search **bouffalolab**, install (tested with **1.0.5**).
3. *Tools → Board → bouffalolab → **BL618G0***.

**This library**
- *Code → Download ZIP* on this page, then in Arduino IDE *Sketch → Include Library → Add .ZIP Library…*
- or clone into your `Documents/Arduino/libraries/` folder.

## 2. Upload

The board has two buttons, **S2** (boot) and **RST** (reset):

1. Plug the board in with USB (CH340, select its COM port).
2. **Hold S2, press and release RST, release S2** – the board is now waiting for code.
3. Click **Upload**.
4. Press **RST** to run your sketch.

> With a CH340 adapter keep the upload speed at 921600 or lower; if uploads fail, try 460800
> (`BL618G0.upload.speed` in the core's `boards.txt`).

## 3. Examples

| Example | What it does |
|---|---|
| **CameraLED** | Start here. Hotspot `AiPi-Cam` + a page with the live camera and an **LED ON/OFF** button. |
| **WebCamHotspot** | Live camera on the built-in page (video, snapshot, light). |
| **WebCamWiFi** | Same, but on your home Wi-Fi – the IP is printed in the Serial Monitor. |
| **WebCamSD** | Live camera + **Save to SD** button (photos saved as `IMG_xxxx.jpg`). |
| **CameraTest** | Camera only, no Wi-Fi: prints the size of every JPEG frame. |
| **SDCardAndLightTest** | Checks the SD card and blinks the LED. |

Open the Serial Monitor at **115200** to see what the board is doing.

## 4. API

### Camera
```cpp
bool Camera.begin(uint8_t quality = 50);   // starts the sensor (also switches its power on)
uint32_t Camera.frameCount();               // frames captured so far (fps = difference per second)
uint16_t Camera.width(), Camera.height();   // 800 x 600
// Get the newest JPEG frame (wait up to timeoutMs). Always release it after use.
bool Camera.getFrame(const uint8_t *&jpg, uint32_t &len, uint32_t &seq, uint32_t timeoutMs);
void Camera.releaseFrame(const uint8_t *jpg);
```

### Wi-Fi
```cpp
bool Hotspot.begin(const char *name, const char *password = "", uint8_t channel = 6);
String Hotspot.ipString();                 // "192.168.169.1"
bool Hotspot.dnsBegin();                   // optional: phones treat the hotspot as "working Wi-Fi"

bool Station.begin(const char *name, const char *password, uint32_t timeoutMs = 20000);
bool Station.connected();
String Station.ipString();
```

### WebCam (web server)
```cpp
bool WebCam.begin(uint16_t port = 80);
void WebCam.setPage(const char *html);     // your own page at "/" instead of the built-in one
void WebCam.setTitle(const char *title);   // title of the built-in page
void WebCam.setLog(bool on);               // print every request to Serial

// Your own URL. The function gets the text after '?' and returns the reply text.
// These connections stay open, so a page can call them 10-20 times a second.
String onLed(const char *query) {
  if (D200WebCam::param(query, "on", 0)) Light.on(); else Light.off();
  return "ok";
}
WebCam.on("/led", onLed);                  // up to 8 routes

// Serve a fixed file from flash (icon, script, manifest...), up to 8
WebCam.file("/icon.png", "image/png", ICON, sizeof(ICON));
```
Built-in URLs: `/` page, `/stream` MJPEG video (use it in `<img src="/stream">`), `/capture` one JPEG,
`/save` save a photo to the SD card, `/light` toggle the LED, `/detect` live object detection in the browser
(MediaPipe, needs its files in an `mp` folder on the SD card). Any other path is served from the SD card if
the file exists there.

### SD card
```cpp
bool SD.begin();                          // FAT32 card
uint32_t SD.totalMB(), SD.freeMB();
String SD.savePhoto();                    // saves the current frame, returns its name ("" on error)
bool SD.writeFile(const char *path, const uint8_t *data, uint32_t len);
bool SD.exists(const char *path);
bool SD.remove(const char *path);
```

### Light (on-board LED)
```cpp
void Light.begin(uint8_t gpio, bool activeHigh = true);   // D200: Light.begin(16)
void Light.on(), Light.off(), Light.toggle();
bool Light.isOn();
```

## 5. Board notes

| | |
|---|---|
| On-board LED | **GPIO16** |
| Camera power | switched by Q1, **GPIO19 LOW = on** (the library does this) |
| Camera | GC2145 on DVP; I2C on GPIO0/1 – these pins are taken |
| microSD | SDH on GPIO10–15 |
| USB serial (CH340) | GPIO21 (TX) / GPIO22 (RX), 115200 |
| Not used by the library | GPIO17, GPIO18 (e.g. UART1 to another board), GPIO20 |

**Built-in workarounds for the bouffalolab core**
- The core's Wi-Fi library binary uses a different `wifi_mgmr_ap_params_t` layout than its header; the
  library passes the right layout, so the hotspot really starts.
- The core's `snprintf` is unreliable with `%u`; the web server builds its numbers without it.
- Avoid **global `String` objects** in your sketch on this core – they can crash the board at boot.

## 6. Troubleshooting

- **Upload fails / "shake hand fail"** – do the S2 + RST sequence again right before clicking Upload;
  check the COM port; close any Serial Monitor that holds the port.
- **"Camera not found"** – check the camera ribbon cable; power the board from a good USB port.
- **No Wi-Fi network** – open the Serial Monitor (115200) after pressing RST to see the error.
- **Video slow** – lower the JPEG quality (`Camera.begin(30)`), stay close to the board.

## License

MIT – see [LICENSE](LICENSE). FatFs (ChaN, BSD-style) and code adapted from Bouffalo Lab
`bouffalo_sdk` (Apache-2.0) keep their own licenses, listed in the LICENSE file.
