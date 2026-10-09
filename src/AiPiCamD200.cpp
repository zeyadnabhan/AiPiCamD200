/*
 * AiPiCamD200.cpp - camera wrapper, hotspot and web server
 */
#include "AiPiCamD200.h"
#include <WiFi.h>

extern "C" {
#include "wifi_mgmr_ext.h"
#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "FreeRTOS.h"
#include "task.h"
}

D200Camera  Camera;
D200Hotspot Hotspot;
D200Station Station;
D200WebCam  WebCam;
D200WebCam *D200WebCam::_self = nullptr;

/* ================================================================ */
/* Camera                                                           */
/* ================================================================ */
bool D200Camera::begin(uint8_t quality) {
  if (quality < 1) quality = 1;
  if (quality > 100) quality = 100;
  return d200_cam_begin(quality) == 0;
}

/* ================================================================ */
/* Hotspot                                                          */
/* ================================================================ */
/* Field layout used by the WiFi library shipped in the bouffalolab
   Arduino core. It differs from wifi_mgmr_ap_params_t in its header
   (found by disassembling wifi_mgmr_ap_start). */
struct d200_ap_params {
  char *ssid;
  char *key;
  char *akm;
  uint8_t channel;
  uint8_t type;
  bool use_dhcpd;
  int start;
  int limit;
  uint32_t ap_ipaddr;
  uint32_t ap_mask;
  uint32_t ap_max_inactivity;
  bool hidden_ssid;
  bool isolation;
  uint8_t reserved[32];
};

static char s_ssid[33];
static char s_pass[65];
static bool s_wifi_started = false;

/* start the WiFi firmware once (hotspot and station share it) */
static bool startWifi() {
  if (s_wifi_started) return true;
  WiFi.init_wifi();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_IDLE_STATUS) {
    if (millis() - t0 > 10000) return false;
    delay(100);
  }
  s_wifi_started = true;
  return true;
}

static void u2s(uint32_t v, char *out);

static String ipToString(uint32_t a) {
  String s;
  char n[12];
  for (int i = 0; i < 4; i++) {
    u2s((a >> (8 * i)) & 0xff, n);
    s += n;
    if (i < 3) s += ".";
  }
  return s;
}

bool D200Hotspot::begin(const char *ssid, const char *password, uint8_t channel) {
  strncpy(s_ssid, ssid ? ssid : "AiPi-Cam", sizeof(s_ssid) - 1);
  strncpy(s_pass, password ? password : "", sizeof(s_pass) - 1);

  if (!startWifi()) return false;

  d200_ap_params ap;
  memset(&ap, 0, sizeof(ap));
  bool secure = strlen(s_pass) >= 8;
  ap.ssid = s_ssid;
  ap.key = secure ? s_pass : NULL;
  ap.akm = secure ? (char *)"WPA2" : (char *)"OPEN";
  ap.channel = channel ? channel : 6;
  ap.use_dhcpd = true;
  ap.start = 2;
  ap.limit = 10;
  ap.ap_max_inactivity = 120;
  wifi_mgmr_ap_start((wifi_mgmr_ap_params_t *)&ap);

  uint32_t t0 = millis();
  while (ip() == 0 && millis() - t0 < 5000) delay(100);
  return ip() != 0;
}

uint32_t D200Hotspot::ip() {
  for (struct netif *nf = netif_list; nf; nf = nf->next) {
    uint32_t a = ip4_addr_get_u32(netif_ip4_addr(nf));
    if (a && (a & 0xff) != 127) return a;
  }
  return 0;
}

static void u2s(uint32_t v, char *out) {
  /* own number->text: the core's snprintf is unreliable with %u */
  char tmp[12];
  int i = 0;
  do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
  while (i) *out++ = tmp[--i];
  *out = 0;
}

String D200Hotspot::ipString() { return ipToString(ip()); }

/* ---- DNS: answers every A query with the hotspot IP (phones then reach the
   car by any name and their "internet check" URLs land on our web server) ---- */
static void dnsTask(void *arg) {
  int s = lwip_socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(53);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (s < 0 || lwip_bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) { vTaskDelete(NULL); return; }
  static uint8_t b[512];
  while (true) {
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n = lwip_recvfrom(s, b, sizeof(b), 0, (struct sockaddr *)&from, &fl);
    if (n < 17 || (b[2] & 0x80)) continue;               /* too short or not a query */
    if (b[4] != 0 || b[5] != 1) continue;                /* exactly one question */
    int q = 12;                                          /* walk the question name */
    while (q < n && b[q]) q += b[q] + 1;
    if (q + 5 > n) continue;
    uint16_t qtype = (b[q + 1] << 8) | b[q + 2];
    /* answer only the phone "is there internet?" checks; every other name gets
       "does not exist", so background apps don't flood the camera's web server */
    char name[96];
    int ni = 0;
    for (int k = 12; k < q && ni < (int)sizeof(name) - 1;) {
      int L = b[k++];
      for (int j = 0; j < L && k < q && ni < (int)sizeof(name) - 1; j++) name[ni++] = (char)b[k++] | 0x20;
      if (ni < (int)sizeof(name) - 1) name[ni++] = '.';
    }
    name[ni] = 0;
    static const char *const ok[] = {"connectivitycheck", "clients", "www.google.com", "gstatic.com",
                                     "msftconnecttest", "msftncsi", "captive.apple.com", "apple.com", 0};
    bool known = false;
    for (int k = 0; ok[k]; k++) if (strstr(name, ok[k])) { known = true; break; }
    int end = q + 5;                                     /* end of question */
    b[2] = 0x81; b[3] = known ? 0x80 : 0x83;             /* response; 3 = name does not exist */
    b[6] = 0; b[7] = 0; b[8] = 0; b[9] = 0; b[10] = 0; b[11] = 0;
    int len = end;
    uint32_t ip = Hotspot.ip();
    if (known && qtype == 1 && ip && end + 16 <= (int)sizeof(b)) {
      b[7] = 1;                                          /* one answer */
      uint8_t *p = b + end;
      p[0] = 0xC0; p[1] = 0x0C;                          /* name: pointer to question */
      p[2] = 0; p[3] = 1; p[4] = 0; p[5] = 1;            /* type A, class IN */
      p[6] = 0; p[7] = 0; p[8] = 0; p[9] = 60;           /* TTL 60 s */
      p[10] = 0; p[11] = 4;
      memcpy(p + 12, &ip, 4);                            /* already network order */
      len = end + 16;
    }
    lwip_sendto(s, b, len, 0, (struct sockaddr *)&from, fl);
  }
}

static void printRow(const char *name, const int8_t *v, int n) {
  Serial.print(name);
  for (int i = 0; i < n; i++) { Serial.print(' '); Serial.print((int)v[i]); }
  Serial.println();
}
bool D200Hotspot::maxRange(int8_t dBm, bool log) {
  rf_pwr_table_t t;
  memset(&t, 0, sizeof(t));
  if (wifi_mgmr_tpc_pwr_get(&t) != 0) return false;
  if (log) { printRow("[wifi] tx dBm before b:", t.pwr_11b, 4); printRow("[wifi] g:", t.pwr_11g, 8); printRow("[wifi] n:", t.pwr_11n_ht20, 8); }
  /* the table can't be read on this firmware (all zero): leave it alone */
  bool any = false;
  for (int i = 0; i < 4; i++) if (t.pwr_11b[i]) any = true;
  if (!any) return false;
  for (int i = 0; i < 4; i++) if (t.pwr_11b[i] < dBm) t.pwr_11b[i] = dBm;            /* 1-11 Mbps */
  /* OFDM (6-18 Mbps, MCS0-2): +2 dB over factory at most, never above dBm-2 - clean signal, no current spikes */
  int8_t ofdm = dBm - 2;
  for (int i = 0; i < 4; i++) { int8_t v = t.pwr_11g[i] + 2; if (v > ofdm) v = ofdm; if (v > t.pwr_11g[i]) t.pwr_11g[i] = v; }
  for (int i = 0; i < 3; i++) {
    int8_t v = t.pwr_11n_ht20[i] + 2; if (v > ofdm) v = ofdm; if (v > t.pwr_11n_ht20[i]) t.pwr_11n_ht20[i] = v;
    v = t.pwr_11ax_he20[i] + 2; if (v > ofdm) v = ofdm; if (v > t.pwr_11ax_he20[i]) t.pwr_11ax_he20[i] = v;
  }
  int r = wifi_mgmr_tpc_pwr_set(&t);
  if (log) {
    memset(&t, 0, sizeof(t));
    wifi_mgmr_tpc_pwr_get(&t);
    printRow("[wifi] tx dBm after  b:", t.pwr_11b, 4); printRow("[wifi] g:", t.pwr_11g, 8); printRow("[wifi] n:", t.pwr_11n_ht20, 8);
  }
  return r == 0;
}

bool D200Hotspot::dnsBegin() {
  static bool started = false;
  if (started) return true;
  started = xTaskCreate(dnsTask, "d200_dns", 1024, NULL, 9, NULL) == pdPASS;
  return started;
}

/* ================================================================ */
/* Station (join your WiFi)                                         */
/* ================================================================ */
static char s_sta_ssid[33];
static char s_sta_pass[65];

bool D200Station::begin(const char *ssid, const char *password, uint32_t timeoutMs) {
  if (!startWifi()) return false;
  strncpy(s_sta_ssid, ssid ? ssid : "", sizeof(s_sta_ssid) - 1);
  strncpy(s_sta_pass, password ? password : "", sizeof(s_sta_pass) - 1);
  /* freq 0 = scan all channels, pmf 1 = optional, dhcp on */
  wifi_sta_connect(s_sta_ssid, s_sta_pass[0] ? s_sta_pass : NULL, NULL, NULL, 1, 0, 0, 1);
  uint32_t t0 = millis();
  while (ip() == 0 && millis() - t0 < timeoutMs) delay(250);
  return ip() != 0;
}

uint32_t D200Station::ip() {
  uint32_t a = 0;
  if (wifi_sta_ip4_addr_get(&a, NULL, NULL, NULL) != 0) return 0;
  return a;
}

bool D200Station::connected() { return ip() != 0; }
String D200Station::ipString() { return ipToString(ip()); }

/* ================================================================ */
/* Web server                                                       */
/* ================================================================ */
static bool sendAll(int s, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  while (len > 0) {
    int n = lwip_send(s, p, len > 4096 ? 4096 : len, 0);
    if (n <= 0) return false;
    p += n;
    len -= n;
  }
  return true;
}

static bool sendStr(int s, const char *str) { return sendAll(s, str, strlen(str)); }

static bool sendLen(int s, uint32_t len) {
  char num[12];
  u2s(len, num);
  return sendStr(s, "Content-Length: ") && sendStr(s, num) && sendStr(s, "\r\n");
}

/* Only the newest video connection is served: a phone that reconnects (weak Wi-Fi)
   ends the old one, so stalled connections never pile up and hold camera buffers. */
static volatile uint32_t s_streamGen = 0;
static volatile uint32_t s_streamFrames = 0;   /* frames delivered to the phone (all streams) */
uint32_t D200WebCam::streamFrames() { return s_streamFrames; }
static void handleStream(int s, int fps) {
  int zero = 0;   /* video: let TCP pack the frame into full packets (as in the first version) */
  lwip_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
  struct timeval stv = {15, 0};   /* video may wait out Wi-Fi hiccups; only a really dead phone ends it */
  lwip_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
  taskENTER_CRITICAL();
  uint32_t my = ++s_streamGen;
  taskEXIT_CRITICAL();
  /* each frame is copied out of the camera before sending: a slow phone never holds
     a camera buffer, so the camera can't stop because of the network */
  uint8_t *copy = (uint8_t *)malloc(64 * 1024);
  if (!sendStr(s, "HTTP/1.1 200 OK\r\n"
                  "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                  "Cache-Control: no-cache\r\nConnection: close\r\n\r\n")) { free(copy); return; }
  uint32_t seq = 0, gapMs = fps > 0 ? 1000 / fps : 0, last = 0;
  int misses = 0;
  while (s_streamGen == my) {
    const uint8_t *buf;
    uint32_t len;
    if (gapMs) {   /* rate limit: wait for the next slot, then take the newest frame */
      uint32_t el = millis() - last;
      if (el < gapMs) vTaskDelay(pdMS_TO_TICKS(gapMs - el));
    }
    if (!d200_cam_lock(&buf, &len, &seq, 2000)) {
      if (++misses >= 5) break;   /* camera stopped for 10 s: free this connection */
      continue;
    }
    misses = 0;
    last = millis();
    const uint8_t *src = buf;
    bool copied = copy && len <= 64 * 1024;
    if (copied) { memcpy(copy, buf, len); d200_cam_unlock(buf); src = copy; }
    char hdr[80], *h = hdr;
    const char *t1 = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    while (*t1) *h++ = *t1++;
    u2s(len, h); while (*h) h++;
    *h++ = '\r'; *h++ = '\n'; *h++ = '\r'; *h++ = '\n';
    bool ok = sendAll(s, hdr, h - hdr);
    const uint8_t *p = src;
    uint32_t left = len;
    while (ok && left) {
      if (s_streamGen != my) { ok = false; break; }   /* the phone opened a newer stream */
      int n = lwip_send(s, p, left > 4096 ? 4096 : left, 0);
      if (n <= 0) { ok = false; break; }
      p += n; left -= n;
    }
    if (ok) ok = sendAll(s, "\r\n", 2);
    if (!copied) d200_cam_unlock(buf);
    if (!ok) break;   /* browser closed the page */
    s_streamFrames++;
  }
  free(copy);
}

static void handleCapture(int s) {
  uint32_t seq = 0;
  const uint8_t *buf;
  uint32_t len;
  if (!d200_cam_lock(&buf, &len, &seq, 3000)) {
    sendStr(s, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    return;
  }
  sendStr(s, "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
             "Content-Disposition: inline; filename=capture.jpg\r\nConnection: close\r\n");
  sendLen(s, len);
  sendStr(s, "\r\n");
  sendAll(s, buf, len);
  d200_cam_unlock(buf);
}

static void sendText(int s, const char *t) {
  sendStr(s, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n");
  sendLen(s, strlen(t));
  sendStr(s, "\r\n");
  sendStr(s, t);
}

static void handlePage(int s, const char *title) {
  static const char a[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'><title>";
  static const char b[] =
    "</title><style>"
    "body{margin:0;background:#111;color:#eee;font-family:sans-serif;text-align:center}"
    "h1{font-size:18px;margin:12px}img{max-width:100%;height:auto;border-radius:8px}"
    "a{color:#6cf}</style></head><body><h1>";
  static const char c[] =
    "</h1><img src='/stream'>"
    "<p><a href='/capture' target='_blank'>Snapshot</a> &nbsp; "
    "<a href='#' onclick=\"fetch('/save').then(r=>r.text()).then(t=>document.getElementById('m').innerText=t);return false\">Save to SD</a> &nbsp; "
    "<a href='/detect'>Detect</a> &nbsp; "
    "<a href='#' onclick=\"fetch('/light').then(r=>r.text()).then(t=>document.getElementById('m').innerText=t);return false\">Light</a></p>"
    "<p id='m'></p></body></html>";
  uint32_t len = strlen(a) + strlen(title) + strlen(b) + strlen(title) + strlen(c);
  sendStr(s, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n");
  sendLen(s, len);
  sendStr(s, "\r\n");
  sendStr(s, a); sendStr(s, title); sendStr(s, b); sendStr(s, title); sendStr(s, c);
}

bool d200_sd_serve(int s, const char *urlPath);

/* Object detection page: MediaPipe EfficientDet-Lite0 running in the browser.
   Library + model are served from the SD card folder /mp (see README). */
static const char DETECT_HTML[] =
"<!doctype html><html><head><meta charset='utf-8'>\n"
"<meta name='viewport' content='width=device-width,initial-scale=1'><title>D200 Detect</title><style>\n"
"body{margin:0;background:#111;color:#eee;font-family:sans-serif;text-align:center}\n"
"h1{font-size:18px;margin:12px}#w{position:relative;display:inline-block;max-width:100%}\n"
"#v{max-width:100%;display:block;border-radius:8px}#c{position:absolute;left:0;top:0;width:100%;height:100%}\n"
"#s{margin:8px;font-size:14px;color:#9cf}a{color:#6cf}</style></head><body>\n"
"<h1>AiPi-Cam-D200 &middot; Live Detect</h1><div id='s'>Loading MediaPipe from the SD card (first time can take ~1 min)...</div>\n"
"<div id='w'><img id='v' src='/stream'><canvas id='c' width='800' height='600'></canvas></div>\n"
"<p><a href='/'>Back</a></p>\n"
"<script type='module'>\n"
"const s=document.getElementById('s'),c=document.getElementById('c'),x=c.getContext('2d');\n"
"let det;\n"
"try{\n"
" const {ObjectDetector,FilesetResolver}=await import('/mp/vision_bundle.mjs');\n"
" const fs=await FilesetResolver.forVisionTasks('/mp/wasm');\n"
" det=await ObjectDetector.createFromOptions(fs,{baseOptions:{modelAssetPath:'/mp/efficientdet_lite0.tflite',delegate:'CPU'},scoreThreshold:0.3,maxResults:10,runningMode:'IMAGE'});\n"
"}catch(e){s.textContent='Could not load MediaPipe from SD: '+e;throw e;}\n"
"s.textContent='Detecting...';\n"
"let n=0,t0=performance.now();\n"
"async function loop(){\n"
" try{\n"
"  const r0=await fetch('/capture?'+Date.now(),{cache:'no-store'});\n"
"  if(!r0.ok)throw new Error('capture '+r0.status);\n"
"  const bm=await createImageBitmap(await r0.blob());\n"
"  if(c.width!=bm.width){c.width=bm.width;c.height=bm.height;}\n"
"  const r=det.detect(bm);bm.close();\n"
"  x.clearRect(0,0,c.width,c.height);x.lineWidth=4;x.font='bold 22px sans-serif';\n"
"  const names=[];\n"
"  for(const d of r.detections){const b=d.boundingBox,k=d.categories[0];\n"
"   const t=k.categoryName+' '+Math.round(k.score*100)+'%';names.push(t);\n"
"   x.strokeStyle='#0f0';x.strokeRect(b.originX,b.originY,b.width,b.height);\n"
"   const ty=Math.max(0,b.originY-26);\n"
"   x.fillStyle='#0f0';x.fillRect(b.originX,ty,x.measureText(t).width+10,26);\n"
"   x.fillStyle='#000';x.fillText(t,b.originX+5,ty+20);}\n"
"  n++;const dt=(performance.now()-t0)/1000;\n"
"  if(dt>1){s.textContent=(n/dt).toFixed(1)+' detections/s  |  '+(names.join(', ')||'nothing');n=0;t0=performance.now();}\n"
" }catch(e){s.textContent='Error: '+e;await new Promise(r=>setTimeout(r,500));}\n"
" setTimeout(loop,0);\n"
"}\n"
"loop();\n"
"</script></body></html>\n";

static bool handleFile(int s, const char *req);
static void handleOne(int s, char *req) {
  D200WebCam *w = D200WebCam::_self;

  if (handleFile(s, req)) {
    /* fixed file from flash */
  } else if (strncmp(req, "GET /stream", 11) == 0) {
    const char *f = strstr(req, "fps=");
    handleStream(s, f ? atoi(f + 4) : 0);
  } else if (strncmp(req, "GET /capture", 12) == 0) {
    handleCapture(s);
  } else if (strncmp(req, "GET /save", 9) == 0) {
    String r;
    if (!SD.mounted()) SD.begin();
    if (!SD.mounted()) r = "SD card not found";
    else {
      String n = SD.savePhoto();
      r = n.length() ? ("Saved " + n) : String("Save failed");
    }
    sendText(s, r.c_str());
  } else if (strncmp(req, "GET /light", 10) == 0) {
    if (Light.pin() < 0) sendText(s, "Light pin not set (Light.begin(pin))");
    else { Light.toggle(); sendText(s, Light.isOn() ? "Light ON" : "Light OFF"); }
  } else if (strncmp(req, "GET /detect", 11) == 0) {
    sendStr(s, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n");
    sendLen(s, strlen(DETECT_HTML));
    sendStr(s, "\r\n");
    sendStr(s, DETECT_HTML);
  } else if ((strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /?", 6) == 0) && w && w->_page) {
    sendStr(s, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n");
    sendLen(s, strlen(w->_page));
    sendStr(s, "\r\n");
    sendStr(s, w->_page);
  } else if (strncmp(req, "GET / ", 6) == 0) {
    handlePage(s, w ? w->_title : "AiPi-Cam-D200");
  } else if (strncmp(req, "GET /generate_204", 17) == 0 || strncmp(req, "GET /gen_204", 12) == 0) {
    sendStr(s, "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
  } else if (strncmp(req, "GET /hotspot-detect.html", 24) == 0 || strncmp(req, "GET /library/test/success.html", 30) == 0) {
    static const char ok[] = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
    sendStr(s, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n");
    sendLen(s, strlen(ok)); sendStr(s, "\r\n"); sendStr(s, ok);
  } else if (strncmp(req, "GET /connecttest.txt", 20) == 0) {
    sendText(s, "Microsoft Connect Test");
  } else if (strncmp(req, "GET /ncsi.txt", 13) == 0) {
    sendText(s, "Microsoft NCSI");
  } else if (strncmp(req, "GET /", 5) == 0 && d200_sd_serve(s, req + 4)) {
    /* served a file from the SD card */
  } else {
    sendStr(s, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
  }
}

bool D200WebCam::on(const char *path, D200Route fn) {
  if (_routes >= 8 || !path || !fn) return false;
  _routePath[_routes] = path;
  _routeFn[_routes] = fn;
  _routes++;
  return true;
}

bool D200WebCam::file(const char *path, const char *mime, const void *data, uint32_t len) {
  if (_files >= 8 || !path || !mime || !data) return false;
  _filePath[_files] = path;
  _fileMime[_files] = mime;
  _fileData[_files] = data;
  _fileLen[_files] = len;
  _files++;
  return true;
}

/* fixed files from flash. Returns true if served. */
static bool handleFile(int s, const char *req) {
  D200WebCam *w = D200WebCam::_self;
  if (!w || strncmp(req, "GET /", 5) != 0) return false;
  const char *path = req + 4;
  size_t n = strcspn(path, " ?");
  for (uint8_t i = 0; i < w->_files; i++) {
    if (strlen(w->_filePath[i]) == n && strncmp(path, w->_filePath[i], n) == 0) {
      sendStr(s, "HTTP/1.1 200 OK\r\nContent-Type: ");
      sendStr(s, w->_fileMime[i]);
      sendStr(s, "\r\nCache-Control: no-cache\r\nConnection: close\r\n");
      sendLen(s, w->_fileLen[i]);
      sendStr(s, "\r\n");
      sendAll(s, w->_fileData[i], w->_fileLen[i]);
      return true;
    }
  }
  return false;
}

int D200WebCam::param(const char *q, const char *name, int def) {
  if (!q || !name) return def;
  size_t nl = strlen(name);
  const char *p = q;
  while (*p) {
    if (strncmp(p, name, nl) == 0 && p[nl] == '=') {
      p += nl + 1;
      bool neg = false;
      if (*p == '-') { neg = true; p++; }
      if (*p < '0' || *p > '9') return def;
      long v = 0;
      while (*p >= '0' && *p <= '9') { if (v < 100000) v = v * 10 + (*p - '0'); p++; }   /* saturate: no overflow */
      return neg ? -v : v;
    }
    const char *amp = strchr(p, '&');
    if (!amp) break;
    p = amp + 1;
  }
  return def;
}

/* user routes: reply and keep the connection open. Returns true if handled. */
static bool handleRoute(int s, char *req) {
  D200WebCam *w = D200WebCam::_self;
  if (!w || w->_routes == 0 || strncmp(req, "GET /", 5) != 0) return false;
  char *path = req + 4;
  char *sp = strchr(path, ' ');
  if (sp) *sp = 0;
  char *q = strchr(path, '?');
  if (q) *q++ = 0;
  for (uint8_t i = 0; i < w->_routes; i++) {
    if (strcmp(path, w->_routePath[i]) == 0) {
      String r = w->_routeFn[i](q ? q : "");
      /* whole reply in ONE packet: no Nagle / delayed-ACK wait on the phone */
      static const char hdr[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                                "Cache-Control: no-store\r\nConnection: keep-alive\r\nContent-Length: ";
      size_t bl = r.length();
      if (bl > 1200) {   /* big reply: send in parts */
        char num[12];
        u2s(bl, num);
        sendStr(s, hdr); sendStr(s, num); sendStr(s, "\r\n\r\n"); sendStr(s, r.c_str());
        return true;
      }
      char out[sizeof(hdr) + 16 + 1200], *p = out;
      memcpy(p, hdr, sizeof(hdr) - 1); p += sizeof(hdr) - 1;
      char num[12];
      u2s(bl, num);
      for (char *n = num; *n; n++) *p++ = *n;
      *p++ = '\r'; *p++ = '\n'; *p++ = '\r'; *p++ = '\n';
      memcpy(p, r.c_str(), bl); p += bl;
      sendAll(s, out, p - out);
      return true;
    }
  }
  if (sp) *sp = ' ';
  if (q) q[-1] = '?';
  return false;
}

static void handleClient(int s) {
  /* idle keep-alive connections are closed after 2 s */
  struct timeval tv = {2, 0};   /* idle keep-alive closed after 2 s (the app talks every 0.2 s) */
  lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  struct timeval stv = {2, 0};  /* a send stuck for 2 s = phone out of reach: give up, free buffers */
  lwip_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
  int one = 1;   /* small replies leave at once (no Nagle) */
  lwip_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  while (true) {
    /* read the whole request header (up to the blank line), keep the first 255 bytes */
    char req[256];
    int len = 0, st = 0;   /* st: matched chars of "\r\n\r\n" */
    bool done = false;
    while (!done) {
      char chunk[128];
      int n = lwip_recv(s, chunk, sizeof(chunk), 0);
      if (n <= 0) return;
      for (int i = 0; i < n; i++) {
        char c = chunk[i];
        if (len < (int)sizeof(req) - 1) req[len++] = c;
        st = (c == '\r') ? (st == 2 ? 3 : 1) : (c == '\n' ? (st == 1 ? 2 : (st == 3 ? 4 : 0)) : 0);
        if (st == 4) { done = true; break; }   /* GET has no body; pipelined bytes are rare */
      }
    }
    req[len] = 0;
    char *e = strchr(req, '\r');
    if (e) *e = 0;
    D200WebCam *w = D200WebCam::_self;
    if (w && w->_log) {
      Serial.print("[HTTP] ");
      Serial.println(req);
    }
    if (handleRoute(s, req)) continue;   /* same connection, next request */
    handleOne(s, req);
    return;
  }
}

static volatile int s_clients = 0;   /* running client tasks */
int D200WebCam::clients() { return s_clients; }
#define D200_MAX_CLIENTS 8   /* lwIP has 10 TCP connections in total */

static void clientTask(void *arg) {
  int s = (int)(intptr_t)arg;
  handleClient(s);
  vTaskDelay(pdMS_TO_TICKS(100));   /* let the last bytes leave */
  taskENTER_CRITICAL();
  s_clients--;
  taskEXIT_CRITICAL();
  lwip_close(s);
  vTaskDelete(NULL);
}

static void serverTask(void *arg) {
  uint16_t port = (uint16_t)(uintptr_t)arg;
  int srv = lwip_socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  lwip_bind(srv, (struct sockaddr *)&addr, sizeof(addr));
  lwip_listen(srv, 4);

  while (true) {
    int c = lwip_accept(srv, NULL, NULL);
    if (c < 0) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    /* own task per request: page, stream and snapshot work at the same time */
    if (s_clients >= D200_MAX_CLIENTS) { lwip_close(c); continue; }   /* protect the heap */
    taskENTER_CRITICAL();
    s_clients++;
    taskEXIT_CRITICAL();
    if (xTaskCreate(clientTask, "d200_cli", 2048, (void *)(intptr_t)c, 10, NULL) != pdPASS) {
      taskENTER_CRITICAL();
      s_clients--;
      taskEXIT_CRITICAL();
      lwip_close(c);
    }
  }
}

bool D200WebCam::begin(uint16_t port) {
  _self = this;
  _port = port;
  return xTaskCreate(serverTask, "d200_http", 2048, (void *)(uintptr_t)port, 10, NULL) == pdPASS;
}
