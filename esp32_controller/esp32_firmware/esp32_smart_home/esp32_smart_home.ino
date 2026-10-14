/*
  ESP32 Smart Home - REST API firmware  v4.0.0
  ------------------------------------------------------------------
  Board    : "ESP32 Dev Module"  (Arduino IDE -> esp32 by Espressif, core 2.x or 3.x)
  Library  : "DHT sensor library" by Adafruit (+ "Adafruit Unified Sensor")
             -> only needed while HAS_DHT = 1
  Talks to : the Flutter app in this project (same Wi-Fi network)

  NEW in v4.0.0  -  customise the whole smart home (everything through the API)
    1) Rooms:        add / rename / delete / re-order rooms (saved in flash).
    2) Names:        rename any device (Main Light, Ceiling Fan ...) with /api/device/update.
    3) Look & feel:  app colour, theme (light / dark / system) and hub name - /api/ui.
    4) Button order: /api/device/move, /api/device/order, /api/scene/move, /api/room/move.
    5) Scenes:       create your own scenes (any devices on / off) next to the built-in three.
    6) Timers:       countdown timers  "switch the heater off in 30 minutes".
    7) Automations:  clock (time + days), motion (PIR), temperature or humidity -> device / scene.
    8) Everything above is saved in flash and is still there after a restart.
       (Only countdown timers live in RAM - a restart cancels them.)

  NEW in v3.0.0
    1) Wi-Fi from the app: scan networks, enter password, see status / IP, reset.
       The Wi-Fi is saved in flash. If the ESP32 cannot join any network it opens its own
       hotspot  "SmartHome-Setup"  (password = AP_PASS, address http://192.168.4.1/)
       so you can always set it up again from the app.
    2) Devices are no longer a fixed table. Add / edit / delete bulbs, fans and other
       devices from the app (name, room, icon, GPIO pin). The list is SAVED in flash and
       survives restarts. DEFAULT_DEVICES[] below is only used the very first time.

  API (all answers are JSON):
    GET  /api/state                          everything: wifi, sensors, devices, free pins
    POST /api/set?id=light&on=1              relay / lock on (1) or off (0)
    POST /api/dnd?on=1                       Do-Not-Disturb (blocks the siren)
    POST /api/scene?id=alloff|goodnight|dinner

    POST /api/device/add?name=..&icon=light&pin=23&room=0[&watts=12]
    POST /api/device/update?id=..[&name=..][&icon=..][&pin=..][&room=..][&watts=..]
    POST /api/device/delete?id=..

    GET  /api/wifi/status                    state of the Wi-Fi connection / setup hotspot
    GET  /api/wifi/scan[?rescan=1]           list of nearby networks (scan runs in background)
    POST /api/wifi/connect?ssid=..&pass=..   join a new network (old one is restored if it fails)
    POST /api/wifi/reset                     forget saved Wi-Fi, restart in setup (hotspot) mode

    --- v4 ---------------------------------------------------------------------------------
    GET  /api/room/list                      POST /api/room/add?name=Kitchen
    POST /api/room/rename?id=1&name=Hall     POST /api/room/delete?id=1[&move=0]
    POST /api/room/move?id=1&to=0

    POST /api/device/move?id=fan&to=0        POST /api/device/order?ids=fan,light,heater

    GET|POST /api/ui[?theme=dark|light|system][&accent=2F7BFF][&name=My Home]

    GET  /api/scene/list                     POST /api/scene?id=alloff|goodnight|dinner|s1   (run)
    POST /api/scene/add?name=Movie&actions=light:0,fan:1,lock:1
    POST /api/scene/update?id=s1[&name=..][&actions=..]
    POST /api/scene/delete?id=s1             POST /api/scene/move?id=s1&to=0

    GET  /api/timer/list
    POST /api/timer/add?after=30&device=heater&on=0      (after = minutes, or sec=90)
    POST /api/timer/add?after=10&scene=goodnight
    POST /api/timer/cancel?id=t1             POST /api/timer/cancel?all=1

    GET  /api/automation/list
    POST /api/automation/add?name=..&trigger=time&at=22:30&days=all&scene=goodnight
    POST /api/automation/add?trigger=motion&sensor=pir&device=light&on=1&revert=5
    POST /api/automation/add?trigger=temp&above=30&device=fan&on=1
    POST /api/automation/add?trigger=hum&below=40&device=purifier&on=1
    POST /api/automation/update?id=a1[&enabled=0|1][...same fields as add...]
    POST /api/automation/delete?id=a1        POST /api/automation/run?id=a1   (test it now)

    GET|POST /api/time[?tz=IST-5:30][&epoch=1760000000]   clock status (set from the internet)
    POST /api/restart                        restart the ESP32 (saved settings stay)
    -----------------------------------------------------------------------------------------

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
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

#define FW_VERSION "4.0.0"

#ifndef WIFI_SCAN_RUNNING
#define WIFI_SCAN_RUNNING (-1)
#endif
#ifndef WIFI_SCAN_FAILED
#define WIFI_SCAN_FAILED  (-2)
#endif

// ====================== 1) YOUR SETTINGS ======================
const char* DEVICE_NAME = "Smart Home";

// OPTIONAL first-time Wi-Fi. Once you set a Wi-Fi from the app, the saved one is used
// instead. Leave the placeholder text if you want to set everything from the app.
const char* WIFI_SSID   = "YOUR_WIFI_NAME";
const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";

// Fixed IP - used ONLY for the first-time Wi-Fi above (same IP as in the app Settings).
// A Wi-Fi chosen from the app always uses DHCP (a new router has another IP range).
#define USE_STATIC_IP 1
IPAddress STATIC_IP(192, 168, 1, 37);
IPAddress GATEWAY(192, 168, 1, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress DNS_SERVER(8, 8, 8, 8);

// Setup hotspot (opens when no Wi-Fi works). Password must be 8+ characters.
const char* AP_SSID = "SmartHome-Setup";
const char* AP_PASS = "12345678";

// Optional password for the API. Leave "" to disable. Use the same text in the app.
const char* API_KEY = "";

// ESP32-CAM stream (optional), e.g. "http://192.168.1.40:81/stream". "" = no camera.
const char* CAMERA_URL = "";

// Time zone for clock-based automations (POSIX text). India = "IST-5:30".
// Others: "UTC0", "GST-4" (Dubai), "PKT-5" (Pakistan), "CET-1CEST,M3.5.0,M10.5.0/3" (Europe).
// The clock is set from the internet (NTP). Can also be changed with /api/time?tz=...
#define TZ_DEFAULT "IST-5:30"

// Most 1/2/4/8-channel relay boards switch ON when the pin is LOW.
#define RELAY_ACTIVE_LOW 1

// Temperature / humidity sensor (DHT22 on GPIO 4). Set 0 if you have none.
// While HAS_DHT = 1 this GPIO is not offered to devices.
#define HAS_DHT  1
#define DHT_PIN  4
#define DHT_TYPE DHT22

// ====================== 2) DEVICES ======================
// type : "switch" relay | "lock" (on = locked) | "pir" motion sensor (read-only)
// group: "Lighting", "Climate", "Security", "Other" ...
// icon : light fan heater curtain purifier siren lock pir plug
// room : 0 = Room 1, 1 = Room 2 ... (the app can also move tiles between rooms)
// These are only the STARTING devices (first boot). After that the list lives in flash
// and is edited from the app.
struct DefaultDevice {
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
};

const DefaultDevice DEFAULT_DEVICES[] = {
  // id         name              type      group      icon       room pin watts auto invert
  {"light",    "Main Light",     "switch", "Lighting", "light",    0,  23,   12,   0, false},
  {"fan",      "Ceiling Fan",    "switch", "Lighting", "fan",      0,  22,   60,   0, false},
  {"curtain",  "Smart Curtains", "switch", "Climate",  "curtain",  0,  19,    0,   0, false},
  {"heater",   "Heater",         "switch", "Climate",  "heater",   0,  21, 1000, 180, false},
  {"purifier", "Air Purifier",   "switch", "Climate",  "purifier", 0,  18,   30,   0, false},
  {"siren",    "Alarm Siren",    "switch", "Security", "siren",    0,  17,    5,   5, false},
  {"lock",     "Smart Lock",     "lock",   "Security", "lock",     0,  16,    0,   0, true},
  {"pir",      "PIR Sensor",     "pir",    "Security", "pir",      0,  27,    0,   0, false},
};
const size_t N_DEFAULT_DEVICES = sizeof(DEFAULT_DEVICES) / sizeof(DEFAULT_DEVICES[0]);

#define MAX_DEVICES 24

struct Device {
  char     id[12];
  char     name[48];
  char     type[8];
  char     group[16];
  char     icon[12];
  int      room;
  int      pin;
  uint16_t watts;
  uint16_t autoOffMin;
  bool     invert;
  // runtime values (not saved)
  bool     on;
  float    wh;
  uint32_t onSince;
};

enum ConnState { CS_IDLE, CS_PENDING, CS_TRYING };

// GPIOs that are safe for relays on a normal ESP32 Dev board. Not offered on purpose:
// 0 2 12 15 (boot pins), 1 3 (USB serial), 6-11 (flash).
const uint8_t OUT_PINS[]     = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
const uint8_t IN_ONLY_PINS[] = {34, 35, 36, 39};   // input only: fine for a PIR sensor
const size_t  N_OUT_PINS     = sizeof(OUT_PINS) / sizeof(OUT_PINS[0]);
const size_t  N_IN_PINS      = sizeof(IN_ONLY_PINS) / sizeof(IN_ONLY_PINS[0]);

const char* ICONS[] = {"light", "fan", "heater", "curtain", "purifier", "siren", "lock", "pir", "plug"};
const size_t N_ICONS = sizeof(ICONS) / sizeof(ICONS[0]);

// ====================== 2b) ROOMS / SCENES / TIMERS / AUTOMATIONS ======================
#define MAX_ROOMS         20
#define MAX_SCENES        10
#define MAX_SCENE_ACTIONS 12
#define MAX_RULES         16
#define MAX_COUNTDOWNS    12
#define RULE_COOLDOWN_MS  10000UL     // an automation does not fire twice within 10 seconds

struct RoomEntry {
  uint8_t id;                 // 0..49 - the number a device keeps in its "room" field
  char    name[32];
};

struct SceneAction {
  char dev[12];
  bool on;                    // for a lock: true = locked
};

struct SceneDef {
  char        id[8];          // "s1", "s2" ...
  char        name[32];
  uint8_t     n;
  SceneAction act[MAX_SCENE_ACTIONS];
};

enum TrigKind : uint8_t { TR_TIME = 0, TR_MOTION = 1, TR_TEMP = 2, TR_HUM = 3 };

struct AutoRule {
  char     id[8];             // "a1", "a2" ...
  char     name[32];
  bool     enabled;
  uint8_t  trig;              // TrigKind
  uint16_t atMin;             // time: minutes after midnight
  uint8_t  days;              // time: bit0 = Sunday ... bit6 = Saturday
  char     sensor[12];        // motion: id of the PIR device
  bool     above;             // temp / hum: true = above, false = below
  bool     hasLevel;
  float    level;
  char     target[12];        // device id, or scene id when isScene
  bool     isScene;
  bool     on;
  uint16_t revertMin;         // device only: switch back after N minutes (0 = never)
  // runtime values (not saved)
  bool     armed;             // temp / hum: may fire again
  bool     lastMotion;
  bool     everFired;
  uint32_t lastMinute;        // time: epoch minute it last fired in
  uint32_t lastFire;
};

struct Countdown {
  bool     used;
  char     id[8];             // "t1", "t2" ...
  char     target[12];
  bool     isScene;
  bool     on;
  uint32_t dueAt;
  char     rule[8];           // automation that made it ("" = made by the user)
};

// ====================== 3) CODE (no need to edit) ======================
#if HAS_DHT
#include <DHT.h>
DHT dht(DHT_PIN, DHT_TYPE);
#endif

#define PREF_NS "smarthome"

WebServer   server(80);
Preferences prefs;

Device   devices[MAX_DEVICES];
size_t   nDevices = 0;
uint32_t nextId   = 1;                       // for generated ids d1, d2, ...

// ---- rooms, scenes, automations, look & feel (saved in flash)
RoomEntry rooms[MAX_ROOMS];     size_t nRooms  = 0;
SceneDef  scenes[MAX_SCENES];   size_t nScenes = 0;
AutoRule  rules[MAX_RULES];     size_t nRules  = 0;
Countdown countdowns[MAX_COUNTDOWNS];            // RAM only
uint32_t  countdownSeq = 1;
char      hubName[40];
char      uiTheme[8]   = "dark";                 // system | light | dark
uint32_t  uiAccent     = 0x2F7BFF;
String    tzString;

// defined further down
void   ensureRoom(int id);
void   purgeDeviceRefs(const char* id);
String stateExtras();

bool     dnd      = false;
float    tempC    = NAN;
float    humidity = NAN;
uint32_t lastTick = 0, lastDht = 0, lastWifiTry = 0;

// ---- Wi-Fi bookkeeping
String    savedSsid, savedPass;              // Wi-Fi stored in flash
bool      setupFlag   = false;               // true after "reset Wi-Fi": go straight to setup mode
String    curSsid, curPass;                  // Wi-Fi we are using right now (to go back to)
bool      curStatic   = false;               // curSsid is the built-in Wi-Fi with a fixed IP
bool      apActive    = false;               // setup hotspot is on
ConnState connState   = CS_IDLE;
String    pendSsid, pendPass;                // new Wi-Fi requested by the app
uint32_t  pendAt = 0, tryStart = 0;
String    lastWifiError;                     // "" = none
bool      lastUp = false;
uint32_t  lastChange = 0;
bool      restartPending = false;
uint32_t  restartAt = 0;

// ---------------------------------------------------------------- small helpers
// Copy text into a fixed buffer, never cutting a UTF-8 (e.g. Hindi) letter in half.
void copyStr(char* dst, size_t cap, const char* src) {
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (((uint8_t)src[n]) & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// Names are saved as "a|b|c" lines, so these characters must not appear in them.
String cleanName(String s) {
  String o;
  o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '|' || c == '"' || c == '\\' || (uint8_t)c < 0x20) continue;
    o += c;
  }
  o.trim();
  return o;
}

bool validIcon(const String& s) {
  for (size_t i = 0; i < N_ICONS; i++) if (s == ICONS[i]) return true;
  return false;
}

const char* typeForIcon(const char* icon) {
  if (!strcmp(icon, "lock")) return "lock";
  if (!strcmp(icon, "pir"))  return "pir";
  return "switch";
}

const char* groupForIcon(const char* icon) {
  if (!strcmp(icon, "light")) return "Lighting";
  if (!strcmp(icon, "fan") || !strcmp(icon, "heater") || !strcmp(icon, "curtain") ||
      !strcmp(icon, "purifier")) return "Climate";
  if (!strcmp(icon, "siren") || !strcmp(icon, "lock") || !strcmp(icon, "pir")) return "Security";
  return "Other";
}

uint16_t defaultWatts(const char* icon) {
  if (!strcmp(icon, "light"))    return 12;
  if (!strcmp(icon, "fan"))      return 60;
  if (!strcmp(icon, "heater"))   return 1000;
  if (!strcmp(icon, "purifier")) return 30;
  if (!strcmp(icon, "siren"))    return 5;
  return 0;
}

uint16_t defaultAutoOff(const char* icon) {          // safety timers, minutes
  if (!strcmp(icon, "heater")) return 180;
  if (!strcmp(icon, "siren"))  return 5;
  return 0;
}

bool inList(const uint8_t* list, size_t n, int pin) {
  for (size_t i = 0; i < n; i++) if (list[i] == pin) return true;
  return false;
}

bool pinReserved(int pin) {
#if HAS_DHT
  if (pin == DHT_PIN) return true;
#endif
  return false;
}

// Output devices need an output-capable pin; a PIR may also use the input-only pins.
bool pinAllowed(const char* type, int pin) {
  if (pinReserved(pin)) return false;
  if (inList(OUT_PINS, N_OUT_PINS, pin)) return true;
  if (!strcmp(type, "pir") && inList(IN_ONLY_PINS, N_IN_PINS, pin)) return true;
  return false;
}

bool pinUsed(int pin, const Device* except) {
  for (size_t i = 0; i < nDevices; i++)
    if (&devices[i] != except && devices[i].pin == pin) return true;
  return false;
}

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

void initPin(Device& d) {
  if (!strcmp(d.type, "pir")) { pinMode(d.pin, INPUT); return; }
  applyDevice(d);                 // write the safe level first ...
  pinMode(d.pin, OUTPUT);         // ... then drive the pin
  applyDevice(d);
}

// De-energise the relay before a pin is given up (device deleted / moved to another pin).
void releasePin(Device& d) {
  if (!strcmp(d.type, "pir")) return;
  relayWrite(d.pin, false);
}

int indexOfDevice(const char* id) {
  for (size_t i = 0; i < nDevices; i++)
    if (!strcmp(id, devices[i].id)) return (int)i;
  return -1;
}

Device* findDevice(const char* id) {
  int i = indexOfDevice(id);
  return i < 0 ? nullptr : &devices[i];
}

// ---------------------------------------------------------------- saved device list
String devicesToText() {
  String t = "v1\n";
  for (size_t i = 0; i < nDevices; i++) {
    const Device& d = devices[i];
    t += d.id;      t += '|';
    t += d.name;    t += '|';
    t += d.type;    t += '|';
    t += d.group;   t += '|';
    t += d.icon;    t += '|';
    t += String(d.room);       t += '|';
    t += String(d.pin);        t += '|';
    t += String((unsigned int)d.watts);      t += '|';
    t += String((unsigned int)d.autoOffMin); t += '|';
    t += d.invert ? '1' : '0';
    t += '\n';
  }
  return t;
}

void saveDevices() {
  prefs.begin(PREF_NS, false);
  size_t n = prefs.putString("devs", devicesToText());
  prefs.putUInt("nextId", nextId);
  prefs.end();
  if (n == 0) Serial.println("WARNING: could not save the device list");
}

bool validId(const String& s) {
  if (s.length() == 0 || s.length() > 11) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

// Returns true when a saved list exists (even an empty one - the user may have deleted all).
bool loadDevices() {
  prefs.begin(PREF_NS, false);
  String t = prefs.getString("devs", "");
  nextId = prefs.getUInt("nextId", 1);
  prefs.end();
  if (!t.startsWith("v1\n")) return false;

  nDevices = 0;
  int pos = 3;
  while (pos < (int)t.length() && nDevices < MAX_DEVICES) {
    int e = t.indexOf('\n', pos);
    if (e < 0) e = t.length();
    String line = t.substring(pos, e);
    pos = e + 1;
    if (line.length() == 0) continue;

    String f[10];
    int n = 0, s = 0;
    while (n < 10) {
      int p = line.indexOf('|', s);
      if (p < 0) { f[n++] = line.substring(s); break; }
      f[n++] = line.substring(s, p);
      s = p + 1;
    }
    if (n < 10 || !validId(f[0])) continue;
    if (f[2] != "switch" && f[2] != "lock" && f[2] != "pir") continue;
    int pin = f[6].toInt();
    if (!pinAllowed(f[2].c_str(), pin) || pinUsed(pin, nullptr) || indexOfDevice(f[0].c_str()) >= 0) {
      Serial.print("Skipping saved device with a bad / duplicate pin: ");
      Serial.println(f[0]);
      continue;
    }

    Device& d = devices[nDevices++];
    memset(&d, 0, sizeof(d));
    copyStr(d.id,    sizeof(d.id),    f[0].c_str());
    copyStr(d.name,  sizeof(d.name),  f[1].length() ? f[1].c_str() : f[0].c_str());
    copyStr(d.type,  sizeof(d.type),  f[2].c_str());
    copyStr(d.group, sizeof(d.group), f[3].length() ? f[3].c_str() : "Other");
    copyStr(d.icon,  sizeof(d.icon),  validIcon(f[4]) ? f[4].c_str() : "plug");
    d.room       = constrain(f[5].toInt(), 0, 49);
    d.pin        = pin;
    d.watts      = (uint16_t)constrain(f[7].toInt(), 0, 5000);
    d.autoOffMin = (uint16_t)constrain(f[8].toInt(), 0, 1440);
    d.invert     = f[9] == "1";
    d.on         = !strcmp(d.type, "lock");      // a lock starts locked
  }
  return true;
}

void seedDevices() {
  nDevices = 0;
  for (size_t i = 0; i < N_DEFAULT_DEVICES && nDevices < MAX_DEVICES; i++) {
    const DefaultDevice& s = DEFAULT_DEVICES[i];
    Device& d = devices[nDevices++];
    memset(&d, 0, sizeof(d));
    copyStr(d.id,    sizeof(d.id),    s.id);
    copyStr(d.name,  sizeof(d.name),  s.name);
    copyStr(d.type,  sizeof(d.type),  s.type);
    copyStr(d.group, sizeof(d.group), s.group);
    copyStr(d.icon,  sizeof(d.icon),  s.icon);
    d.room       = s.room;
    d.pin        = s.pin;
    d.watts      = s.watts;
    d.autoOffMin = s.autoOffMin;
    d.invert     = s.invert;
    d.on         = !strcmp(s.type, "lock");
  }
  saveDevices();
}

void makeId(char* out, size_t cap) {
  for (int guard = 0; guard < 1000; guard++) {
    snprintf(out, cap, "d%lu", (unsigned long)nextId++);
    if (!findDevice(out)) return;
  }
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

const char* jsonBool(bool b) { return b ? "true" : "false"; }

bool staUp() { return WiFi.status() == WL_CONNECTED; }

String ipString() {
  if (staUp()) return WiFi.localIP().toString();
  if (apActive) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

const char* wifiMode() {
  bool up = staUp();
  if (up && apActive) return "ap+sta";
  if (up)             return "sta";
  if (apActive)       return "ap";
  return "offline";
}

String stateJson() {
  float totalW = 0;
  for (size_t i = 0; i < nDevices; i++)
    if (devices[i].on && !strcmp(devices[i].type, "switch")) totalW += devices[i].watts;

  String j;
  j.reserve(1000 + nDevices * 210);
  j += "{\"name\":\"";   j += esc(hubName);
  j += "\",\"fw\":\"";   j += FW_VERSION;
  j += "\",\"ssid\":\""; j += esc(staUp() ? WiFi.SSID() : String(""));
  j += "\",\"ip\":\"";   j += ipString();
  j += "\",\"rssi\":";   j += String(staUp() ? WiFi.RSSI() : 0);
  j += ",\"mode\":\"";   j += wifiMode();
  j += "\",\"apSsid\":\""; j += apActive ? esc(AP_SSID) : String("");
  j += "\",\"uptime\":"; j += String((unsigned long)(millis() / 1000UL));
  j += ",\"temp\":";     j += numOrNull(tempC, 1);
  j += ",\"hum\":";      j += numOrNull(humidity, 0);
  j += ",\"dnd\":";      j += jsonBool(dnd);
  j += ",\"camera\":\""; j += esc(CAMERA_URL);
  j += "\",\"powerW\":"; j += String(totalW, 1);
  j += ",\"maxDevices\":"; j += String((unsigned int)MAX_DEVICES);

  j += ",\"pinsOut\":[";
  bool first = true;
  for (size_t i = 0; i < N_OUT_PINS; i++) {
    if (pinReserved(OUT_PINS[i])) continue;
    if (!first) j += ',';
    first = false;
    j += String((unsigned int)OUT_PINS[i]);
  }
  j += "],\"pinsIn\":[";
  for (size_t i = 0; i < N_IN_PINS; i++) {
    if (i) j += ',';
    j += String((unsigned int)IN_ONLY_PINS[i]);
  }

  j += "]";
  j += stateExtras();
  j += ",\"devices\":[";
  for (size_t i = 0; i < nDevices; i++) {
    const Device& d = devices[i];
    if (i) j += ',';
    j += "{\"id\":\"";     j += d.id;
    j += "\",\"name\":\""; j += esc(d.name);
    j += "\",\"type\":\""; j += d.type;
    j += "\",\"group\":\""; j += esc(d.group);
    j += "\",\"icon\":\""; j += d.icon;
    j += "\",\"room\":";   j += String(d.room);
    j += ",\"pin\":";      j += String(d.pin);
    j += ",\"on\":";       j += jsonBool(d.on);
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

// ---------------------------------------------------------------- device handlers
void handleState() {
  REQUIRE_AUTH();
  sendJson(200, stateJson());
}

void handleSet() {
  REQUIRE_AUTH();
  if (!server.hasArg("id") || !server.hasArg("on")) { sendError(400, "need id and on=0|1"); return; }
  String id = server.arg("id");
  Device* d = findDevice(id.c_str());
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
  if (dnd) {
    for (size_t i = 0; i < nDevices; i++)
      if (!strcmp(devices[i].icon, "siren")) setDevice(devices[i], false);
  }
  sendJson(200, "{\"ok\":true}");
}

void allSwitchesOff() {
  for (size_t i = 0; i < nDevices; i++)
    if (!strcmp(devices[i].type, "switch")) setDevice(devices[i], false);
}

void handleDeviceAdd() {
  REQUIRE_AUTH();
  if (nDevices >= MAX_DEVICES) { sendError(409, "device limit reached"); return; }

  String name = cleanName(server.arg("name"));
  if (name.length() == 0) { sendError(400, "name required"); return; }
  String icon = server.hasArg("icon") ? server.arg("icon") : String("plug");
  if (!validIcon(icon)) { sendError(400, "unknown icon"); return; }
  if (!server.hasArg("pin")) { sendError(400, "pin required"); return; }

  int pin = server.arg("pin").toInt();
  const char* type = typeForIcon(icon.c_str());
  if (!pinAllowed(type, pin)) { sendError(400, "this GPIO pin cannot be used"); return; }
  if (pinUsed(pin, nullptr))  { sendError(409, "this GPIO pin is already used"); return; }

  int room = constrain(server.arg("room").toInt(), 0, 49);
  int watts = server.hasArg("watts") ? constrain(server.arg("watts").toInt(), 0, 5000)
                                     : (int)defaultWatts(icon.c_str());

  Device& d = devices[nDevices];
  memset(&d, 0, sizeof(d));
  makeId(d.id, sizeof(d.id));
  copyStr(d.name,  sizeof(d.name),  name.c_str());
  copyStr(d.type,  sizeof(d.type),  type);
  copyStr(d.group, sizeof(d.group), groupForIcon(icon.c_str()));
  copyStr(d.icon,  sizeof(d.icon),  icon.c_str());
  d.room       = room;
  d.pin        = pin;
  d.watts      = (uint16_t)watts;
  d.autoOffMin = defaultAutoOff(icon.c_str());
  d.invert     = !strcmp(type, "lock");     // door strike: energised = unlocked
  d.on         = !strcmp(type, "lock");     // a lock starts locked
  nDevices++;
  ensureRoom(room);

  initPin(d);
  saveDevices();
  sendJson(200, String("{\"ok\":true,\"id\":\"") + d.id + "\"}");
}

void handleDeviceUpdate() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  Device* d = findDevice(id.c_str());
  if (!d) { sendError(404, "unknown device"); return; }

  // Check everything first, change things afterwards - a bad request changes nothing.
  String name = d->name;
  if (server.hasArg("name")) {
    name = cleanName(server.arg("name"));
    if (name.length() == 0) { sendError(400, "name required"); return; }
  }
  String icon = d->icon;
  if (server.hasArg("icon")) {
    icon = server.arg("icon");
    if (!validIcon(icon)) { sendError(400, "unknown icon"); return; }
    if (strcmp(typeForIcon(icon.c_str()), d->type) != 0) { sendError(400, "this icon does not fit the device type"); return; }
  }
  int pin = d->pin;
  if (server.hasArg("pin")) {
    pin = server.arg("pin").toInt();
    if (!pinAllowed(d->type, pin)) { sendError(400, "this GPIO pin cannot be used"); return; }
    if (pinUsed(pin, d))           { sendError(409, "this GPIO pin is already used"); return; }
  }
  int room  = server.hasArg("room")  ? constrain(server.arg("room").toInt(), 0, 49)   : d->room;
  int watts = server.hasArg("watts") ? constrain(server.arg("watts").toInt(), 0, 5000) : (int)d->watts;

  copyStr(d->name, sizeof(d->name), name.c_str());
  if (icon != d->icon) {
    copyStr(d->icon,  sizeof(d->icon),  icon.c_str());
    copyStr(d->group, sizeof(d->group), groupForIcon(icon.c_str()));
    d->autoOffMin = defaultAutoOff(icon.c_str());
  }
  d->room  = room;
  ensureRoom(room);
  d->watts = (uint16_t)watts;
  if (pin != d->pin) {
    releasePin(*d);
    d->pin = pin;
    initPin(*d);
  }
  saveDevices();
  sendJson(200, "{\"ok\":true}");
}

void handleDeviceDelete() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  int idx = indexOfDevice(id.c_str());
  if (idx < 0) { sendError(404, "unknown device"); return; }

  releasePin(devices[idx]);
  for (size_t i = (size_t)idx; i + 1 < nDevices; i++) devices[i] = devices[i + 1];
  nDevices--;
  memset(&devices[nDevices], 0, sizeof(Device));
  saveDevices();
  purgeDeviceRefs(id.c_str());
  sendJson(200, "{\"ok\":true}");
}

// ================================================================ v4.0.0 : customise the whole smart home
// Rooms, scenes, automations, look & feel and the device order are SAVED in flash and survive
// a restart. Only the countdown timers live in RAM (a restart cancels them).

// ---------------------------------------------------------------- v4: small parsers
bool parseOnOff(String v, bool& out) {
  v.trim();
  v.toLowerCase();
  if (v == "1" || v == "true" || v == "on")   { out = true;  return true; }
  if (v == "0" || v == "false" || v == "off") { out = false; return true; }
  return false;
}

bool allDigits(const String& s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

bool looksNumeric(String s) {
  s.trim();
  unsigned int i = 0;
  if (s.length() > 0 && (s[0] == '-' || s[0] == '+')) i = 1;
  bool digit = false, dot = false;
  for (; i < s.length(); i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') digit = true;
    else if (c == '.' && !dot) dot = true;
    else return false;
  }
  return digit;
}

// Whole number argument. False when missing or not a number.
bool argInt(const char* name, int& out) {
  if (!server.hasArg(name)) return false;
  String s = server.arg(name);
  s.trim();
  String body = (s.length() > 0 && s[0] == '-') ? s.substring(1) : s;
  if (!allDigits(body)) return false;
  out = (int)s.toInt();
  return true;
}

// "22:30" -> minutes after midnight, -1 when it is not a valid time.
int parseHHMM(String s) {
  s.trim();
  int c = s.indexOf(':');
  if (c < 1 || c > 2) return -1;
  String h = s.substring(0, c);
  String m = s.substring(c + 1);
  if (m.length() != 2 || !allDigits(h) || !allDigits(m)) return -1;
  int hh = (int)h.toInt(), mm = (int)m.toInt();
  if (hh > 23 || mm > 59) return -1;
  return hh * 60 + mm;
}

const char* DAY_NAMES[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

// "all", "weekdays", "weekend" or "mon,tue,sat" (also full names or 0-6, 0 = Sunday).
bool parseDays(String s, uint8_t& out) {
  s.trim();
  s.toLowerCase();
  if (s.length() == 0 || s == "all" || s == "daily" || s == "everyday") { out = 0x7F; return true; }
  if (s == "weekdays") { out = 0x3E; return true; }      // Mon..Fri
  if (s == "weekend")  { out = 0x41; return true; }      // Sat + Sun
  uint8_t m = 0;
  int pos = 0;
  while (pos < (int)s.length()) {
    int e = s.indexOf(',', pos);
    if (e < 0) e = s.length();
    String tok = s.substring(pos, e);
    pos = e + 1;
    tok.trim();
    if (tok.length() == 0) continue;
    int d = -1;
    if (tok.length() == 1 && tok[0] >= '0' && tok[0] <= '6') {
      d = tok[0] - '0';
    } else if (tok.length() >= 3) {
      for (int k = 0; k < 7; k++) if (tok.startsWith(DAY_NAMES[k])) d = k;
    }
    if (d < 0) return false;
    m |= (uint8_t)(1 << d);
  }
  if (m == 0) return false;
  out = m;
  return true;
}

String daysJson(uint8_t mask) {
  String j = "[";
  bool first = true;
  for (int k = 0; k < 7; k++) {
    if (!(mask & (1 << k))) continue;
    if (!first) j += ',';
    first = false;
    j += '"'; j += DAY_NAMES[k]; j += '"';
  }
  j += "]";
  return j;
}

// ---------------------------------------------------------------- v4: saving to flash
// Split "a|b|c" into at most maxN fields. Returns how many were found.
int splitPipe(const String& line, String* f, int maxN) {
  int n = 0, s = 0;
  while (n < maxN) {
    int p = line.indexOf('|', s);
    if (p < 0) { f[n++] = line.substring(s); break; }
    f[n++] = line.substring(s, p);
    s = p + 1;
  }
  return n;
}

void saveKey(const char* key, const String& value) {
  prefs.begin(PREF_NS, false);
  size_t n = prefs.putString(key, value);
  prefs.end();
  if (n == 0 && value.length() > 0) {
    Serial.print("WARNING: could not save ");
    Serial.println(key);
  }
}

String loadKey(const char* key) {
  prefs.begin(PREF_NS, false);
  String t = prefs.getString(key, "");
  prefs.end();
  return t;
}

// ---------------------------------------------------------------- v4: time (NTP clock)
bool timeSynced() { return time(nullptr) > 1700000000L; }

bool localNow(struct tm& t) {
  time_t n = time(nullptr);
  if (n < 1700000000L) return false;
  localtime_r(&n, &t);
  return true;
}

bool validTz(const String& s) {
  if (s.length() == 0 || s.length() > 40) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == ',' || c == '.' || c == ':' || c == '/' || c == '+' || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

void applyTz() {
  setenv("TZ", tzString.c_str(), 1);
  tzset();
}

// Called when the router connection comes up. Needs internet; without it the clock stays
// unset and only the clock-based (time) automations wait.
void startNtp() {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
  applyTz();
}

String timeJson() {
  struct tm t;
  bool ok = localNow(t);
  String j = "{\"ok\":true,\"synced\":";
  j += jsonBool(ok);
  j += ",\"tz\":\"";      j += esc(tzString);
  j += "\",\"epoch\":";   j += String(ok ? (unsigned long)time(nullptr) : 0UL);
  j += ",\"weekday\":\""; if (ok) j += DAY_NAMES[t.tm_wday];
  j += "\",\"local\":\"";
  if (ok) {
    char b[24];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t);
    j += b;
  }
  j += "\"}";
  return j;
}

void handleTime() {
  REQUIRE_AUTH();
  String tz = tzString;
  if (server.hasArg("tz")) {
    tz = server.arg("tz");
    tz.trim();
    if (!validTz(tz)) { sendError(400, "tz must be a POSIX time zone like IST-5:30"); return; }
  }
  long epoch = 0;
  if (server.hasArg("epoch")) {
    String e = server.arg("epoch");
    e.trim();
    if (!allDigits(e) || e.toInt() < 1700000000L) { sendError(400, "epoch must be a Unix time in seconds"); return; }
    epoch = e.toInt();
  }
  if (server.hasArg("tz")) {
    tzString = tz;
    saveKey("tz", tzString);
    applyTz();
  }
  if (epoch) {
    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
  }
  sendJson(200, timeJson());
}

// ---------------------------------------------------------------- v4: rooms
int indexOfRoom(int id) {
  for (size_t i = 0; i < nRooms; i++) if (rooms[i].id == id) return (int)i;
  return -1;
}

String roomsToText() {
  String t = "v1\n";
  for (size_t i = 0; i < nRooms; i++) {
    t += String((unsigned int)rooms[i].id); t += '|';
    t += rooms[i].name;                      t += '\n';
  }
  return t;
}

void saveRooms() { saveKey("rooms", roomsToText()); }

bool addRoomEntry(int id, const char* name) {
  if (nRooms >= MAX_ROOMS || id < 0 || id > 49 || indexOfRoom(id) >= 0) return false;
  rooms[nRooms].id = (uint8_t)id;
  copyStr(rooms[nRooms].name, sizeof(rooms[nRooms].name), name);
  nRooms++;
  return true;
}

// A device may use a room number that has no entry yet (the app can still create rooms on
// its own). Give such a number a default name ("Room 3") so it shows up in the room list.
void ensureRoom(int id) {
  if (indexOfRoom(id) >= 0) return;
  char nm[24];
  snprintf(nm, sizeof(nm), "Room %d", id + 1);
  if (addRoomEntry(id, nm)) saveRooms();
}

void loadRooms() {
  nRooms = 0;
  String t = loadKey("rooms");
  if (t.startsWith("v1\n")) {
    int pos = 3;
    while (pos < (int)t.length()) {
      int e = t.indexOf('\n', pos);
      if (e < 0) e = t.length();
      String line = t.substring(pos, e);
      pos = e + 1;
      String f[2];
      if (splitPipe(line, f, 2) < 2 || !allDigits(f[0])) continue;
      String nm = cleanName(f[1]);
      addRoomEntry((int)f[0].toInt(), nm.length() ? nm.c_str() : "Room");
    }
  }
  if (nRooms == 0) { addRoomEntry(0, "Room 1"); saveRooms(); }     // very first start
  for (size_t i = 0; i < nDevices; i++) ensureRoom(devices[i].room);
}

void moveRoom(int from, int to) {
  RoomEntry tmp = rooms[from];
  if (from < to) for (int i = from; i < to; i++) rooms[i] = rooms[i + 1];
  else           for (int i = from; i > to; i--) rooms[i] = rooms[i - 1];
  rooms[to] = tmp;
}

String roomsJson() {
  String j = "[";
  for (size_t i = 0; i < nRooms; i++) {
    if (i) j += ',';
    j += "{\"id\":";      j += String((unsigned int)rooms[i].id);
    j += ",\"name\":\"";  j += esc(rooms[i].name);
    j += "\"}";
  }
  j += "]";
  return j;
}

void handleRoomList() {
  REQUIRE_AUTH();
  sendJson(200, String("{\"ok\":true,\"rooms\":") + roomsJson() + "}");
}

void handleRoomAdd() {
  REQUIRE_AUTH();
  if (nRooms >= MAX_ROOMS) { sendError(409, "room limit reached"); return; }
  String name = cleanName(server.arg("name"));
  if (name.length() == 0) { sendError(400, "name required"); return; }
  int id = -1;
  for (int k = 0; k < 50; k++) if (indexOfRoom(k) < 0) { id = k; break; }
  if (id < 0) { sendError(409, "room limit reached"); return; }
  addRoomEntry(id, name.c_str());
  saveRooms();
  sendJson(200, String("{\"ok\":true,\"id\":") + String(id) + "}");
}

void handleRoomRename() {
  REQUIRE_AUTH();
  int id;
  if (!argInt("id", id)) { sendError(400, "need id"); return; }
  int i = indexOfRoom(id);
  if (i < 0) { sendError(404, "unknown room"); return; }
  String name = cleanName(server.arg("name"));
  if (name.length() == 0) { sendError(400, "name required"); return; }
  copyStr(rooms[i].name, sizeof(rooms[i].name), name.c_str());
  saveRooms();
  sendJson(200, "{\"ok\":true}");
}

// Devices of the deleted room go to move=<room id> (default: the first other room).
void handleRoomDelete() {
  REQUIRE_AUTH();
  int id;
  if (!argInt("id", id)) { sendError(400, "need id"); return; }
  int i = indexOfRoom(id);
  if (i < 0) { sendError(404, "unknown room"); return; }
  if (nRooms <= 1) { sendError(409, "the last room cannot be deleted"); return; }
  int dest = rooms[i == 0 ? 1 : 0].id;
  if (server.hasArg("move")) {
    int m;
    if (!argInt("move", m) || m == id || indexOfRoom(m) < 0) {
      sendError(400, "move must be the id of another existing room");
      return;
    }
    dest = m;
  }
  int moved = 0;
  for (size_t k = 0; k < nDevices; k++)
    if (devices[k].room == id) { devices[k].room = dest; moved++; }
  for (size_t k = (size_t)i; k + 1 < nRooms; k++) rooms[k] = rooms[k + 1];
  nRooms--;
  memset(&rooms[nRooms], 0, sizeof(RoomEntry));
  saveRooms();
  if (moved) saveDevices();
  sendJson(200, String("{\"ok\":true,\"moved\":") + String(moved) + ",\"to\":" + String(dest) + "}");
}

void handleRoomMove() {
  REQUIRE_AUTH();
  int id, to;
  if (!argInt("id", id)) { sendError(400, "need id"); return; }
  int i = indexOfRoom(id);
  if (i < 0) { sendError(404, "unknown room"); return; }
  if (!argInt("to", to)) { sendError(400, "need to=<position>, 0 is first"); return; }
  to = constrain(to, 0, (int)nRooms - 1);
  moveRoom(i, to);
  saveRooms();
  sendJson(200, "{\"ok\":true}");
}

// ---------------------------------------------------------------- v4: look & feel (theme, colour, name)
bool validTheme(const String& s) { return s == "system" || s == "light" || s == "dark"; }

bool parseAccent(String s, uint32_t& out) {
  s.trim();
  if (s.startsWith("#")) s = s.substring(1);
  if (s.length() != 6) return false;
  uint32_t v = 0;
  for (unsigned int i = 0; i < 6; i++) {
    char c = s[i];
    uint32_t x;
    if (c >= '0' && c <= '9')      x = c - '0';
    else if (c >= 'a' && c <= 'f') x = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') x = c - 'A' + 10;
    else return false;
    v = v * 16 + x;
  }
  out = v;
  return true;
}

String accentString() {
  char b[8];
  snprintf(b, sizeof(b), "#%06X", (unsigned int)uiAccent);
  return String(b);
}

String uiJson() {
  String j = "{\"theme\":\"";
  j += uiTheme;
  j += "\",\"accent\":\"";
  j += accentString();
  j += "\"}";
  return j;
}

void saveUi() {
  char b[20];
  snprintf(b, sizeof(b), "%s|%06X", uiTheme, (unsigned int)uiAccent);
  saveKey("ui", String(b));
  saveKey("hub", String(hubName));
}

void handleUi() {
  REQUIRE_AUTH();
  String theme = uiTheme;
  uint32_t accent = uiAccent;
  String hub = hubName;
  if (server.hasArg("theme")) {
    theme = server.arg("theme");
    theme.trim();
    theme.toLowerCase();
    if (!validTheme(theme)) { sendError(400, "theme must be system, light or dark"); return; }
  }
  if (server.hasArg("accent") && !parseAccent(server.arg("accent"), accent)) {
    sendError(400, "accent must be a colour like 2F7BFF");
    return;
  }
  if (server.hasArg("name")) {
    hub = cleanName(server.arg("name"));
    if (hub.length() == 0) { sendError(400, "name required"); return; }
  }
  bool change = server.hasArg("theme") || server.hasArg("accent") || server.hasArg("name");
  if (change) {
    copyStr(uiTheme, sizeof(uiTheme), theme.c_str());
    uiAccent = accent;
    copyStr(hubName, sizeof(hubName), hub.c_str());
    saveUi();
  }
  sendJson(200, String("{\"ok\":true,\"name\":\"") + esc(hubName) + "\",\"theme\":\"" + uiTheme +
                "\",\"accent\":\"" + accentString() + "\"}");
}

void loadUi() {
  copyStr(hubName, sizeof(hubName), DEVICE_NAME);
  copyStr(uiTheme, sizeof(uiTheme), "dark");
  uiAccent = 0x2F7BFF;
  String ui = loadKey("ui");
  int p = ui.indexOf('|');
  if (p > 0) {
    String th = ui.substring(0, p);
    uint32_t a;
    if (validTheme(th)) copyStr(uiTheme, sizeof(uiTheme), th.c_str());
    if (parseAccent(ui.substring(p + 1), a)) uiAccent = a;
  }
  String hn = loadKey("hub");
  if (hn.length()) copyStr(hubName, sizeof(hubName), hn.c_str());
  String tz = loadKey("tz");
  tzString = validTz(tz) ? tz : String(TZ_DEFAULT);
}

// ---------------------------------------------------------------- v4: device order (buttons)
void moveDevice(int from, int to) {
  Device tmp = devices[from];
  if (from < to) for (int i = from; i < to; i++) devices[i] = devices[i + 1];
  else           for (int i = from; i > to; i--) devices[i] = devices[i - 1];
  devices[to] = tmp;
}

void handleDeviceMove() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  int idx = indexOfDevice(id.c_str());
  if (idx < 0) { sendError(404, "unknown device"); return; }
  int to;
  if (!argInt("to", to)) { sendError(400, "need to=<position>, 0 is first"); return; }
  to = constrain(to, 0, (int)nDevices - 1);
  moveDevice(idx, to);
  saveDevices();
  sendJson(200, "{\"ok\":true}");
}

// ids=a,b,c  -> these come first in that order, every other device keeps its place after them.
void handleDeviceOrder() {
  REQUIRE_AUTH();
  String ids = server.arg("ids");
  String seen[MAX_DEVICES];
  int count = 0, pos = 0;
  while (pos < (int)ids.length()) {
    int e = ids.indexOf(',', pos);
    if (e < 0) e = ids.length();
    String id = ids.substring(pos, e);
    pos = e + 1;
    id.trim();
    if (id.length() == 0) continue;
    if (indexOfDevice(id.c_str()) < 0) { sendError(404, "unknown device in ids"); return; }
    for (int k = 0; k < count; k++) if (seen[k] == id) { sendError(400, "a device is listed twice"); return; }
    seen[count++] = id;
  }
  if (count == 0) { sendError(400, "need ids=a,b,c"); return; }
  for (int p = 0; p < count; p++) moveDevice(indexOfDevice(seen[p].c_str()), p);
  saveDevices();
  sendJson(200, "{\"ok\":true}");
}

// ---------------------------------------------------------------- v4: scenes
int indexOfScene(const char* id) {
  for (size_t i = 0; i < nScenes; i++) if (!strcmp(scenes[i].id, id)) return (int)i;
  return -1;
}

SceneDef* findScene(const char* id) {
  int i = indexOfScene(id);
  return i < 0 ? nullptr : &scenes[i];
}

bool isBuiltinScene(const String& id) { return id == "alloff" || id == "goodnight" || id == "dinner"; }

bool sceneExists(const char* id) { return isBuiltinScene(String(id)) || indexOfScene(id) >= 0; }

String sceneActionsText(const SceneDef& s) {
  String t;
  for (uint8_t k = 0; k < s.n; k++) {
    if (k) t += ',';
    t += s.act[k].dev;
    t += ':';
    t += s.act[k].on ? '1' : '0';
  }
  return t;
}

String scenesToText() {
  String t = "v1\n";
  for (size_t i = 0; i < nScenes; i++) {
    t += scenes[i].id;   t += '|';
    t += scenes[i].name; t += '|';
    t += sceneActionsText(scenes[i]);
    t += '\n';
  }
  return t;
}

void saveScenes() { saveKey("scenes", scenesToText()); }

// "light:1,fan:0,lock:1" -> actions. strict = report mistakes (API); not strict = skip them (loading).
// Returns nullptr when fine, otherwise a short error text.
const char* parseSceneActions(const String& text, SceneDef& sc, bool strict) {
  sc.n = 0;
  int pos = 0;
  while (pos < (int)text.length()) {
    int e = text.indexOf(',', pos);
    if (e < 0) e = text.length();
    String tok = text.substring(pos, e);
    pos = e + 1;
    tok.trim();
    if (tok.length() == 0) continue;

    int c = tok.indexOf(':');
    String id = c > 0 ? tok.substring(0, c) : String("");
    String val = c > 0 ? tok.substring(c + 1) : String("");
    id.trim();
    bool on = false;
    const char* bad = nullptr;
    if (c <= 0 || !validId(id) || !parseOnOff(val, on)) {
      bad = "actions must look like light:1,fan:0";
    } else {
      Device* d = findDevice(id.c_str());
      if (!d) bad = "unknown device in actions";
      else if (!strcmp(d->type, "pir")) bad = "a sensor cannot be switched";
      else if (!strcmp(d->type, "lock") && !on) bad = "a lock can only be locked here - use /api/set to unlock";
      else {
        for (uint8_t k = 0; k < sc.n; k++) if (!strcmp(sc.act[k].dev, id.c_str())) bad = "a device is listed twice";
        if (!bad && sc.n >= MAX_SCENE_ACTIONS) bad = "too many actions in one scene";
      }
    }
    if (bad) {
      if (strict) return bad;
      continue;
    }
    copyStr(sc.act[sc.n].dev, sizeof(sc.act[sc.n].dev), id.c_str());
    sc.act[sc.n].on = on;
    sc.n++;
  }
  return nullptr;
}

void loadScenes() {
  nScenes = 0;
  String t = loadKey("scenes");
  if (!t.startsWith("v1\n")) return;
  int pos = 3;
  while (pos < (int)t.length() && nScenes < MAX_SCENES) {
    int e = t.indexOf('\n', pos);
    if (e < 0) e = t.length();
    String line = t.substring(pos, e);
    pos = e + 1;
    String f[3];
    if (splitPipe(line, f, 3) < 3) continue;
    if (!validId(f[0]) || f[0].length() > 7 || isBuiltinScene(f[0]) || indexOfScene(f[0].c_str()) >= 0) continue;
    SceneDef sc;
    memset(&sc, 0, sizeof(sc));
    copyStr(sc.id, sizeof(sc.id), f[0].c_str());
    copyStr(sc.name, sizeof(sc.name), f[1].length() ? f[1].c_str() : f[0].c_str());
    parseSceneActions(f[2], sc, false);
    scenes[nScenes++] = sc;
  }
}

// What a built-in scene or a saved scene does to the devices.
void applyActionToDevice(Device& d, bool on) {
  if (!strcmp(d.type, "pir")) return;
  if (!strcmp(d.type, "lock") && !on) return;            // scenes / automations never unlock
  if (dnd && on && !strcmp(d.icon, "siren")) return;     // Do-Not-Disturb blocks the siren
  setDevice(d, on);
}

bool runSceneById(const String& id) {
  if (id == "alloff") {
    allSwitchesOff();
  } else if (id == "goodnight") {
    allSwitchesOff();
    for (size_t i = 0; i < nDevices; i++)
      if (!strcmp(devices[i].type, "lock")) setDevice(devices[i], true);   // locked
  } else if (id == "dinner") {
    for (size_t i = 0; i < nDevices; i++) {
      Device& d = devices[i];
      if (!strcmp(d.icon, "light")) setDevice(d, true);
      else if (!strcmp(d.icon, "fan") || !strcmp(d.icon, "heater")) setDevice(d, false);
    }
  } else {
    SceneDef* s = findScene(id.c_str());
    if (!s) return false;
    for (uint8_t k = 0; k < s->n; k++) {
      Device* d = findDevice(s->act[k].dev);
      if (d) applyActionToDevice(*d, s->act[k].on);
    }
  }
  return true;
}

// device (on / off) or scene, used by timers and automations
bool runTarget(const char* target, bool isScene, bool on) {
  if (isScene) return runSceneById(String(target));
  Device* d = findDevice(target);
  if (!d) return false;
  applyActionToDevice(*d, on);
  return true;
}

// Checks that a device / scene can be used as the target of a timer or an automation.
const char* checkTarget(const char* target, bool isScene, bool on) {
  if (isScene) return sceneExists(target) ? nullptr : "unknown scene";
  Device* d = findDevice(target);
  if (!d) return "unknown device";
  if (!strcmp(d->type, "pir")) return "a sensor cannot be switched";
  if (!strcmp(d->type, "lock") && !on) return "a lock can only be locked here - use /api/set to unlock";
  return nullptr;
}

String sceneFullJson(const SceneDef& s) {
  String j = "{\"id\":\"";
  j += s.id;
  j += "\",\"name\":\"";  j += esc(s.name);
  j += "\",\"builtin\":false,\"actions\":[";
  for (uint8_t k = 0; k < s.n; k++) {
    if (k) j += ',';
    j += "{\"device\":\""; j += s.act[k].dev;
    j += "\",\"on\":";     j += jsonBool(s.act[k].on);
    j += "}";
  }
  j += "]}";
  return j;
}

void handleSceneList() {
  REQUIRE_AUTH();
  String j = "{\"ok\":true,\"scenes\":[";
  j += "{\"id\":\"alloff\",\"name\":\"All off\",\"builtin\":true}";
  j += ",{\"id\":\"goodnight\",\"name\":\"Good night\",\"builtin\":true}";
  j += ",{\"id\":\"dinner\",\"name\":\"Dinner\",\"builtin\":true}";
  for (size_t i = 0; i < nScenes; i++) { j += ','; j += sceneFullJson(scenes[i]); }
  j += "]}";
  sendJson(200, j);
}

// POST /api/scene?id=alloff|goodnight|dinner|s1 ...   run a scene
void handleScene() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  if (!runSceneById(id)) { sendError(404, "unknown scene"); return; }
  sendJson(200, "{\"ok\":true}");
}

void handleSceneAdd() {
  REQUIRE_AUTH();
  if (nScenes >= MAX_SCENES) { sendError(409, "scene limit reached"); return; }
  String name = cleanName(server.arg("name"));
  if (name.length() == 0) { sendError(400, "name required"); return; }
  SceneDef sc;
  memset(&sc, 0, sizeof(sc));
  const char* err = parseSceneActions(server.arg("actions"), sc, true);
  if (err) { sendError(400, err); return; }
  if (sc.n == 0) { sendError(400, "actions required, e.g. actions=light:1,fan:0"); return; }
  for (int k = 1; k < 1000; k++) {
    snprintf(sc.id, sizeof(sc.id), "s%d", k);
    if (indexOfScene(sc.id) < 0) break;
  }
  copyStr(sc.name, sizeof(sc.name), name.c_str());
  scenes[nScenes++] = sc;
  saveScenes();
  sendJson(200, String("{\"ok\":true,\"id\":\"") + sc.id + "\"}");
}

void handleSceneUpdate() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  if (isBuiltinScene(id)) { sendError(400, "built-in scenes cannot be changed"); return; }
  SceneDef* s = findScene(id.c_str());
  if (!s) { sendError(404, "unknown scene"); return; }
  SceneDef ns = *s;
  if (server.hasArg("name")) {
    String nm = cleanName(server.arg("name"));
    if (nm.length() == 0) { sendError(400, "name required"); return; }
    copyStr(ns.name, sizeof(ns.name), nm.c_str());
  }
  if (server.hasArg("actions")) {
    SceneDef tmp;
    memset(&tmp, 0, sizeof(tmp));
    const char* err = parseSceneActions(server.arg("actions"), tmp, true);
    if (err) { sendError(400, err); return; }
    if (tmp.n == 0) { sendError(400, "actions required, e.g. actions=light:1,fan:0"); return; }
    ns.n = tmp.n;
    memcpy(ns.act, tmp.act, sizeof(ns.act));
  }
  *s = ns;
  saveScenes();
  sendJson(200, "{\"ok\":true}");
}

void handleSceneMove() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  int i = indexOfScene(id.c_str());
  if (i < 0) { sendError(404, "unknown scene"); return; }
  int to;
  if (!argInt("to", to)) { sendError(400, "need to=<position>, 0 is first"); return; }
  to = constrain(to, 0, (int)nScenes - 1);
  SceneDef tmp = scenes[i];
  if (i < to) for (int k = i; k < to; k++) scenes[k] = scenes[k + 1];
  else        for (int k = i; k > to; k--) scenes[k] = scenes[k - 1];
  scenes[to] = tmp;
  saveScenes();
  sendJson(200, "{\"ok\":true}");
}

// ---------------------------------------------------------------- v4: countdown timers (RAM only)
void cancelCountdownsForRule(const char* rid) {
  if (!rid[0]) return;
  for (size_t i = 0; i < MAX_COUNTDOWNS; i++)
    if (countdowns[i].used && !strcmp(countdowns[i].rule, rid)) countdowns[i].used = false;
}

bool addCountdown(const char* target, bool isScene, bool on, uint32_t afterSec, const char* ruleId, char* idOut) {
  for (size_t i = 0; i < MAX_COUNTDOWNS; i++) {
    if (countdowns[i].used) continue;
    Countdown& c = countdowns[i];
    memset(&c, 0, sizeof(c));
    c.used = true;
    snprintf(c.id, sizeof(c.id), "t%lu", (unsigned long)(countdownSeq % 1000000UL));   // t1 .. t999999
    if (++countdownSeq > 999999UL) countdownSeq = 1;
    copyStr(c.target, sizeof(c.target), target);
    c.isScene = isScene;
    c.on      = on;
    c.dueAt   = millis() + afterSec * 1000UL;
    copyStr(c.rule, sizeof(c.rule), ruleId ? ruleId : "");
    if (idOut) copyStr(idOut, 8, c.id);
    return true;
  }
  return false;
}

void countdownsTick(uint32_t now) {
  for (size_t i = 0; i < MAX_COUNTDOWNS; i++) {
    Countdown& c = countdowns[i];
    if (!c.used || (int32_t)(now - c.dueAt) < 0) continue;
    c.used = false;
    runTarget(c.target, c.isScene, c.on);
  }
}

// device=<id>&on=0|1  or  scene=<id>  ->  target / isScene / on. nullptr when fine, else an error text.
const char* readTimerAction(char* target, size_t cap, bool& isScene, bool& on) {
  bool hasDev = server.hasArg("device"), hasScene = server.hasArg("scene");
  if (hasDev == hasScene) return "need device=..&on=0|1  or  scene=..";
  if (hasScene) {
    String s = server.arg("scene");
    s.trim();
    if (!validId(s)) return "unknown scene";
    copyStr(target, cap, s.c_str());
    isScene = true;
    on = true;
  } else {
    String d = server.arg("device");
    d.trim();
    if (!validId(d)) return "unknown device";
    copyStr(target, cap, d.c_str());
    isScene = false;
    if (!server.hasArg("on") || !parseOnOff(server.arg("on"), on)) return "need on=0|1 with device";
  }
  return checkTarget(target, isScene, on);
}

String actionJson(const char* target, bool isScene, bool on) {
  String j = "{";
  if (isScene) { j += "\"scene\":\""; j += target; j += "\""; }
  else         { j += "\"device\":\""; j += target; j += "\",\"on\":"; j += jsonBool(on); }
  j += "}";
  return j;
}

void handleTimerList() {
  REQUIRE_AUTH();
  uint32_t now = millis();
  String j = "{\"ok\":true,\"timers\":[";
  bool first = true;
  for (size_t i = 0; i < MAX_COUNTDOWNS; i++) {
    const Countdown& c = countdowns[i];
    if (!c.used) continue;
    if (!first) j += ',';
    first = false;
    int32_t left = (int32_t)(c.dueAt - now);
    if (left < 0) left = 0;
    j += "{\"id\":\"";        j += c.id;
    j += "\",\"remainingSec\":"; j += String((unsigned long)(left / 1000));
    j += ",\"action\":";      j += actionJson(c.target, c.isScene, c.on);
    j += ",\"automation\":\""; j += c.rule;
    j += "\"}";
  }
  j += "]}";
  sendJson(200, j);
}

// POST /api/timer/add?after=30&device=light&on=0   (after = minutes, or sec=90 for seconds)
void handleTimerAdd() {
  REQUIRE_AUTH();
  float secs = -1;
  if (server.hasArg("sec")) {
    if (!looksNumeric(server.arg("sec"))) { sendError(400, "sec must be a number"); return; }
    secs = server.arg("sec").toFloat();
  } else if (server.hasArg("after")) {
    if (!looksNumeric(server.arg("after"))) { sendError(400, "after must be a number of minutes"); return; }
    secs = server.arg("after").toFloat() * 60.0f;
  }
  if (secs < 1.0f) { sendError(400, "need after=<minutes> (or sec=<seconds>)"); return; }
  if (secs > 86400.0f) { sendError(400, "a timer can be 24 hours at most"); return; }

  char target[12];
  bool isScene = false, on = false;
  const char* err = readTimerAction(target, sizeof(target), isScene, on);
  if (err) { sendError(400, err); return; }

  char id[8];
  if (!addCountdown(target, isScene, on, (uint32_t)secs, nullptr, id)) { sendError(409, "timer limit reached"); return; }
  sendJson(200, String("{\"ok\":true,\"id\":\"") + id + "\"}");
}

void handleTimerCancel() {
  REQUIRE_AUTH();
  if (server.hasArg("all")) {
    for (size_t i = 0; i < MAX_COUNTDOWNS; i++) countdowns[i].used = false;
    sendJson(200, "{\"ok\":true}");
    return;
  }
  String id = server.arg("id");
  for (size_t i = 0; i < MAX_COUNTDOWNS; i++) {
    if (countdowns[i].used && id == countdowns[i].id) {
      countdowns[i].used = false;
      sendJson(200, "{\"ok\":true}");
      return;
    }
  }
  sendError(404, "unknown timer");
}

// ---------------------------------------------------------------- v4: automations (saved rules)
int indexOfRule(const char* id) {
  for (size_t i = 0; i < nRules; i++) if (!strcmp(rules[i].id, id)) return (int)i;
  return -1;
}

const char* trigName(uint8_t t) {
  switch (t) {
    case TR_TIME:   return "time";
    case TR_MOTION: return "motion";
    case TR_TEMP:   return "temp";
    default:        return "hum";
  }
}

String rulesToText() {
  String t = "v1\n";
  for (size_t i = 0; i < nRules; i++) {
    const AutoRule& r = rules[i];
    t += r.id;                                  t += '|';
    t += r.name;                                t += '|';
    t += r.enabled ? '1' : '0';                 t += '|';
    t += String((unsigned int)r.trig);          t += '|';
    t += String((unsigned int)r.atMin);         t += '|';
    t += String((unsigned int)r.days);          t += '|';
    t += r.sensor;                              t += '|';
    t += r.above ? '1' : '0';                   t += '|';
    t += String(r.level, 2);                    t += '|';
    t += r.target;                              t += '|';
    t += r.isScene ? '1' : '0';                 t += '|';
    t += r.on ? '1' : '0';                      t += '|';
    t += String((unsigned int)r.revertMin);
    t += '\n';
  }
  return t;
}

void saveRules() { saveKey("rules", rulesToText()); }

// Is this rule complete and does everything it points to exist?  nullptr = fine, else error text.
const char* checkRule(AutoRule& r) {
  if (r.trig == TR_TIME) {
    if (r.atMin >= 1440) return "need at=HH:MM for a time trigger";
    if (r.days == 0) return "days must not be empty";
  } else if (r.trig == TR_MOTION) {
    if (r.sensor[0] == '\0') {                          // only one PIR? use that one
      int cnt = 0;
      Device* only = nullptr;
      for (size_t i = 0; i < nDevices; i++)
        if (!strcmp(devices[i].type, "pir")) { cnt++; only = &devices[i]; }
      if (cnt != 1) return "need sensor=<id of a PIR device>";
      copyStr(r.sensor, sizeof(r.sensor), only->id);
    }
    Device* s = findDevice(r.sensor);
    if (!s || strcmp(s->type, "pir") != 0) return "sensor must be the id of a PIR device";
  } else if (r.trig == TR_TEMP || r.trig == TR_HUM) {
    if (!r.hasLevel) return "need above=<value> or below=<value>";
    if (r.trig == TR_TEMP && (r.level < -40.0f || r.level > 125.0f)) return "temperature must be between -40 and 125";
    if (r.trig == TR_HUM  && (r.level < 0.0f   || r.level > 100.0f)) return "humidity must be between 0 and 100";
  } else {
    return "trigger must be time, motion, temp or hum";
  }

  if (r.target[0] == '\0') return "need device=..&on=0|1  or  scene=..";
  const char* err = checkTarget(r.target, r.isScene, r.on);
  if (err) return err;
  if (r.revertMin) {
    if (r.isScene) return "revert works only with a device, not a scene";
    Device* d = findDevice(r.target);
    if (d && !strcmp(d->type, "lock")) return "revert does not work with a lock";
  }
  return nullptr;
}

void loadRules() {
  nRules = 0;
  String t = loadKey("rules");
  if (!t.startsWith("v1\n")) return;
  int pos = 3;
  while (pos < (int)t.length() && nRules < MAX_RULES) {
    int e = t.indexOf('\n', pos);
    if (e < 0) e = t.length();
    String line = t.substring(pos, e);
    pos = e + 1;
    String f[13];
    if (splitPipe(line, f, 13) < 13) continue;
    if (!validId(f[0]) || f[0].length() > 7 || indexOfRule(f[0].c_str()) >= 0) continue;

    AutoRule r;
    memset(&r, 0, sizeof(r));
    copyStr(r.id,   sizeof(r.id),   f[0].c_str());
    copyStr(r.name, sizeof(r.name), f[1].length() ? f[1].c_str() : f[0].c_str());
    r.enabled   = f[2] == "1";
    r.trig      = (uint8_t)constrain(f[3].toInt(), 0, 3);
    r.atMin     = (uint16_t)constrain(f[4].toInt(), 0, 65535);
    r.days      = (uint8_t)(f[5].toInt() & 0x7F);
    copyStr(r.sensor, sizeof(r.sensor), f[6].c_str());
    r.above     = f[7] == "1";
    r.level     = f[8].toFloat();
    r.hasLevel  = r.trig == TR_TEMP || r.trig == TR_HUM;
    copyStr(r.target, sizeof(r.target), f[9].c_str());
    r.isScene   = f[10] == "1";
    r.on        = f[11] == "1";
    r.revertMin = (uint16_t)constrain(f[12].toInt(), 0, 1440);
    r.armed     = true;
    if (checkRule(r)) {
      Serial.print("Skipping saved automation that no longer fits: ");
      Serial.println(r.id);
      continue;
    }
    rules[nRules++] = r;
  }
}

void removeRuleAt(size_t i) {
  cancelCountdownsForRule(rules[i].id);
  for (size_t k = i; k + 1 < nRules; k++) rules[k] = rules[k + 1];
  nRules--;
  memset(&rules[nRules], 0, sizeof(AutoRule));
}

// A deleted device is also taken out of scenes, timers and automations that used it.
void purgeDeviceRefs(const char* id) {
  bool scenesChanged = false, rulesChanged = false;
  for (size_t i = 0; i < nScenes; i++) {
    SceneDef& s = scenes[i];
    uint8_t w = 0;
    for (uint8_t k = 0; k < s.n; k++) if (strcmp(s.act[k].dev, id) != 0) s.act[w++] = s.act[k];
    if (w != s.n) { s.n = w; scenesChanged = true; }
  }
  for (size_t i = 0; i < MAX_COUNTDOWNS; i++)
    if (countdowns[i].used && !countdowns[i].isScene && !strcmp(countdowns[i].target, id)) countdowns[i].used = false;
  for (size_t i = 0; i < nRules;) {
    const AutoRule& r = rules[i];
    bool dead = (!r.isScene && !strcmp(r.target, id)) || (r.trig == TR_MOTION && !strcmp(r.sensor, id));
    if (dead) { removeRuleAt(i); rulesChanged = true; }
    else i++;
  }
  if (scenesChanged) saveScenes();
  if (rulesChanged)  saveRules();
}

void purgeSceneRefs(const char* id) {
  bool rulesChanged = false;
  for (size_t i = 0; i < MAX_COUNTDOWNS; i++)
    if (countdowns[i].used && countdowns[i].isScene && !strcmp(countdowns[i].target, id)) countdowns[i].used = false;
  for (size_t i = 0; i < nRules;) {
    if (rules[i].isScene && !strcmp(rules[i].target, id)) { removeRuleAt(i); rulesChanged = true; }
    else i++;
  }
  if (rulesChanged) saveRules();
}

void handleSceneDelete() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  if (isBuiltinScene(id)) { sendError(400, "built-in scenes cannot be deleted"); return; }
  int i = indexOfScene(id.c_str());
  if (i < 0) { sendError(404, "unknown scene"); return; }
  purgeSceneRefs(id.c_str());                 // timers / automations that ran this scene go too
  for (size_t k = (size_t)i; k + 1 < nScenes; k++) scenes[k] = scenes[k + 1];
  nScenes--;
  memset(&scenes[nScenes], 0, sizeof(SceneDef));
  saveScenes();
  sendJson(200, "{\"ok\":true}");
}

String ruleJson(const AutoRule& r) {
  String j = "{\"id\":\"";
  j += r.id;
  j += "\",\"name\":\"";    j += esc(r.name);
  j += "\",\"enabled\":";   j += jsonBool(r.enabled);
  j += ",\"trigger\":\"";   j += trigName(r.trig);
  j += "\"";
  if (r.trig == TR_TIME) {
    char hm[8];
    snprintf(hm, sizeof(hm), "%02u:%02u", (unsigned int)(r.atMin / 60), (unsigned int)(r.atMin % 60));
    j += ",\"at\":\"";      j += hm;
    j += "\",\"days\":";    j += daysJson(r.days);
    j += ",\"daysMask\":";  j += String((unsigned int)r.days);
  } else if (r.trig == TR_MOTION) {
    j += ",\"sensor\":\"";  j += r.sensor;
    j += "\"";
  } else {
    j += ",\"condition\":\""; j += r.above ? "above" : "below";
    j += "\",\"level\":";     j += String(r.level, 1);
  }
  j += ",\"action\":";      j += actionJson(r.target, r.isScene, r.on);
  j += ",\"revert\":";      j += String((unsigned int)r.revertMin);
  j += ",\"firedSinceBoot\":"; j += jsonBool(r.everFired);
  j += "}";
  return j;
}

// Reads the request into r. Only arguments that are present change anything.
// Returns nullptr when fine, else a short error text.
const char* readRuleArgs(AutoRule& r) {
  if (server.hasArg("name")) {
    String nm = cleanName(server.arg("name"));
    if (nm.length() == 0) return "name required";
    copyStr(r.name, sizeof(r.name), nm.c_str());
  }
  if (server.hasArg("enabled")) {
    bool b;
    if (!parseOnOff(server.arg("enabled"), b)) return "enabled must be 0 or 1";
    r.enabled = b;
  }
  if (server.hasArg("trigger")) {
    String t = server.arg("trigger");
    t.trim();
    t.toLowerCase();
    if      (t == "time")   r.trig = TR_TIME;
    else if (t == "motion") r.trig = TR_MOTION;
    else if (t == "temp")   r.trig = TR_TEMP;
    else if (t == "hum")    r.trig = TR_HUM;
    else return "trigger must be time, motion, temp or hum";
  }
  if (server.hasArg("at")) {
    int m = parseHHMM(server.arg("at"));
    if (m < 0) return "at must look like 22:30";
    r.atMin = (uint16_t)m;
  }
  if (server.hasArg("days")) {
    uint8_t m;
    if (!parseDays(server.arg("days"), m)) return "days: use all, weekdays, weekend or mon,tue,sat";
    r.days = m;
  }
  if (server.hasArg("sensor")) {
    String s = server.arg("sensor");
    s.trim();
    if (!validId(s)) return "sensor must be the id of a PIR device";
    copyStr(r.sensor, sizeof(r.sensor), s.c_str());
  }
  bool hasAbove = server.hasArg("above"), hasBelow = server.hasArg("below");
  if (hasAbove && hasBelow) return "use either above or below, not both";
  if (hasAbove || hasBelow) {
    String v = server.arg(hasAbove ? "above" : "below");
    if (!looksNumeric(v)) return "above / below must be a number";
    r.above = hasAbove;
    r.level = v.toFloat();
    r.hasLevel = true;
  }

  bool hasDev = server.hasArg("device"), hasScene = server.hasArg("scene");
  if (hasDev && hasScene) return "use either device or scene, not both";
  if (hasScene) {
    String s = server.arg("scene");
    s.trim();
    if (!validId(s)) return "unknown scene";
    copyStr(r.target, sizeof(r.target), s.c_str());
    r.isScene = true;
    r.on = true;
  }
  if (hasDev) {
    String d = server.arg("device");
    d.trim();
    if (!validId(d)) return "unknown device";
    copyStr(r.target, sizeof(r.target), d.c_str());
    r.isScene = false;
    bool on;
    if (!server.hasArg("on") || !parseOnOff(server.arg("on"), on)) return "need on=0|1 with device";
    r.on = on;
  } else if (!hasScene && server.hasArg("on")) {
    bool on;
    if (!parseOnOff(server.arg("on"), on)) return "on must be 0 or 1";
    r.on = on;
  }
  if (server.hasArg("revert")) {
    int v;
    if (!argInt("revert", v) || v < 0 || v > 1440) return "revert must be 0 to 1440 minutes";
    r.revertMin = (uint16_t)v;
  }
  return nullptr;
}

void handleAutoList() {
  REQUIRE_AUTH();
  String j = "{\"ok\":true,\"automations\":[";
  for (size_t i = 0; i < nRules; i++) { if (i) j += ','; j += ruleJson(rules[i]); }
  j += "]}";
  sendJson(200, j);
}

void handleAutoAdd() {
  REQUIRE_AUTH();
  if (nRules >= MAX_RULES) { sendError(409, "automation limit reached"); return; }
  AutoRule r;
  memset(&r, 0, sizeof(r));
  r.enabled = true;
  r.trig    = 0xFF;                 // must be given
  r.atMin   = 0xFFFF;
  r.days    = 0x7F;
  r.armed   = true;
  const char* err = readRuleArgs(r);
  if (!err) err = checkRule(r);
  if (err) { sendError(400, err); return; }
  for (int k = 1; k < 1000; k++) {
    snprintf(r.id, sizeof(r.id), "a%d", k);
    if (indexOfRule(r.id) < 0) break;
  }
  if (r.name[0] == '\0') {
    char nm[32];
    snprintf(nm, sizeof(nm), "Automation %s", r.id);
    copyStr(r.name, sizeof(r.name), nm);
  }
  rules[nRules++] = r;
  saveRules();
  sendJson(200, String("{\"ok\":true,\"id\":\"") + r.id + "\"}");
}

void handleAutoUpdate() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  int i = indexOfRule(id.c_str());
  if (i < 0) { sendError(404, "unknown automation"); return; }
  AutoRule nr = rules[i];
  const char* err = readRuleArgs(nr);
  if (!err) err = checkRule(nr);
  if (err) { sendError(400, err); return; }
  nr.armed = true;
  cancelCountdownsForRule(nr.id);
  rules[i] = nr;
  saveRules();
  sendJson(200, "{\"ok\":true}");
}

void handleAutoDelete() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  int i = indexOfRule(id.c_str());
  if (i < 0) { sendError(404, "unknown automation"); return; }
  removeRuleAt((size_t)i);
  saveRules();
  sendJson(200, "{\"ok\":true}");
}

void fireRule(AutoRule& r, uint32_t now) {
  runTarget(r.target, r.isScene, r.on);
  r.lastFire  = now;
  r.everFired = true;
  cancelCountdownsForRule(r.id);
  if (r.revertMin && !r.isScene) {
    if (!addCountdown(r.target, false, !r.on, (uint32_t)r.revertMin * 60UL, r.id, nullptr))
      Serial.println("No free timer slot - the automation will not switch back by itself.");
  }
  Serial.print("Automation fired: ");
  Serial.println(r.id);
}

// POST /api/automation/run?id=a1   run it now (to test), even when it is switched off
void handleAutoRun() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  int i = indexOfRule(id.c_str());
  if (i < 0) { sendError(404, "unknown automation"); return; }
  fireRule(rules[i], millis());
  sendJson(200, "{\"ok\":true}");
}

bool ruleCooledDown(const AutoRule& r, uint32_t now) {
  return !r.everFired || (now - r.lastFire) >= RULE_COOLDOWN_MS;
}

// Motion: fires when the PIR goes from quiet to active. Checked on every pass of loop().
void rulesTickMotion(uint32_t now) {
  for (size_t i = 0; i < nRules; i++) {
    AutoRule& r = rules[i];
    if (r.trig != TR_MOTION) continue;
    Device* s = findDevice(r.sensor);
    bool m = s && !strcmp(s->type, "pir") && s->on;
    if (m && !r.lastMotion && r.enabled && ruleCooledDown(r, now)) fireRule(r, now);
    r.lastMotion = m;
  }
}

// Clock and sensor rules, once per second.
void rulesTickSlow(uint32_t now) {
  struct tm t;
  bool haveTime = localNow(t);
  uint32_t epochMin = haveTime ? (uint32_t)(time(nullptr) / 60) : 0;
  for (size_t i = 0; i < nRules; i++) {
    AutoRule& r = rules[i];
    if (r.trig == TR_TIME) {
      if (!r.enabled || !haveTime || r.lastMinute == epochMin) continue;
      if (!(r.days & (1 << t.tm_wday))) continue;
      if ((unsigned int)(t.tm_hour * 60 + t.tm_min) != (unsigned int)r.atMin) continue;
      r.lastMinute = epochMin;
      fireRule(r, now);
    } else if (r.trig == TR_TEMP || r.trig == TR_HUM) {
      float v = (r.trig == TR_TEMP) ? tempC : humidity;
      if (isnan(v)) continue;
      bool met   = r.above ? (v >= r.level) : (v <= r.level);
      bool clear = r.above ? (v <= r.level - 1.0f) : (v >= r.level + 1.0f);   // 1 degree / 1 % hysteresis
      if (met) {
        if (r.armed && r.enabled && ruleCooledDown(r, now)) {
          r.armed = false;
          fireRule(r, now);
        }
      } else if (clear) {
        r.armed = true;
      }
    }
  }
}

// ---------------------------------------------------------------- v4: restart + load everything
void handleRestart() {
  REQUIRE_AUTH();
  restartPending = true;
  restartAt = millis() + 1200;
  sendJson(200, "{\"ok\":true,\"restart\":true}");
}

String stateExtras() {
  String j;
  j.reserve(700);
  j += ",\"time\":{\"synced\":"; j += jsonBool(timeSynced());
  j += ",\"tz\":\"";             j += esc(tzString);
  j += "\"}";
  j += ",\"ui\":";               j += uiJson();
  j += ",\"rooms\":";            j += roomsJson();
  j += ",\"scenes\":[";
  j += "{\"id\":\"alloff\",\"name\":\"All off\",\"builtin\":true}";
  j += ",{\"id\":\"goodnight\",\"name\":\"Good night\",\"builtin\":true}";
  j += ",{\"id\":\"dinner\",\"name\":\"Dinner\",\"builtin\":true}";
  for (size_t i = 0; i < nScenes; i++) {
    j += ",{\"id\":\"";  j += scenes[i].id;
    j += "\",\"name\":\""; j += esc(scenes[i].name);
    j += "\",\"builtin\":false}";
  }
  j += "]";
  return j;
}

// Everything that is saved in flash. The list order of devices / rooms / scenes is the order
// of the buttons in the app.
void loadAll() {
  nDevices = 0; nRooms = 0; nScenes = 0; nRules = 0;
  memset(countdowns, 0, sizeof(countdowns));
  if (!loadDevices()) seedDevices();     // very first start -> default devices
  loadRooms();
  loadScenes();
  loadRules();
  loadUi();
}

// ---------------------------------------------------------------- wifi: saved settings
void loadWifiPrefs() {
  prefs.begin(PREF_NS, false);
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  setupFlag = prefs.getBool("setup", false);
  prefs.end();
}

void saveWifi(const String& ssid, const String& pass) {
  prefs.begin(PREF_NS, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.remove("setup");
  prefs.end();
  savedSsid = ssid;
  savedPass = pass;
  setupFlag = false;
}

void forgetWifi() {
  prefs.begin(PREF_NS, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.putBool("setup", true);
  prefs.end();
  savedSsid = "";
  savedPass = "";
  setupFlag = true;
}

bool hasBuiltInWifi() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "YOUR_WIFI_NAME") != 0;
}

// ---------------------------------------------------------------- wifi: connect / hotspot
void beginSta(const String& ssid, const String& pass, bool useStatic) {
  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (useStatic) {
    WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS_SERVER);
  } else {
    IPAddress zero(0, 0, 0, 0);
    WiFi.config(zero, zero, zero);             // back to DHCP
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
}

void startAp() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);                      // STA stays on so networks can be scanned
  apActive = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Setup hotspot \"");
  Serial.print(AP_SSID);
  Serial.print("\" ");
  Serial.println(apActive ? "is ON  ->  http://192.168.4.1/" : "FAILED to start");
}

void stopAp() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  apActive = false;
  Serial.println("Setup hotspot off");
}

void startMdns() {
  MDNS.end();
  if (MDNS.begin("esp32-home")) MDNS.addService("http", "tcp", 80);   // http://esp32-home.local
}

void bootWifi() {
  if (savedSsid.length()) {
    curSsid = savedSsid; curPass = savedPass; curStatic = false;
  } else if (!setupFlag && hasBuiltInWifi()) {
    curSsid = WIFI_SSID; curPass = WIFI_PASS; curStatic = USE_STATIC_IP;
  } else {
    curSsid = ""; curPass = ""; curStatic = false;
  }

  if (curSsid.length() == 0) {                 // nothing to join -> straight to setup mode
    startAp();
    return;
  }

  Serial.print("Connecting to Wi-Fi \"");
  Serial.print(curSsid);
  Serial.print("\"");
  beginSta(curSsid, curPass, curStatic);
  uint32_t t0 = millis();
  while (!staUp() && millis() - t0 < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (staUp()) {
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi not connected - opening the setup hotspot.");
    startAp();
  }
}

String wifiJson() {
  const char* state = "idle";
  if (connState != CS_IDLE) state = "trying";
  else if (lastWifiError.length()) state = "failed";

  String j;
  j.reserve(400);
  j += "{\"ok\":true,\"state\":\""; j += state;
  j += "\",\"connected\":";         j += jsonBool(staUp());
  j += ",\"ssid\":\"";              j += esc(staUp() ? WiFi.SSID() : String(""));
  j += "\",\"ip\":\"";              j += ipString();
  j += "\",\"rssi\":";              j += String(staUp() ? WiFi.RSSI() : 0);
  j += ",\"mode\":\"";              j += wifiMode();
  j += "\",\"apSsid\":\"";          j += apActive ? esc(AP_SSID) : String("");
  j += "\",\"target\":\"";          j += connState != CS_IDLE ? esc(pendSsid) : String("");
  j += "\",\"saved\":";             j += jsonBool(savedSsid.length() > 0);
  j += ",\"error\":\"";             j += esc(lastWifiError);
  j += "\"}";
  return j;
}

void handleWifiStatus() {
  REQUIRE_AUTH();
  sendJson(200, wifiJson());
}

void sendScanList(bool scanning, const String& list) {
  sendJson(200, String("{\"ok\":true,\"scanning\":") + jsonBool(scanning) + ",\"networks\":[" + list + "]}");
}

void handleWifiScan() {
  REQUIRE_AUTH();
  if (server.hasArg("rescan")) {               // start a fresh scan in the background
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    sendScanList(true, "");
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { sendScanList(true, ""); return; }
  if (n < 0) {                                 // no scan yet -> start one
    WiFi.scanNetworks(true);
    sendScanList(true, "");
    return;
  }

  // Results are ready: strongest first, one line per network name, hidden ones skipped.
  int order[40];
  int count = 0;
  for (int i = 0; i < n && count < 40; i++) {
    if (WiFi.SSID(i).length() == 0) continue;
    int at = count++;
    while (at > 0 && WiFi.RSSI(order[at - 1]) < WiFi.RSSI(i)) {
      order[at] = order[at - 1];
      at--;
    }
    order[at] = i;
  }

  String list;
  list.reserve(900);
  int shown = 0;
  for (int k = 0; k < count && shown < 20; k++) {
    int i = order[k];
    String name = WiFi.SSID(i);
    bool dup = false;
    for (int m = 0; m < k; m++) if (WiFi.SSID(order[m]) == name) { dup = true; break; }
    if (dup) continue;
    if (shown) list += ',';
    list += "{\"ssid\":\""; list += esc(name);
    list += "\",\"rssi\":"; list += String(WiFi.RSSI(i));
    list += ",\"secure\":"; list += jsonBool(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    list += "}";
    shown++;
  }
  WiFi.scanDelete();
  sendScanList(false, list);
}

void handleWifiConnect() {
  REQUIRE_AUTH();
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32) { sendError(400, "Wi-Fi name must be 1 to 32 bytes"); return; }
  if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) {
    sendError(400, "password must be 8 to 63 characters (empty = open network)");
    return;
  }
  if (connState != CS_IDLE) { sendError(409, "already connecting"); return; }

  pendSsid = ssid;
  pendPass = pass;
  pendAt = millis() + 1000;                    // let this answer leave before the radio switches
  connState = CS_PENDING;
  lastWifiError = "";
  sendJson(200, "{\"ok\":true,\"pending\":true}");
}

void handleWifiReset() {
  REQUIRE_AUTH();
  forgetWifi();
  restartPending = true;
  restartAt = millis() + 1200;
  sendJson(200, "{\"ok\":true,\"restart\":true}");
}

// ---------------------------------------------------------------- web page + routes
const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32 Smart Home</title>
<style>:root{--a:#2f7bff}body{font-family:sans-serif;background:#0b1220;color:#e5e9f0;margin:0;padding:16px}
body.light{background:#f4f6fb;color:#16202f}
h2{margin:0 0 4px}small{color:#8b95a7}.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:10px;margin-top:14px}
button{padding:16px 8px;border-radius:14px;border:1px solid #2a3550;background:#141d30;color:#e5e9f0;font-size:15px}
body.light button{background:#fff;color:#16202f;border-color:#c9d2e3}
button.on{background:var(--a);border-color:var(--a);color:#fff}button.sc{background:transparent;border-color:var(--a)}</style></head><body>
<h2 id="t">ESP32 Smart Home</h2><small id="s">connecting...</small><div class="g" id="g"></div><div class="g" id="sc"></div>
<script>
const key=new URLSearchParams(location.search).get('key')||'';
const H=key?{'X-API-Key':key}:{};
async function load(){try{const r=await fetch('/api/state',{headers:H});const j=await r.json();
const u=j.ui||{};document.documentElement.style.setProperty('--a',u.accent||'#2f7bff');
document.body.className=(u.theme=='light'||(u.theme=='system'&&matchMedia('(prefers-color-scheme: light)').matches))?'light':'';
document.getElementById('t').textContent=j.name;
document.getElementById('s').textContent=(j.ssid||j.apSsid)+' | '+j.ip+' | '+(j.temp==null?'--':j.temp+' C')+' | '+j.powerW+' W';
const g=document.getElementById('g');g.innerHTML='';
j.devices.forEach(d=>{const b=document.createElement('button');b.textContent=d.name+(d.on?' - ON':' - OFF');
if(d.on)b.className='on';if(d.type!='pir')b.onclick=async()=>{await fetch('/api/set?id='+d.id+'&on='+(d.on?0:1),{method:'POST',headers:H});load()};
g.appendChild(b)});
const sc=document.getElementById('sc');sc.innerHTML='';
(j.scenes||[]).forEach(s=>{const b=document.createElement('button');b.className='sc';b.textContent=s.name;
b.onclick=async()=>{await fetch('/api/scene?id='+s.id,{method:'POST',headers:H});load()};sc.appendChild(b)})
}catch(e){document.getElementById('s').textContent='offline'}}
load();setInterval(load,2000);
</script></body></html>)HTML";

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }
void handleNotFound() { sendError(404, "not found"); }

// ---------------------------------------------------------------- background work
void wifiTick(uint32_t now) {
  // 1) A new Wi-Fi was requested by the app: switch to it (DHCP).
  if (connState == CS_PENDING) {
    if ((int32_t)(now - pendAt) >= 0) {
      connState = CS_TRYING;
      tryStart = now;
      WiFi.disconnect();
      beginSta(pendSsid, pendPass, false);
      Serial.print("Trying new Wi-Fi: ");
      Serial.println(pendSsid);
    }
    return;
  }

  // 2) Waiting for the new Wi-Fi: keep it if it works, otherwise go back to the old one.
  if (connState == CS_TRYING) {
    if (now - tryStart > 1500 && staUp() && WiFi.SSID() == pendSsid) {
      saveWifi(pendSsid, pendPass);
      curSsid = pendSsid; curPass = pendPass; curStatic = false;
      connState = CS_IDLE;
      Serial.print("New Wi-Fi saved. IP: ");
      Serial.println(WiFi.localIP());
    } else if (now - tryStart > 25000) {
      lastWifiError = "could not connect - check the Wi-Fi name and password";
      connState = CS_IDLE;
      Serial.println("New Wi-Fi failed - going back to the old one.");
      WiFi.disconnect();
      if (curSsid.length()) beginSta(curSsid, curPass, curStatic);
    }
    return;
  }

  // 3) Normal life: reconnect when the router is gone, hotspot only while there is no Wi-Fi.
  bool up = staUp();
  if (up != lastUp) {
    lastUp = up;
    lastChange = now;
    if (up) { startMdns(); startNtp(); }
  }
  if (up) {
    if (apActive && now - lastChange > 30000UL) stopAp();
  } else {
    uint32_t retryEvery = apActive ? 30000UL : 10000UL;
    if (curSsid.length() && now - lastWifiTry > retryEvery) {
      lastWifiTry = now;
      WiFi.disconnect();
      beginSta(curSsid, curPass, curStatic);
    }
    if (!apActive && now - lastChange > 20000UL) startAp();
  }
}

void tick() {
  uint32_t now = millis();

  // PIR is read on every pass so short pulses are not missed.
  for (size_t i = 0; i < nDevices; i++)
    if (!strcmp(devices[i].type, "pir")) devices[i].on = digitalRead(devices[i].pin) == HIGH;

  rulesTickMotion(now);          // automations: motion
  countdownsTick(now);           // timers that ran out

  // Once per second: energy estimate + safety auto-off timers.
  if (now - lastTick >= 1000) {
    float dt = (now - lastTick) / 1000.0f;
    lastTick = now;
    for (size_t i = 0; i < nDevices; i++) {
      Device& d = devices[i];
      if (strcmp(d.type, "switch") || !d.on) continue;
      d.wh += d.watts * dt / 3600.0f;
      if (d.autoOffMin && (now - d.onSince) > (uint32_t)d.autoOffMin * 60000UL) setDevice(d, false);
    }
    rulesTickSlow(now);          // automations: clock, temperature, humidity
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

  wifiTick(now);

  if (restartPending && (int32_t)(now - restartAt) >= 0) {
    Serial.println("Restarting ...");
    delay(50);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("ESP32 Smart Home v" FW_VERSION);

  WiFi.persistent(false);                  // we keep the Wi-Fi in our own settings
  loadWifiPrefs();

  loadAll();                               // devices, rooms, scenes, automations, look & feel
  for (size_t i = 0; i < nDevices; i++) initPin(devices[i]);

#if HAS_DHT
  dht.begin();
#endif

  bootWifi();
  lastUp = staUp();
  lastChange = millis();
  startMdns();
  if (staUp()) startNtp();

  const char* wanted[] = {"X-API-Key"};
  server.collectHeaders(wanted, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state",         HTTP_ANY, handleState);
  server.on("/api/set",           HTTP_ANY, handleSet);
  server.on("/api/dnd",           HTTP_ANY, handleDnd);
  server.on("/api/scene",         HTTP_ANY, handleScene);
  server.on("/api/device/add",    HTTP_ANY, handleDeviceAdd);
  server.on("/api/device/update", HTTP_ANY, handleDeviceUpdate);
  server.on("/api/device/delete", HTTP_ANY, handleDeviceDelete);
  server.on("/api/wifi/status",   HTTP_ANY, handleWifiStatus);
  server.on("/api/wifi/scan",     HTTP_ANY, handleWifiScan);
  server.on("/api/wifi/connect",  HTTP_ANY, handleWifiConnect);
  server.on("/api/wifi/reset",    HTTP_ANY, handleWifiReset);

  server.on("/api/room/list",     HTTP_ANY, handleRoomList);
  server.on("/api/room/add",      HTTP_ANY, handleRoomAdd);
  server.on("/api/room/rename",   HTTP_ANY, handleRoomRename);
  server.on("/api/room/delete",   HTTP_ANY, handleRoomDelete);
  server.on("/api/room/move",     HTTP_ANY, handleRoomMove);
  server.on("/api/device/move",   HTTP_ANY, handleDeviceMove);
  server.on("/api/device/order",  HTTP_ANY, handleDeviceOrder);
  server.on("/api/ui",            HTTP_ANY, handleUi);
  server.on("/api/scene/list",    HTTP_ANY, handleSceneList);
  server.on("/api/scene/add",     HTTP_ANY, handleSceneAdd);
  server.on("/api/scene/update",  HTTP_ANY, handleSceneUpdate);
  server.on("/api/scene/delete",  HTTP_ANY, handleSceneDelete);
  server.on("/api/scene/move",    HTTP_ANY, handleSceneMove);
  server.on("/api/timer/list",    HTTP_ANY, handleTimerList);
  server.on("/api/timer/add",     HTTP_ANY, handleTimerAdd);
  server.on("/api/timer/cancel",  HTTP_ANY, handleTimerCancel);
  server.on("/api/automation/list",   HTTP_ANY, handleAutoList);
  server.on("/api/automation/add",    HTTP_ANY, handleAutoAdd);
  server.on("/api/automation/update", HTTP_ANY, handleAutoUpdate);
  server.on("/api/automation/delete", HTTP_ANY, handleAutoDelete);
  server.on("/api/automation/run",    HTTP_ANY, handleAutoRun);
  server.on("/api/time",          HTTP_ANY, handleTime);
  server.on("/api/restart",       HTTP_ANY, handleRestart);
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
