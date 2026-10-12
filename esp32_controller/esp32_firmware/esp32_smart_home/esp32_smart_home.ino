/*
  ESP32 Smart Home - REST API firmware  v2.0.0
  ------------------------------------------------------------------
  Board    : "ESP32 Dev Module"  (Arduino IDE -> esp32 by Espressif, core 2.x or 3.x)
  Library  : "DHT sensor library" by Adafruit (+ "Adafruit Unified Sensor")
             -> only needed while HAS_DHT = 1
  Talks to : the Flutter app in this project (same Wi-Fi network)

  API (all answers are JSON):
    GET  /api/state                          everything: wifi, sensors, devices
    POST /api/set?id=light&on=1              relay / lock on (1) or off (0)
    POST /api/dnd?on=1                       Do-Not-Disturb (blocks the siren)
    POST /api/scene?id=alloff|goodnight|dinner
    GET  /                                   tiny test page (works in any browser)
  If API_KEY is not empty, every /api request needs the header  X-API-Key: <key>
  (or ?key=<key> in the URL).

  !! MAINS SAFETY: 220 V loads (heater, fan, lights) must go through properly rated
  relays / SSRs in an insulated box with a fuse. Never power a heater from a hobby
  relay board. If unsure, ask an electrician.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#define FW_VERSION "2.0.0"

// ====================== 1) YOUR SETTINGS ======================
const char* WIFI_SSID   = "YOUR_WIFI_NAME";
const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";
const char* DEVICE_NAME = "Smart Home";

// Fixed IP so the app always finds the ESP32 (same IP as in the app Settings).
#define USE_STATIC_IP 1
IPAddress STATIC_IP(192, 168, 1, 37);
IPAddress GATEWAY(192, 168, 1, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress DNS_SERVER(8, 8, 8, 8);

// Optional password for the API. Leave "" to disable. Use the same text in the app.
const char* API_KEY = "";

// ESP32-CAM stream (optional), e.g. "http://192.168.1.40:81/stream". "" = no camera.
const char* CAMERA_URL = "";

// Most 1/2/4/8-channel relay boards switch ON when the pin is LOW.
#define RELAY_ACTIVE_LOW 1

// Temperature / humidity sensor (DHT22 on GPIO 4). Set 0 if you have none.
#define HAS_DHT  1
#define DHT_PIN  4
#define DHT_TYPE DHT22

// ====================== 2) YOUR DEVICES ======================
// One line = one tile in the app. Add / remove lines freely.
//   type : "switch" relay | "lock" (on = locked) | "pir" motion sensor (read-only)
//   group: "Lighting", "Climate", "Security", "Scenes" or any new name
//   icon : light fan heater curtain purifier siren lock pir plug
//   room : 0 = Room 1, 1 = Room 2 ... (you can also move tiles inside the app)
//   watts: typical load, used for the (estimated) energy numbers
//   autoOffMin: safety timer, 0 = never
//   invert: lock only - relay energised = unlocked (normal door strike / solenoid)
struct Device {
  const char* id;
  const char* name;
  const char* type;
  const char* group;
  const char* icon;
  int         room;
  int         pin;
  uint16_t    watts;
  uint16_t    autoOffMin;
  bool        invert;
  // runtime values (leave as they are)
  bool        on;
  float       wh;
  uint32_t    onSince;
};

Device devices[] = {
  // id         name              type      group      icon       room pin watts auto invert on     wh onSince
  {"light",    "Main Light",     "switch", "Lighting", "light",    0,  23,   12,   0, false, false, 0, 0},
  {"fan",      "Ceiling Fan",    "switch", "Lighting", "fan",      0,  22,   60,   0, false, false, 0, 0},
  {"curtain",  "Smart Curtains", "switch", "Climate",  "curtain",  0,  19,    0,   0, false, false, 0, 0},
  {"heater",   "Heater",         "switch", "Climate",  "heater",   0,  21, 1000, 180, false, false, 0, 0},
  {"purifier", "Air Purifier",   "switch", "Climate",  "purifier", 0,  18,   30,   0, false, false, 0, 0},
  {"siren",    "Alarm Siren",    "switch", "Security", "siren",    0,  17,    5,   5, false, false, 0, 0},
  {"lock",     "Smart Lock",     "lock",   "Security", "lock",     0,  16,    0,   0, true,  true,  0, 0},
  {"pir",      "PIR Sensor",     "pir",    "Security", "pir",      0,  27,    0,   0, false, false, 0, 0},
};
const size_t N_DEVICES = sizeof(devices) / sizeof(devices[0]);

// ====================== 3) CODE (no need to edit) ======================
#if HAS_DHT
#include <DHT.h>
DHT dht(DHT_PIN, DHT_TYPE);
#endif

WebServer server(80);

bool     dnd      = false;
float    tempC    = NAN;
float    humidity = NAN;
uint32_t lastTick = 0, lastDht = 0, lastWifiTry = 0;

// ---------------------------------------------------------------- relays
inline void relayWrite(int pin, bool energised) {
#if RELAY_ACTIVE_LOW
  digitalWrite(pin, energised ? LOW : HIGH);
#else
  digitalWrite(pin, energised ? HIGH : LOW);
#endif
}

void applyDevice(Device& d) {
  if (!strcmp(d.type, "pir")) return;
  bool energised = d.invert ? !d.on : d.on;
  relayWrite(d.pin, energised);
}

void setDevice(Device& d, bool on) {
  if (d.on == on) return;
  d.on = on;
  if (on) d.onSince = millis();
  applyDevice(d);
}

Device* findDevice(const String& id) {
  for (size_t i = 0; i < N_DEVICES; i++)
    if (id == devices[i].id) return &devices[i];
  return nullptr;
}

void setById(const char* id, bool on) {
  Device* d = findDevice(id);
  if (d) setDevice(*d, on);
}

// ---------------------------------------------------------------- JSON
String esc(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { /* drop control characters */ }
    else o += c;               // UTF-8 (e.g. Hindi Wi-Fi names) passes through
  }
  return o;
}

String numOrNull(float v, int decimals) {
  return isnan(v) ? String("null") : String(v, decimals);
}

String stateJson() {
  float totalW = 0;
  for (size_t i = 0; i < N_DEVICES; i++)
    if (devices[i].on && !strcmp(devices[i].type, "switch")) totalW += devices[i].watts;

  String j;
  j.reserve(1600);
  j += "{\"name\":\"";   j += esc(DEVICE_NAME);
  j += "\",\"fw\":\"";   j += FW_VERSION;
  j += "\",\"ssid\":\""; j += esc(WiFi.SSID());
  j += "\",\"ip\":\"";   j += WiFi.localIP().toString();
  j += "\",\"rssi\":";   j += String(WiFi.RSSI());
  j += ",\"uptime\":";   j += String((unsigned long)(millis() / 1000UL));
  j += ",\"temp\":";     j += numOrNull(tempC, 1);
  j += ",\"hum\":";      j += numOrNull(humidity, 0);
  j += ",\"dnd\":";      j += dnd ? "true" : "false";
  j += ",\"camera\":\""; j += esc(CAMERA_URL);
  j += "\",\"powerW\":"; j += String(totalW, 1);
  j += ",\"devices\":[";
  for (size_t i = 0; i < N_DEVICES; i++) {
    const Device& d = devices[i];
    if (i) j += ',';
    j += "{\"id\":\"";     j += d.id;
    j += "\",\"name\":\""; j += esc(d.name);
    j += "\",\"type\":\""; j += d.type;
    j += "\",\"group\":\""; j += esc(d.group);
    j += "\",\"icon\":\""; j += d.icon;
    j += "\",\"room\":";   j += String(d.room);
    j += ",\"on\":";       j += d.on ? "true" : "false";
    j += ",\"watts\":";    j += String((unsigned int)d.watts);
    j += ",\"wh\":";       j += String(d.wh, 2);
    j += "}";
  }
  j += "]}";
  return j;
}

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

void sendError(int code, const char* msg) {
  sendJson(code, String("{\"ok\":false,\"error\":\"") + msg + "\"}");
}

bool authorized() {
  if (API_KEY[0] == '\0') return true;
  String k = server.header("X-API-Key");
  if (k.length() == 0) k = server.arg("key");
  return k == API_KEY;
}

#define REQUIRE_AUTH() if (!authorized()) { sendError(401, "unauthorized"); return; }

bool wantOn() {
  String v = server.arg("on");
  return v == "1" || v == "true" || v == "on";
}

// ---------------------------------------------------------------- handlers
void handleState() {
  REQUIRE_AUTH();
  sendJson(200, stateJson());
}

void handleSet() {
  REQUIRE_AUTH();
  if (!server.hasArg("id") || !server.hasArg("on")) { sendError(400, "need id and on=0|1"); return; }
  Device* d = findDevice(server.arg("id"));
  if (!d || !strcmp(d->type, "pir")) { sendError(404, "unknown device"); return; }
  bool on = wantOn();
  if (dnd && on && !strcmp(d->icon, "siren")) { sendError(409, "DND mode is on"); return; }
  setDevice(*d, on);
  sendJson(200, "{\"ok\":true}");
}

void handleDnd() {
  REQUIRE_AUTH();
  if (!server.hasArg("on")) { sendError(400, "need on=0|1"); return; }
  dnd = wantOn();
  if (dnd) setById("siren", false);
  sendJson(200, "{\"ok\":true}");
}

void handleScene() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  if (id == "alloff") {
    for (size_t i = 0; i < N_DEVICES; i++)
      if (!strcmp(devices[i].type, "switch")) setDevice(devices[i], false);
  } else if (id == "goodnight") {
    for (size_t i = 0; i < N_DEVICES; i++)
      if (!strcmp(devices[i].type, "switch")) setDevice(devices[i], false);
    setById("lock", true);                       // locked
  } else if (id == "dinner") {
    setById("light", true);
    setById("fan", false);
    setById("heater", false);
  } else {
    sendError(404, "unknown scene");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32 Smart Home</title>
<style>body{font-family:sans-serif;background:#0b1220;color:#e5e9f0;margin:0;padding:16px}
h2{margin:0 0 4px}small{color:#8b95a7}.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:10px;margin-top:14px}
button{padding:16px 8px;border-radius:14px;border:1px solid #2a3550;background:#141d30;color:#e5e9f0;font-size:15px}
button.on{background:#2f7bff;border-color:#2f7bff}</style></head><body>
<h2 id="t">ESP32 Smart Home</h2><small id="s">connecting...</small><div class="g" id="g"></div>
<script>
const key=new URLSearchParams(location.search).get('key')||'';
const H=key?{'X-API-Key':key}:{};
async function load(){try{const r=await fetch('/api/state',{headers:H});const j=await r.json();
document.getElementById('t').textContent=j.name;
document.getElementById('s').textContent=j.ssid+' | '+j.ip+' | '+(j.temp==null?'--':j.temp+' C')+' | '+j.powerW+' W';
const g=document.getElementById('g');g.innerHTML='';
j.devices.forEach(d=>{const b=document.createElement('button');b.textContent=d.name+(d.on?' - ON':' - OFF');
if(d.on)b.className='on';if(d.type!='pir')b.onclick=async()=>{await fetch('/api/set?id='+d.id+'&on='+(d.on?0:1),{method:'POST',headers:H});load()};
g.appendChild(b)})}catch(e){document.getElementById('s').textContent='offline'}}
load();setInterval(load,2000);
</script></body></html>)HTML";

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }
void handleNotFound() { sendError(404, "not found"); }

// ---------------------------------------------------------------- wifi
void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
#if USE_STATIC_IP
  WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS_SERVER);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi not connected yet - will keep retrying.");
  }
}

// ---------------------------------------------------------------- background work
void tick() {
  uint32_t now = millis();

  // PIR is read on every pass so short pulses are not missed.
  for (size_t i = 0; i < N_DEVICES; i++)
    if (!strcmp(devices[i].type, "pir")) devices[i].on = digitalRead(devices[i].pin) == HIGH;

  // Once per second: energy estimate + safety auto-off timers.
  if (now - lastTick >= 1000) {
    float dt = (now - lastTick) / 1000.0f;
    lastTick = now;
    for (size_t i = 0; i < N_DEVICES; i++) {
      Device& d = devices[i];
      if (strcmp(d.type, "switch") || !d.on) continue;
      d.wh += d.watts * dt / 3600.0f;
      if (d.autoOffMin && (now - d.onSince) > (uint32_t)d.autoOffMin * 60000UL) setDevice(d, false);
    }
  }

#if HAS_DHT
  if (now - lastDht >= 3000) {            // DHT22 needs >= 2 s between reads
    lastDht = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) tempC = t;
    if (!isnan(h)) humidity = h;
  }
#endif

  if (WiFi.status() != WL_CONNECTED && now - lastWifiTry > 10000) {
    lastWifiTry = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  for (size_t i = 0; i < N_DEVICES; i++) {
    Device& d = devices[i];
    if (!strcmp(d.type, "pir")) { pinMode(d.pin, INPUT); continue; }
    applyDevice(d);                 // write the safe level first ...
    pinMode(d.pin, OUTPUT);         // ... then drive the pin
    applyDevice(d);
  }

#if HAS_DHT
  dht.begin();
#endif

  connectWifi();
  if (MDNS.begin("esp32-home")) MDNS.addService("http", "tcp", 80);   // http://esp32-home.local

  const char* wanted[] = {"X-API-Key"};
  server.collectHeaders(wanted, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_ANY, handleState);
  server.on("/api/set",   HTTP_ANY, handleSet);
  server.on("/api/dnd",   HTTP_ANY, handleDnd);
  server.on("/api/scene", HTTP_ANY, handleScene);
  server.onNotFound(handleNotFound);
  server.begin();
  lastTick = millis();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  tick();
  delay(2);
}
