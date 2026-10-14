# ESP32 Smart Home (Flutter app v3.0 + ESP32 firmware v4.0)

Native Android dashboard jo ESP32 ko Wi-Fi (HTTP/JSON) se control karta hai.
Project me do hisse hain:

| Folder | Kya hai |
|---|---|
| `lib/`, `android/`, `assets/` | Flutter app (APK yahin se banta hai) |
| `esp32_firmware/esp32_smart_home/` | ESP32 ka Arduino sketch (API server) |

### v3.0 / v4.0 me naya kya hai

1. **Wi-Fi connection setting (app se)**: nayi Wi-Fi chunna + password dalna, ESP32 ko naye router se jodna,
   connection status aur IP dekhna, zaroorat par Wi-Fi reset karna. Wi-Fi ESP32 ki flash me save hoti hai.
2. **Jitne chahein utne bulb / fan / device**: dashboard par **Add Bulb**, **Add Fan**, **Add Device** buttons.
   Device ka naam, kamra, icon aur GPIO pin chunein; baad me icon/pin badal sakte hain ya device hata sakte hain.
   Ek hi kamre me kai bulb ho sakte hain. Device ki list ab firmware ke code me fixed nahi hai -
   wo ESP32 ki flash me **configuration ki tarah save** hoti hai aur restart ke baad bhi rehti hai.
3. **पूरे स्मार्ट होम को कस्टमाइज़ करना**:
   - नए Room बनाना, नाम बदलना और हटाना।
   - Main Light, Ceiling Fan जैसे नाम बदलना।
   - ऐप का रंग, थीम और बटन का क्रम बदलना।
   - टाइमर, ऑटोमेशन और सीन बनाना।
   - ESP32 की सेटिंग को सेव करके रीस्टार्ट के बाद भी बनाए रखना।

   Ye sab **firmware v4.0.0 me API se** control hota hai (neeche section 1B). App ki screens abhi in naye
   endpoints se nahi judi hain - app purane tareeke se hi chalti rahegi, kuch toot-ta nahi.

> **Zaroori:** nayi features ke liye ESP32 par naya `esp32_smart_home.ino` (v4.0.0) ek baar flash karna hoga.
> Purane firmware (v2) ke saath app chalti hai, par Wi-Fi aur Add Device buttons "firmware purana hai" ka message denge.

> Is project ko banate waqt Flutter/Dart SDK aur asli ESP32 available nahi tha.
> Firmware ko PC par stub headers ke saath compile + API logic test kiya gaya hai,
> lekin `flutter analyze / test / build` aur asli board par test aapko chalana hoga.
> Koi error aaye to error text bhej do, theek kar denge.

---------------------------------------------------------------------------

## 1. ESP32 firmware flash karna

1. Arduino IDE -> Boards Manager -> **esp32 by Espressif** install karein.
2. Library Manager -> **DHT sensor library** (Adafruit) + **Adafruit Unified Sensor** install karein
   (DHT22 nahi hai to sketch me `#define HAS_DHT 0` kar dein).
3. `esp32_firmware/esp32_smart_home/esp32_smart_home.ino` kholein. "YOUR SETTINGS" me (sab optional):
   - `WIFI_SSID`, `WIFI_PASS` - pehli baar ke liye. **Khali (YOUR_WIFI_NAME) chhod dein to ESP32 seedha setup hotspot
     chalu karega aur Wi-Fi aap app se set karenge** (neeche section 1A).
   - `STATIC_IP` (default `192.168.1.37`, apne router ke hisaab se `GATEWAY` bhi) - sirf upar wali pehli Wi-Fi ke liye.
     App se chuni gayi Wi-Fi hamesha DHCP (router se IP) leti hai.
   - `AP_SSID` / `AP_PASS` - setup hotspot ka naam/password (default `SmartHome-Setup` / `12345678`).
   - `API_KEY` (optional password; app Settings me wahi daalna hoga)
4. Board: **ESP32 Dev Module** -> Upload. Serial Monitor (115200) me IP dikhega.
5. Browser me `http://192.168.1.37/` kholkar test page dekh sakte hain.

### Shuruaati (default) devices

Pehli baar chalne par ESP32 ye 8 devices banata hai (`DEFAULT_DEVICES[]`). Uske baad list app se badli jati hai
(add / edit / delete) aur flash me save rehti hai - sketch dobara flash karne par bhi list wahi rehti hai.

| Tile | GPIO | Note |
|---|---|---|
| Main Light | 23 | relay |
| Ceiling Fan | 22 | relay |
| Smart Curtains | 19 | relay (abhi simple on/off) |
| Heater | 21 | relay, 180 min safety auto-off |
| Air Purifier | 18 | relay |
| Alarm Siren | 17 | relay/buzzer, 5 min auto-off, DND me block |
| Smart Lock | 16 | relay (energised = unlocked) |
| PIR Sensor | 27 | input |
| DHT22 (temp/humidity) | 4 | |

Relay board "active LOW" hai to `RELAY_ACTIVE_LOW 1` (default) rakhein.

**Safety:** 220 V ke load (heater, fan, light) sirf sahi rating ke relay/SSR se, fuse aur insulated box me
chalayein. Heater ko hobby relay se na chalayein.

### API

```
GET  /api/state                         sab kuch (wifi, sensor, devices, khali GPIO pins)
POST /api/set?id=light&on=1             ek device on/off
POST /api/dnd?on=1                      Do-Not-Disturb
POST /api/scene?id=alloff|goodnight|dinner

POST /api/device/add?name=..&icon=light&pin=23&room=0[&watts=12]
POST /api/device/update?id=..[&name=..][&icon=..][&pin=..][&room=..][&watts=..]
POST /api/device/delete?id=..

GET  /api/wifi/status                   Wi-Fi ki sthiti (idle / trying / failed), IP, hotspot
GET  /api/wifi/scan[?rescan=1]          aas-paas ke networks (scan background me chalta hai)
POST /api/wifi/connect?ssid=..&pass=..  naya Wi-Fi (fail hone par purana apne-aap wapas)
POST /api/wifi/reset                    saved Wi-Fi mita do, setup (hotspot) mode me restart

(v4.0: rooms, scenes, timers, automations, theme/colour, button order, time - section 1B me)
```
`API_KEY` set ho to header `X-API-Key: <key>` zaroori hai.
Naya control jodna ho to app me **Add Device** dabayein - firmware me koi line add karne ki zaroorat nahi.

**Kaun se GPIO pin milte hain:** 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33
(DHT22 chalu ho to 4 reserved). PIR sensor ke liye input-only 34, 35, 36, 39 bhi.
Boot/flash wale pins (0, 2, 12, 15, 1, 3, 6-11) jaan-boojhkar nahi diye gaye. Ek pin do devices ko nahi mil sakta.
Zyada se zyada 24 devices.

### 1A. Wi-Fi app se set karna

1. App kholo -> upar ka **Wi-Fi icon** (ya Settings -> *Wi-Fi कनेक्शन सेटिंग*, ya dashboard ka Wi-Fi status card).
2. **Wi-Fi नेटवर्क खोजें** -> apna network chuno -> password daalo -> **कनेक्ट करें**.
3. ESP32 naye router se judta hai (30 sec tak). App nayi IP dikha deti hai aur ESP32 ka pata khud update kar leti hai.
   Agar app ESP32 tak nahi pahunch paayi (phone doosri Wi-Fi par hai), to phone ko bhi usi nayi Wi-Fi se jodein aur
   *नया IP डालें* se IP likh dein (router ki device list ya Arduino Serial Monitor me dikhta hai).
4. Galat password/naam ho to ESP32 **apne-aap purani Wi-Fi par wapas** aa jata hai - kuch bigadta nahi.
5. Hamesha wahi IP chahiye to router me ESP32 ke liye *DHCP reservation* kar dein.

**Setup hotspot:** ESP32 kisi Wi-Fi se na jud paye (ya Wi-Fi reset ho) to wo apna hotspot `SmartHome-Setup`
(password `12345678`) chalu karta hai. Phone ko usse jodein, app me Wi-Fi page par *ऐप को सेटअप पते (192.168.4.1) से जोड़ें*
dabayein, phir ghar ki Wi-Fi chunein. Na jude to phone ka mobile data band karke dekhein.

**Wi-Fi reset:** Wi-Fi page ke neeche *Wi-Fi रीसेट करें*. Saved Wi-Fi mit jati hai, ESP32 setup mode me restart hota hai.
Devices ki list nahi mitti.

---------------------------------------------------------------------------

### 1B. Poora smart home API se customize karna (firmware v4.0)

Neeche ke saare kaam `curl` (ya Postman / browser ke liye `/` test page) se ho sakte hain.
Ye sab ESP32 ki **flash me save** hota hai aur restart ke baad bhi rehta hai
(sirf *countdown timer* RAM me rehte hain - restart unhe cancel kar deta hai).
Naam me space ya Hindi ho to `-G --data-urlencode` use karein. `API_KEY` set ho to `-H "X-API-Key: <key>"` lagayein.

```bash
IP=192.168.1.37
# naam / Hindi ke liye:  curl -X POST -G --data-urlencode "name=मेरी रसोई" http://$IP/api/room/add
```

**Room** (kamre) - `GET /api/state` ya `GET /api/room/list` me `rooms` milta hai

```
POST /api/room/add?name=Kitchen              naya room, id wapas milti hai
POST /api/room/rename?id=1&name=Hall         naam badlo
POST /api/room/delete?id=1[&move=0]          room hatao; uske devices move=<room id> me jate hain
POST /api/room/move?id=1&to=0                room ka kram (0 = sabse pehle)
POST /api/device/update?id=fan&room=1        device ko room me daalo
```

**Naam badalna** - `POST /api/device/update?id=light&name=Tube Light` (Main Light, Ceiling Fan, kuch bhi).

**App ka rang, theme aur hub ka naam**

```
GET|POST /api/ui?theme=dark|light|system&accent=2F7BFF&name=Mera Ghar
```

**Button ka kram** (list ka order hi tiles / buttons ka order hai)

```
POST /api/device/move?id=fan&to=0                    ek device ko pehle number par
POST /api/device/order?ids=heater,fan,light          ye devices is kram me sabse pehle, baaki uske baad
POST /api/scene/move?id=s1&to=0                      scene ka kram
```

**Scene** (apne scene; `alloff`, `goodnight`, `dinner` built-in rehte hain, unhe badla/hataya nahi ja sakta)

```
GET  /api/scene/list
POST /api/scene/add?name=Movie&actions=light:0,fan:1,lock:1     -> {"id":"s1"}
POST /api/scene/update?id=s1&name=Cinema&actions=light:1,heater:0
POST /api/scene/delete?id=s1
POST /api/scene?id=s1                                           scene chalao (built-in jaise hi)
```
`actions` = `device_id:1|0` ki comma list (zyada se zyada 12). Lock sirf **lock** ho sakta hai (`lock:1`).

**Timer** (countdown, ek baar chalta hai; 1 second se 24 ghante)

```
POST /api/timer/add?after=30&device=heater&on=0      30 minute baad heater band   (sec=90 se seconds)
POST /api/timer/add?after=10&scene=goodnight         10 minute baad scene
GET  /api/timer/list                                 kitna time bacha hai
POST /api/timer/cancel?id=t1        |        POST /api/timer/cancel?all=1
```

**Automation** (rule - flash me save, chalu/band kar sakte hain)

```
# 1) Samay: roz raat 10:30 par Good night       (days = all | weekdays | weekend | mon,tue,sat)
POST /api/automation/add?name=Raat&trigger=time&at=22:30&days=all&scene=goodnight

# 2) Motion (PIR): hilne par light on, 5 minute baad apne-aap off (revert)
POST /api/automation/add?trigger=motion&sensor=pir&device=light&on=1&revert=5

# 3) Temperature / Humidity: 30 C se upar fan on;  hawa me 40 % se kam ho to purifier on
POST /api/automation/add?trigger=temp&above=30&device=fan&on=1
POST /api/automation/add?trigger=hum&below=40&device=purifier&on=1

GET  /api/automation/list
POST /api/automation/update?id=a1&enabled=0          band karo (aur koi bhi field badal sakte hain)
POST /api/automation/delete?id=a1
POST /api/automation/run?id=a1                       abhi chalakar test karo
```

**Samay (clock)**: time wale automation ke liye ESP32 internet (NTP) se samay leta hai. Default time zone India
(`IST-5:30`, sketch me `TZ_DEFAULT`). `GET /api/time` se dekh sakte hain ki clock set hai ya nahi;
`POST /api/time?tz=UTC0` zone badalta hai, internet na ho to `POST /api/time?epoch=<unix seconds>` se haath se set karein.
Clock set hone tak sirf time wale automation ruke rehte hain, baaki sab chalta hai.

**Zaroori baatein**

- **Lock kabhi automation / scene / timer se unlock nahi hota** (sirf lock). Unlock sirf `POST /api/set?id=lock&on=0`
  se - jaise pehle. Siren par Do-Not-Disturb ab bhi lagu hai.
- Heater ka 180 minute safety auto-off ab bhi kaam karta hai, chahe automation use chalaye.
- Temp / humidity rule tab chalta hai jab shart *nayi* sach hoti hai, aur dubara tab jab value 1 degree / 1 % wapas
  neeche (ya upar) aa jaye - isse relay baar-baar nahi khat-khatata. Ek rule 10 second me do baar nahi chalta.
  Rule banate (ya chalu karte) waqt shart pehle se sach ho to wo turant ek baar chal jata hai.
- Device hatane par wo scene, timer aur automation se bhi hat jata hai; scene hatane par use chalane wale automation bhi.
- Limit: 24 devices, 20 rooms, 10 scenes (har me 12 actions), 16 automations, 12 timers.
- App abhi rooms ka naam, theme aur order apne andar rakhti hai; firmware ke ye naye fields app me dikhane ke liye
  app ko in endpoints se jodna padega (alag kaam).

---------------------------------------------------------------------------

## 2. Logo (`app_logo.png`)

| File | Kaam |
|---|---|
| `assets/images/app_logo.png` | app ke andar (loading screen, top bar) + launcher icon ka source |
| `assets/icon/app_logo_foreground.png` | adaptive icon ka foreground (source copy) |
| `android/app/src/main/res/mipmap-*/ic_launcher.png` | purane Android ke icons |
| `android/app/src/main/res/drawable-nodpi/ic_launcher_foreground.png` | adaptive icon foreground |
| `android/app/src/main/res/drawable-nodpi/ic_launcher_monochrome.png` | Android 13 themed icon |
| `android/app/src/main/res/drawable-nodpi/smart_home_logo.png` | splash screen |
| `android/app/src/main/res/values/smart_home_colors.xml` | icon/splash ka cream background |

`pubspec.yaml` me `assets: - assets/images/app_logo.png` declared hai, isliye
`Image.asset('assets/images/app_logo.png')` chalta hai.

Logo baad me badalna ho to sabse aasaan tareeqa `flutter_launcher_icons`:

```yaml
# pubspec.yaml
dev_dependencies:
  flutter_launcher_icons: ^0.14.4

flutter_launcher_icons:
  android: true
  ios: false
  image_path: "assets/images/app_logo.png"
  adaptive_icon_background: "#F3EADB"
  adaptive_icon_foreground: "assets/icon/app_logo_foreground.png"
```
```
dart run flutter_launcher_icons
```
(iOS/macOS/Windows ke icons abhi Flutter default hain - sirf Android target hai.)

---------------------------------------------------------------------------

## 3. APK banana (Windows)

```bat
flutter clean
flutter pub get
flutter analyze
flutter test
flutter build apk --release --split-per-abi
```
APK: `build\app\outputs\flutter-apk\app-arm64-v8a-release.apk`
(ek hi bada APK chahiye to `--split-per-abi` hata dein -> `app-release.apk`).

Gradle cache wala `.aar` error aaye: VS Code band karein, `android` folder me `gradlew --stop`, phir dobara build.

---------------------------------------------------------------------------

### PC par Flutter nahi hai? GitHub se APK banwayein

1. github.com par free account -> New repository (private rakh sakte hain).
2. Is project folder ke *andar ki* saari files/folders (jisme `pubspec.yaml` hai, `.github` folder samet) repo me upload karein.
3. Repo me **Actions** tab -> **Build APK** -> **Run workflow**.
4. 5-10 minute baad run khologe to neeche **Artifacts -> smart-home-apk** milega (zip me APK). Log me `flutter analyze`/`test` ke errors bhi dikhenge.

---------------------------------------------------------------------------

## 4. App kaise kaam karti hai

- **Dashboard**: clock, Wi-Fi/ESP32 status, voice button, day/night switch, Room tabs, energy card,
  Lighting / Climate / Security / Scenes tiles.
- Tile dabao = relay on/off (turant dikhta hai, fail ho to wapas ho jata hai).
- Tile **long-press** = menu: **badlo** (naam, icon, GPIO pin, kamra), **doosre kamre me bhejo**, **device hatao**.
- **Add Bulb / Add Fan / Add Device**: *Create Device* form khulta hai - naam, kamra, icon, GPIO pin (aur optional watts).
  Jo pin kisi aur device ke paas hai wo grey dikhta hai. Device turant ESP32 ki flash me save ho jata hai.
- Room tab long-press = rename / delete. **Add Room** = naya room (rooms sirf app me save hote hain).
- **Voice**: mic dabao, bolo "light on", "पंखा बंद करो", "sab band", "good night". ताला voice se sirf lock hota hai, unlock nahi.
- **Energy**: ESP32 relay-ON-time x `watts` se *anumaanit* kWh nikalta hai (asli meter nahi). ESP32 reboot par reset hota hai.
- **Security Cam**: firmware me `CAMERA_URL` (ESP32-CAM stream) set karein.
- Top bar ka globe icon = purana ESP32 web page (WebView) fallback.
- Wi-Fi icon (upar) / Settings -> Wi-Fi कनेक्शन सेटिंग: Wi-Fi chunna, status + IP, reset.
- Settings (tune icon): Wi-Fi, ESP32 IP/devices, API key, theme, accent colour, live-update interval, language.

## 5. Ghar ke bahar se use

Phone ko ghar ke LAN se private VPN (jaise Tailscale subnet routing) ke zariye jodein.
ESP32 ka HTTP port seedha internet par kabhi na kholein; local network par bhi `API_KEY` lagana behtar hai.
