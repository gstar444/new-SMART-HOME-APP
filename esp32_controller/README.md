# ESP32 Smart Home (Flutter + ESP32 firmware) - v2.0

Native Android dashboard jo ESP32 ko Wi-Fi (HTTP/JSON) se control karta hai.
Project me do hisse hain:

| Folder | Kya hai |
|---|---|
| `lib/`, `android/`, `assets/` | Flutter app (APK yahin se banta hai) |
| `esp32_firmware/esp32_smart_home/` | ESP32 ka Arduino sketch (API server) |

> Is project ko banate waqt Flutter/Dart SDK aur asli ESP32 available nahi tha.
> Firmware ko PC par stub headers ke saath compile + API logic test kiya gaya hai,
> lekin `flutter analyze / test / build` aur asli board par test aapko chalana hoga.
> Koi error aaye to error text bhej do, theek kar denge.

---------------------------------------------------------------------------

## 1. ESP32 firmware flash karna

1. Arduino IDE -> Boards Manager -> **esp32 by Espressif** install karein.
2. Library Manager -> **DHT sensor library** (Adafruit) + **Adafruit Unified Sensor** install karein
   (DHT22 nahi hai to sketch me `#define HAS_DHT 0` kar dein).
3. `esp32_firmware/esp32_smart_home/esp32_smart_home.ino` kholein aur upar ke "YOUR SETTINGS" me badlein:
   - `WIFI_SSID`, `WIFI_PASS`
   - `STATIC_IP` (default `192.168.1.37`, apne router ke hisaab se `GATEWAY` bhi)
   - `API_KEY` (optional password; app Settings me wahi daalna hoga)
4. Board: **ESP32 Dev Module** -> Upload. Serial Monitor (115200) me IP dikhega.
5. Browser me `http://192.168.1.37/` kholkar test page dekh sakte hain.

### Default pin table (sketch me `devices[]` table me badal sakte hain)

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
GET  /api/state                         sab kuch (wifi, sensor, devices)
POST /api/set?id=light&on=1             ek device on/off
POST /api/dnd?on=1                      Do-Not-Disturb
POST /api/scene?id=alloff|goodnight|dinner
```
`API_KEY` set ho to header `X-API-Key: <key>` zaroori hai.
Naya control jodna ho to `devices[]` me ek line add karein - app me tile apne-aap aa jayega.

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
- Tile dabao = relay on/off (turant dikhta hai, fail ho to wapas ho jata hai). Tile **long-press** = kisi aur room me bhejo.
- Room tab long-press = rename / delete. **Add Room** = naya room (rooms sirf app me save hote hain).
- **Voice**: mic dabao, bolo "light on", "पंखा बंद करो", "sab band", "good night". ताला voice se sirf lock hota hai, unlock nahi.
- **Energy**: ESP32 relay-ON-time x `watts` se *anumaanit* kWh nikalta hai (asli meter nahi). ESP32 reboot par reset hota hai.
- **Security Cam**: firmware me `CAMERA_URL` (ESP32-CAM stream) set karein.
- Top bar ka globe icon = purana ESP32 web page (WebView) fallback.
- Settings (tune icon): ESP32 IP/devices, API key, theme, accent colour, live-update interval, language.

## 5. Ghar ke bahar se use

Phone ko ghar ke LAN se private VPN (jaise Tailscale subnet routing) ke zariye jodein.
ESP32 ka HTTP port seedha internet par kabhi na kholein; local network par bhi `API_KEY` lagana behtar hai.
