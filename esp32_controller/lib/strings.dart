import 'settings.dart';

/// Minimal Hindi / English text table. Pick the language in Settings.
class Tr {
  const Tr(this.hi);

  factory Tr.of(AppSettings s) => Tr(s.hindi);

  final bool hi;

  String _t(String h, String e) => hi ? h : e;

  String get settings => _t('सेटिंग्स', 'Settings');
  String get refresh => _t('रीफ़्रेश', 'Refresh');
  String get switchDevice => _t('डिवाइस बदलें', 'Switch device');
  String get retry => _t('फिर कोशिश करें', 'Try again');
  String get openSettings => _t('सेटिंग्स खोलें', 'Open settings');
  String get cantReach => _t('ESP32 से कनेक्ट नहीं हुआ', "Couldn't reach the ESP32");
  String get cantReachHelp => _t(
      'जाँचें कि फ़ोन और ESP32 एक ही Wi-Fi या VPN पर हैं और ESP32 चालू है।',
      'Check that your phone and the ESP32 are on the same Wi-Fi or VPN and that the ESP32 is powered on.');
  String get timedOut => _t('पेज समय पर नहीं खुला', 'The page took too long to load');
  String get settingsHint => _t('बदलाव अपने-आप सेव हो जाते हैं।', 'Changes are saved automatically.');

  String get sectionGeneral => _t('सामान्य', 'General');
  String get sectionDevices => _t('डिवाइस', 'Devices');
  String get sectionAppearance => _t('रूप-रंग', 'Appearance');
  String get sectionBehaviour => _t('व्यवहार', 'Behaviour');
  String get sectionLanguage => _t('भाषा', 'Language');
  String get sectionAbout => _t('सुरक्षा और जानकारी', 'Security and info');

  String get appName => _t('ऐप का नाम', 'App name');
  String get addDevice => _t('डिवाइस जोड़ें', 'Add device');
  String get editDevice => _t('डिवाइस बदलें', 'Edit device');
  String get deviceName => _t('डिवाइस का नाम', 'Device name');
  String get deviceUrl => _t('ESP32 URL या IP', 'ESP32 URL or IP');
  String get deviceUrlHint => 'http://192.168.1.37/';
  String get invalidUrl => _t('सही IP या http/https URL डालें।', 'Enter a valid IP or http/https URL.');
  String get nameRequired => _t('नाम खाली नहीं हो सकता।', 'Name can’t be empty.');
  String get delete => _t('हटाएँ', 'Delete');
  String get cancel => _t('रद्द करें', 'Cancel');
  String get save => _t('सेव करें', 'Save');
  String get ok => _t('ठीक है', 'OK');

  String get themeMode => _t('थीम', 'Theme');
  String get themeSystem => _t('सिस्टम', 'System');
  String get themeLight => _t('लाइट', 'Light');
  String get themeDark => _t('डार्क', 'Dark');
  String get accent => _t('मुख्य रंग', 'Accent colour');
  String get customColour => _t('अपना रंग (HEX)', 'Custom colour (HEX)');
  String get invalidHex => _t('6 अंकों का HEX कोड डालें, जैसे 0F766E।', 'Enter a 6-digit HEX code, e.g. 0F766E.');
  String get showToolbar => _t('ऊपर की टूलबार दिखाएँ', 'Show top toolbar');
  String get showToolbarHelp => _t('बंद करने पर पूरी स्क्रीन पर पेज दिखेगा।', 'Turn off for a full-screen page.');
  String get pageZoom => _t('पेज ज़ूम', 'Page zoom');
  String get autoRefresh => _t('ऑटो-रीफ़्रेश', 'Auto-refresh');
  String get off => _t('बंद', 'Off');
  String every(int seconds) {
    if (seconds >= 60) {
      final m = seconds ~/ 60;
      return _t('हर $m मिनट', 'Every $m min');
    }
    return _t('हर $seconds सेकंड', 'Every $seconds s');
  }

  String get loadTimeout => _t('लोड होने का समय-सीमा', 'Load timeout');
  String seconds(int s) => _t('$s सेकंड', '$s s');
  String get javaScript => _t('JavaScript चालू रखें', 'Enable JavaScript');
  String get javaScriptHelp => _t(
      'ESP32 के बटन और सेंसर के लिए ज़रूरी है।', 'Needed for the ESP32 buttons and sensor data.');
  String get language => _t('ऐप की भाषा', 'App language');
  String get resetAll => _t('सभी सेटिंग्स रीसेट करें', 'Reset all settings');
  String get resetConfirm => _t('सभी सेटिंग्स डिफ़ॉल्ट पर लौट जाएँगी।', 'All settings will return to their defaults.');
  String get securityNote => _t(
      'घर के बाहर से इस्तेमाल के लिए निजी VPN (जैसे Tailscale) चलाएँ। ESP32 का HTTP पोर्ट सीधे इंटरनेट पर कभी खुला न छोड़ें।',
      'To use it away from home, use a private VPN such as Tailscale. Never expose the ESP32 HTTP port directly to the internet.');

  // ---------------------------------------------------------------- dashboard
  static const _wdHi = ['सोम', 'मंगल', 'बुध', 'गुरु', 'शुक्र', 'शनि', 'रवि'];
  static const _wdEn = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  static const _moHi = ['जन', 'फ़र', 'मार्च', 'अप्रै', 'मई', 'जून', 'जुल', 'अग', 'सित', 'अक्टू', 'नव', 'दिस'];
  static const _moEn = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

  /// weekday: DateTime.weekday (1 = Monday ... 7 = Sunday)
  String weekday(int weekday) => (hi ? _wdHi : _wdEn)[(weekday - 1).clamp(0, 6).toInt()];

  /// month: DateTime.month (1..12)
  String month(int month) => (hi ? _moHi : _moEn)[(month - 1).clamp(0, 11).toInt()];

  String get connectedTo => _t('कनेक्टेड: ', 'Connected to: ');
  String get status => _t('स्थिति: ', 'Status: ');
  String get online => _t('ऑनलाइन', 'Online');
  String get offline => _t('ऑफ़लाइन', 'Offline');
  String get systemState => _t('ESP32 सिस्टम स्थिति', 'ESP32 System State');
  String get connecting => _t('ESP32 से जुड़ रहे हैं…', 'Connecting to ESP32…');
  String get openWebPage => _t('ESP32 वेब पेज', 'ESP32 web page');

  // errors
  String get errNetwork => _t('ESP32 से कनेक्ट नहीं हो पाया। Wi-Fi और IP जाँचें।', "Can't reach the ESP32. Check Wi-Fi and the IP address.");
  String get errAuth => _t('API key गलत है। Settings में सही key डालें।', 'Wrong API key. Enter the right one in Settings.');
  String get errFirmware => _t(
      'ESP32 मिल गया, लेकिन उसमें नया फ़र्मवेयर नहीं है। esp32_smart_home.ino फ़्लैश करें (या ऊपर 🌐 से पुराना वेब पेज खोलें)।',
      'The ESP32 answered, but it is not running the new firmware. Flash esp32_smart_home.ino (or open the old web page with the globe icon).');
  String get errServer => _t('ESP32 ने गलती लौटाई।', 'The ESP32 returned an error.');
  String get notConnected => _t('ESP32 कनेक्टेड नहीं है।', 'ESP32 is not connected.');

  // voice
  String get voiceAssistant => _t('वॉइस असिस्टेंट', 'Voice Assistant');
  String get voicePrompt => _t('बोलिए, जैसे "लाइट चालू करो"', 'Say something like "light on"');
  String voiceHeard(String text) => _t('सुना: “$text”', 'Heard: “$text”');
  String get voiceNoMatch => _t('समझ नहीं आया। "पंखा बंद" जैसे बोलें।', 'Did not understand. Try "fan off".');
  String get voiceNoUnlock => _t('सुरक्षा के लिए ताला वॉइस से नहीं खुलेगा। टाइल दबाएँ।', 'For safety the lock cannot be opened by voice. Use the tile.');
  String get voiceUnavailable => _t('इस फ़ोन में वॉइस इनपुट उपलब्ध नहीं है।', 'Voice input is not available on this phone.');
  String voiceDone(String text) => _t('हो गया: “$text”', 'Done: “$text”');

  // rooms
  String get addRoom => _t('कमरा जोड़ें', 'Add Room');
  String get roomName => _t('कमरे का नाम', 'Room name');
  String get renameRoom => _t('नाम बदलें', 'Rename');
  String get deleteRoom => _t('कमरा हटाएँ', 'Delete room');
  String get moveToRoom => _t('किस कमरे में रखें?', 'Move to which room?');

  // groups & tiles
  String group(String g) {
    switch (g) {
      case 'Lighting':
        return _t('लाइटिंग', 'Lighting');
      case 'Climate':
        return _t('क्लाइमेट', 'Climate');
      case 'Security':
        return _t('सुरक्षा', 'Security');
      case 'Scenes':
        return _t('सीन और मोड', 'Scenes & Modes');
      default:
        return g;
    }
  }

  String get tempSensor => _t('तापमान सेंसर', 'Temp Sensor');
  String get routines => _t('रूटीन', 'Routines');
  String get moodDinner => _t('मूड सीन: डिनर', 'Mood Scene: Dinner');
  String get dnd => _t('DND मोड', 'DND Mode');
  String get securityCam => _t('सिक्योरिटी कैम', 'Security Cam');
  String get addControl => _t('कंट्रोल जोड़ें', 'Add Control');
  String get addControlHelp => _t(
      'नया कंट्रोल जोड़ने के लिए ESP32 फ़र्मवेयर की DEVICES[] टेबल में एक लाइन जोड़ें और फ़्लैश करें। ऐप उसका टाइल अपने-आप दिखा देगा।',
      'To add a control, add one line to the DEVICES[] table in the ESP32 firmware and flash it. The app shows the new tile automatically.');
  String get capOn => _t('चालू', 'On');
  String get capOff => _t('बंद', 'Off');
  String get capLocked => _t('लॉक्ड', 'Locked');
  String get capUnlocked => _t('अनलॉक्ड', 'Unlocked');
  String get capMotion => _t('मोशन', 'Motion');
  String get capClear => _t('शांत', 'Clear');
  String get capRun => _t('चलाएँ', 'Run');
  String get capView => _t('देखें', 'View');
  String humidityText(int percent) => _t('नमी $percent%', 'Humidity $percent%');
  String unlockQuestion(String name) => _t('“$name” अनलॉक करें?', 'Unlock “$name”?');
  String get unlock => _t('अनलॉक', 'Unlock');
  String get cameraNotSet => _t('ESP32 फ़र्मवेयर में CAMERA_URL खाली है।', 'CAMERA_URL is empty in the ESP32 firmware.');

  // scenes
  String get routinesTitle => _t('रूटीन चुनें', 'Choose a routine');
  String get sceneAllOff => _t('सब बंद', 'All off');
  String get sceneGoodNight => _t('गुड नाइट (सब बंद + ताला)', 'Good night (all off + lock)');
  String get sceneDinner => _t('डिनर', 'Dinner');
  String sceneName(String id) {
    switch (id) {
      case 'alloff':
        return sceneAllOff;
      case 'goodnight':
        return sceneGoodNight;
      case 'dinner':
        return sceneDinner;
      default:
        return id;
    }
  }

  // energy
  String energyTitle(String room) => _t('ऊर्जा खपत ($room)', 'Energy Consumption ($room)');
  String energyNow(int watts) => _t('अभी: $watts W', 'Now: $watts W');
  String get smartPlugs => _t('स्मार्ट प्लग', 'Smart Plugs');
  String get energyNote => _t('अनुमानित', 'Estimated');

  // settings additions
  String get apiKey => _t('API key (वैकल्पिक)', 'API key (optional)');
  String get apiKeyNone => _t('सेट नहीं है', 'Not set');
  String get apiKeyHelp => _t('फ़र्मवेयर में API_KEY डाला हो तो वही यहाँ डालें।', 'Enter the same value as API_KEY in the firmware.');
  String get pollInterval => _t('लाइव अपडेट अंतराल', 'Live update interval');
}
