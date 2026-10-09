/*
 * D200Storage.cpp - microSD card (FatFs over SDH) and the light for AiPi-Cam-D200
 */
#include "AiPiCamD200.h"
#include <strings.h>

extern "C" {
#include "fatfs/ff.h"
#include "fatfs/fatfs_diskio_register.h"
#include "bflb_gpio.h"
#include "board.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "lwip/sockets.h"
}

D200SD    SD;
D200Light Light;

static FATFS s_fs;
static void u2str(uint32_t v, char *o);
static SemaphoreHandle_t s_sdLock = NULL;   /* FatFs is not re-entrant: one user at a time */

static bool sdLock(uint32_t ms = 30000) {
  if (!s_sdLock) return true;
  return xSemaphoreTake(s_sdLock, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static void sdUnlock() { if (s_sdLock) xSemaphoreGive(s_sdLock); }

static void fullPath(const char *in, char *out, size_t n) {
  if (strncmp(in, "/sd/", 4) == 0) {
    strncpy(out, in, n - 1);
  } else {
    strcpy(out, "/sd/");
    strncat(out, in[0] == '/' ? in + 1 : in, n - 5);
  }
  out[n - 1] = 0;
}

bool D200SD::begin() {
  if (_mounted) return true;
  if (!s_sdLock) s_sdLock = xSemaphoreCreateMutex();
  board_sdh_gpio_init();          /* SDH: GPIO10..15 */
  fatfs_sdh_driver_register();
  FRESULT r = f_mount(&s_fs, "/sd", 1);
  _err = r;
  _mounted = (r == FR_OK);
  return _mounted;
}

uint32_t D200SD::totalMB() {
  if (!_mounted) return 0;
  return (uint32_t)(((uint64_t)(s_fs.n_fatent - 2) * s_fs.csize * 512) / (1024 * 1024));
}

uint32_t D200SD::freeMB() {
  if (!_mounted) return 0;
  DWORD fre = 0;
  FATFS *fs;
  if (!sdLock()) return 0;
  FRESULT r = f_getfree("/sd", &fre, &fs);
  sdUnlock();
  if (r != FR_OK) return 0;
  return (uint32_t)(((uint64_t)fre * fs->csize * 512) / (1024 * 1024));
}

bool D200SD::writeFile(const char *path, const uint8_t *data, uint32_t len) {
  if (!_mounted) return false;
  char p[96];
  fullPath(path, p, sizeof(p));
  FIL f;
  UINT bw = 0;
  if (!sdLock()) return false;
  FRESULT r = f_open(&f, p, FA_CREATE_ALWAYS | FA_WRITE);
  if (r == FR_OK) {
    r = f_write(&f, data, len, &bw);
    FRESULT rc = f_close(&f);
    if (r == FR_OK) r = rc;
  }
  sdUnlock();
  _err = r;
  return r == FR_OK && bw == len;
}

bool D200SD::exists(const char *path) {
  if (!_mounted) return false;
  char p[96];
  fullPath(path, p, sizeof(p));
  FILINFO fi;
  if (!sdLock()) return false;
  bool ok = f_stat(p, &fi) == FR_OK;
  sdUnlock();
  return ok;
}

/* ---------- clock for file dates (FatFs get_fattime) ---------- */
static uint32_t s_epochBase = 0;   /* local epoch at s_epochMs */
static uint32_t s_epochMs = 0;

void D200SD::setClock(uint32_t localEpoch) {
  s_epochBase = localEpoch;
  s_epochMs = millis();
}

extern "C" DWORD get_fattime(void) {
  if (!s_epochBase) return ((DWORD)(2026 - 1980) << 25) | (1 << 21) | (1 << 16);
  uint32_t t = s_epochBase + (millis() - s_epochMs) / 1000;
  uint32_t days = t / 86400, sec = t % 86400;
  /* civil from days (Howard Hinnant) */
  int32_t z = days + 719468;
  int32_t era = z / 146097;
  uint32_t doe = z - era * 146097;
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t y = yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
  if (y < 1980) y = 1980;
  return ((DWORD)(y - 1980) << 25) | ((DWORD)m << 21) | ((DWORD)d << 16) |
         ((DWORD)(sec / 3600) << 11) | ((DWORD)((sec % 3600) / 60) << 5) | ((sec % 60) / 2);
}

/* number of an "IMG_nnnn.jpg" name, or -1 */
static int imgNumber(const char *n) {
  if (strncasecmp(n, "IMG_", 4) != 0) return -1;
  int v = 0, k = 0;
  const char *p = n + 4;
  while (*p >= '0' && *p <= '9' && k < 6) { v = v * 10 + (*p++ - '0'); k++; }
  if (k == 0 || strcasecmp(p, ".jpg") != 0) return -1;
  return v;
}

static void num4(uint32_t v, char *o) {
  o[0] = '0' + (v / 1000) % 10; o[1] = '0' + (v / 100) % 10;
  o[2] = '0' + (v / 10) % 10;   o[3] = '0' + v % 10; o[4] = 0;
}

String D200SD::savePhoto() {
  if (!_mounted) return "";
  char name[32];
  if (_next == 0) {               /* continue after the highest existing number */
    DIR d; FILINFO fi;
    if (sdLock()) {
      if (f_opendir(&d, "/sd") == FR_OK) {
        while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
          int n = imgNumber(fi.fname);
          if (n > (int)_next) _next = n;
        }
        f_closedir(&d);
      }
      sdUnlock();
    }
  }
  /* find next free number */
  for (int tries = 0; tries < 10000; tries++) {
    _next++;
    char n[5];
    num4(_next, n);
    strcpy(name, "IMG_");
    strcat(name, n);
    strcat(name, ".jpg");
    if (!exists(name)) break;
  }
  uint32_t seq = 0;
  const uint8_t *buf;
  uint32_t len;
  if (d200_cam_lock(&buf, &len, &seq, 2000) != 1) return "";
  bool ok = writeFile(name, buf, len);
  d200_cam_unlock(buf);
  return ok ? String(name) : String("");
}

static void two(uint32_t v, String &s) { s += (char)('0' + v / 10 % 10); s += (char)('0' + v % 10); }

String D200SD::listPhotos(uint16_t maxCount) {
  String out;
  if (!_mounted) return out;
  /* collect numbers, then sort newest (highest) first */
  static uint16_t nums[600];
  static uint32_t sizes[600];
  static uint16_t dates[600], times[600];
  uint16_t cnt = 0;
  DIR d; FILINFO fi;
  if (!sdLock()) return out;
  if (f_opendir(&d, "/sd") == FR_OK) {
    while (cnt < 600 && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
      if (fi.fattrib & AM_DIR) continue;
      int n = imgNumber(fi.fname);
      if (n < 0) continue;
      nums[cnt] = n; sizes[cnt] = fi.fsize; dates[cnt] = fi.fdate; times[cnt] = fi.ftime;
      cnt++;
    }
    f_closedir(&d);
  }
  sdUnlock();
  for (uint16_t i = 1; i < cnt; i++) {          /* insertion sort, descending */
    uint16_t n = nums[i], dt = dates[i], tm = times[i]; uint32_t sz = sizes[i];
    int j = i - 1;
    while (j >= 0 && nums[j] < n) {
      nums[j + 1] = nums[j]; sizes[j + 1] = sizes[j]; dates[j + 1] = dates[j]; times[j + 1] = times[j];
      j--;
    }
    nums[j + 1] = n; sizes[j + 1] = sz; dates[j + 1] = dt; times[j + 1] = tm;
  }
  for (uint16_t i = 0; i < cnt && i < maxCount; i++) {
    char n4[5], sz[12];
    num4(nums[i], n4);
    u2str(sizes[i], sz);
    out += "IMG_"; out += n4; out += ".jpg ";
    out += sz; out += ' ';
    uint32_t y = 1980 + (dates[i] >> 9);
    char yy[6]; u2str(y, yy); out += yy; out += '-';
    two((dates[i] >> 5) & 15, out); out += '-';
    two(dates[i] & 31, out); out += ' ';
    two(times[i] >> 11, out); out += ':';
    two((times[i] >> 5) & 63, out);
    out += '\n';
  }
  return out;
}

uint16_t D200SD::photoCount() {
  if (!_mounted) return 0;
  uint16_t cnt = 0;
  DIR d; FILINFO fi;
  if (!sdLock()) return 0;
  if (f_opendir(&d, "/sd") == FR_OK) {
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0])
      if (!(fi.fattrib & AM_DIR) && imgNumber(fi.fname) >= 0) cnt++;
    f_closedir(&d);
  }
  sdUnlock();
  return cnt;
}

bool D200SD::remove(const char *path) {
  if (!_mounted) return false;
  char p[96];
  fullPath(path, p, sizeof(p));
  if (!sdLock()) return false;
  FRESULT r = f_unlink(p);
  sdUnlock();
  _err = r;
  return r == FR_OK;
}

bool D200SD::removePhoto(uint16_t number) {
  char n4[5], name[20];
  num4(number, n4);
  strcpy(name, "IMG_"); strcat(name, n4); strcat(name, ".jpg");
  return remove(name);
}

/* ---------------------------------------------------------------- */
void D200Light::begin(uint8_t gpio, bool activeHigh) {
  _pin = gpio;
  _high = activeHigh;
  struct bflb_device_s *g = bflb_device_get_by_name("gpio");
  bflb_gpio_init(g, gpio, GPIO_OUTPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
  off();
}

void D200Light::on() {
  if (_pin < 0) return;
  struct bflb_device_s *g = bflb_device_get_by_name("gpio");
  if (_high) bflb_gpio_set(g, _pin); else bflb_gpio_reset(g, _pin);
  _on = true;
}

void D200Light::off() {
  if (_pin < 0) return;
  struct bflb_device_s *g = bflb_device_get_by_name("gpio");
  if (_high) bflb_gpio_reset(g, _pin); else bflb_gpio_set(g, _pin);
  _on = false;
}

void D200Light::toggle() { if (_on) off(); else on(); }

/* ---------------------------------------------------------------- */
/* Serve a file from the SD card over an open socket (used by WebCam) */
static const char *mimeOf(const char *p) {
  const char *e = strrchr(p, '.');
  if (!e) return "application/octet-stream";
  if (!strcasecmp(e, ".html") || !strcasecmp(e, ".htm")) return "text/html; charset=utf-8";
  if (!strcasecmp(e, ".js") || !strcasecmp(e, ".mjs")) return "text/javascript";
  if (!strcasecmp(e, ".wasm")) return "application/wasm";
  if (!strcasecmp(e, ".json")) return "application/json";
  if (!strcasecmp(e, ".css")) return "text/css";
  if (!strcasecmp(e, ".jpg") || !strcasecmp(e, ".jpeg")) return "image/jpeg";
  if (!strcasecmp(e, ".png")) return "image/png";
  if (!strcasecmp(e, ".txt")) return "text/plain";
  return "application/octet-stream";
}

static bool sockSend(int s, const void *d, size_t n) {
  const uint8_t *p = (const uint8_t *)d;
  while (n) {
    int k = lwip_send(s, p, n > 4096 ? 4096 : n, 0);
    if (k <= 0) return false;
    p += k; n -= k;
  }
  return true;
}
static bool sockStr(int s, const char *t) { return sockSend(s, t, strlen(t)); }

static void u2str(uint32_t v, char *o) {
  char t[12]; int i = 0;
  do { t[i++] = '0' + v % 10; v /= 10; } while (v);
  while (i) *o++ = t[--i];
  *o = 0;
}


/* returns true if the file existed and was sent */
bool d200_sd_serve(int s, const char *urlPath) {
  if (!SD.mounted()) {   /* no card: retry at most every 5 s (stray requests must stay cheap) */
    static volatile uint32_t lastTry = 0;
    static volatile bool trying = false;
    uint32_t now = millis();
    if (trying || (lastTry && now - lastTry < 5000)) return false;
    trying = true;
    lastTry = now ? now : 1;
    bool ok = SD.begin();
    trying = false;
    if (!ok) return false;
  }
  char p[128];
  size_t i = 0;
  strcpy(p, "/sd");
  i = 3;
  for (const char *c = urlPath; *c && *c != '?' && *c != ' ' && i < sizeof(p) - 1; c++) p[i++] = *c;
  p[i] = 0;
  if (strstr(p, "..")) return false;

  /* the card lock is held only while touching the card, never while waiting on Wi-Fi:
     a slow phone can't block other files (MediaPipe loads several at once) */
  if (!sdLock(5000)) return false;
  static FIL files[4];
  static volatile bool used[4];
  int fi = -1;
  for (int k = 0; k < 4; k++) if (!used[k]) { used[k] = true; fi = k; break; }
  if (fi < 0) { sdUnlock(); return false; }
  FIL &f = files[fi];
  if (f_open(&f, p, FA_READ) != FR_OK) { used[fi] = false; sdUnlock(); return false; }
  uint32_t size = f_size(&f);
  sdUnlock();
  char num[12];
  u2str(size, num);
  bool ok = sockStr(s, "HTTP/1.1 200 OK\r\nContent-Type: ") && sockStr(s, mimeOf(p)) &&
            sockStr(s, "\r\nContent-Length: ") && sockStr(s, num) &&
            sockStr(s, "\r\nCache-Control: max-age=31536000\r\nConnection: close\r\n\r\n");
  uint8_t *chunk = (uint8_t *)malloc(4096);
  if (!chunk) ok = false;
  while (ok) {
    UINT br = 0;
    if (!sdLock(5000)) break;
    FRESULT r = f_read(&f, chunk, 4096, &br);
    sdUnlock();
    if (r != FR_OK || br == 0) break;
    ok = sockSend(s, chunk, br);
  }
  free(chunk);
  if (sdLock(5000)) { f_close(&f); sdUnlock(); }
  used[fi] = false;
  return true;
}
