/*
 * AiPiCamD200 - Arduino library for the Ai-Thinker AiPi-Cam-D200 (Ai-M61-32S / BL618)
 *
 *   D200Camera   : GC2145 camera + hardware MJPEG encoder (800x600)
 *   D200Hotspot  : WiFi access point (hotspot) with DHCP
 *   D200Station  : join your home WiFi (station mode)
 *   D200WebCam   : web page + live MJPEG stream + snapshot (+ save to SD)
 *   D200SD       : microSD card (FAT32) through the SDH controller
 *   D200Light    : on-board LED / flash light
 *
 * Arduino core: bouffalolab "BL618G0" board package.
 */
#ifndef AIPI_CAM_D200_H
#define AIPI_CAM_D200_H

#include <Arduino.h>
#include "d200_cam.h"

/* ---------------------------------------------------------------- */
class D200Camera {
public:
  /* quality 1..100 (50 is a good default). Returns false if the sensor does not answer. */
  bool begin(uint8_t quality = 50);

  const char *sensorName() { return d200_cam_sensor_name(); }
  uint16_t sensorId()      { return d200_cam_sensor_id(); }
  uint16_t width()         { return d200_cam_width(); }
  uint16_t height()        { return d200_cam_height(); }
  uint32_t frameCount()    { return d200_cam_frame_count(); }

  /* Get the newest JPEG frame (newer than seq). Call releaseFrame(buf) when done. */
  bool getFrame(const uint8_t *&buf, uint32_t &len, uint32_t &seq, uint32_t timeoutMs = 1000) {
    return d200_cam_lock(&buf, &len, &seq, timeoutMs) == 1;
  }
  void releaseFrame(const uint8_t *buf) { d200_cam_unlock(buf); }
};

/* ---------------------------------------------------------------- */
class D200Hotspot {
public:
  /* password: empty = open hotspot, otherwise 8+ characters (WPA2) */
  bool begin(const char *ssid, const char *password = "", uint8_t channel = 6);
  uint32_t ip();              /* raw IPv4 (little endian), 0 if not up */
  String ipString();          /* e.g. "192.168.169.1" */
  /* answer DNS on the hotspot: every name -> this camera. Phones then treat the
     Wi-Fi as working and keep using it (call after begin()). */
  bool dnsBegin();
  /* range: 802.11b rates (the farthest-reaching) to dBm, the slow OFDM rates up to +2 dB
     (fast rates keep the factory values so they stay clean). Prints the table if log. */
  bool maxRange(int8_t dBm = 20, bool log = false);
};

/* ---------------------------------------------------------------- */
class D200Station {
public:
  /* Join an existing 2.4 GHz WiFi network. Waits up to timeoutMs for an IP. */
  bool begin(const char *ssid, const char *password, uint32_t timeoutMs = 20000);
  bool connected();           /* true while we have an IP */
  uint32_t ip();              /* raw IPv4 (little endian), 0 if none */
  String ipString();
};

/* ---------------------------------------------------------------- */
class D200SD {
public:
  /* Mount the microSD card (FAT/FAT32). Pins: SDH on GPIO10..15 (4-bit). */
  bool begin();
  bool mounted() { return _mounted; }
  int lastError() { return _err; }         /* FatFs FRESULT of the last call */
  uint32_t totalMB();
  uint32_t freeMB();
  /* path like "/sd/photo.jpg" or just "photo.jpg" */
  bool writeFile(const char *path, const uint8_t *data, uint32_t len);
  bool exists(const char *path);
  /* save the newest camera frame as /sd/IMG_0001.jpg, IMG_0002.jpg ...
     returns the file name ("" on failure) */
  String savePhoto();
  /* photos (IMG_nnnn.jpg) in the card root, newest first, one per line:
     "IMG_0012.jpg <bytes> <YYYY-MM-DD HH:MM>" */
  String listPhotos(uint16_t maxCount = 300);
  uint16_t photoCount();
  bool removePhoto(uint16_t number);          /* deletes IMG_<number>.jpg */
  bool remove(const char *path);
  /* set the clock used for file dates (seconds since 1970, local time) */
  void setClock(uint32_t localEpoch);
  bool _mounted = false;
  int _err = 0;
  uint32_t _next = 0;
};

/* ---------------------------------------------------------------- */
class D200Light {
public:
  /* activeHigh: true if writing HIGH turns the light on */
  void begin(uint8_t gpio, bool activeHigh = true);
  void on();
  void off();
  void toggle();
  bool isOn() { return _on; }
  int pin() { return _pin; }
  int _pin = -1;
  bool _high = true;
  bool _on = false;
};

/* ---------------------------------------------------------------- */
/* Your own URL: gets the text after '?' (e.g. "l=100&r=-50"), returns the
   reply text. Runs in the web server task: keep it short (set variables). */
typedef String (*D200Route)(const char *query);

class D200WebCam {
public:
  /* Serves:  /  (page)   /stream  (MJPEG)   /capture  (one JPEG) */
  bool begin(uint16_t port = 80);
  void setTitle(const char *title) { _title = title; }
  void setLog(bool on) { _log = on; }   /* print each request to Serial */
  /* replace the built-in "/" page with your own HTML */
  void setPage(const char *html) { _page = html; }
  /* add your own URL, e.g. WebCam.on("/drive", onDrive). Up to 8.
     These replies keep the connection open, so a page can call them
     10-20 times a second. */
  bool on(const char *path, D200Route fn);
  /* serve a fixed file from flash (manifest, icon, script...). Up to 8.
     e.g. WebCam.file("/icon.png", "image/png", ICON, sizeof(ICON)) */
  bool file(const char *path, const char *mime, const void *data, uint32_t len);
  /* read a number from a query: param("l=100&r=-5", "r", 0) -> -5 */
  static int param(const char *query, const char *name, int def = 0);
  /* open connections (video, app...) - for diagnostics */
  static int clients();
  /* frames sent on /stream so far (the app uses it to notice a stopped video) */
  static uint32_t streamFrames();
  /* /stream?fps=N limits the video rate (weak Wi-Fi: fewer frames, controls stay quick) */

  /* internal */
  static D200WebCam *_self;
  const char *_title = "AiPi-Cam-D200";
  const char *_page = nullptr;
  bool _log = false;
  uint16_t _port = 80;
  const char *_routePath[8];
  D200Route _routeFn[8];
  uint8_t _routes = 0;
  const char *_filePath[8], *_fileMime[8];
  const void *_fileData[8];
  uint32_t _fileLen[8];
  uint8_t _files = 0;
};

extern D200Camera  Camera;
extern D200Hotspot Hotspot;
extern D200Station Station;
extern D200WebCam  WebCam;
extern D200SD      SD;
extern D200Light   Light;

#endif
