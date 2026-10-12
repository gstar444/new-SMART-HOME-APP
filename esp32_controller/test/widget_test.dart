import 'package:esp32_controller/models.dart';
import 'package:esp32_controller/settings.dart';
import 'package:esp32_controller/voice.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('default ESP32 URL is an HTTP URL with an IP host', () {
    final uri = Uri.parse(esp32Url);
    expect(uri.scheme, 'http');
    expect(uri.host, '192.168.1.37');
    expect(uri.path, '/');
  });

  group('normalizeUrl', () {
    test('adds http:// to a bare IP', () {
      expect(normalizeUrl('192.168.1.50'), 'http://192.168.1.50');
    });
    test('keeps https URLs', () {
      expect(normalizeUrl(' https://esp32.local/ '), 'https://esp32.local/');
    });
    test('rejects empty and unsupported input', () {
      expect(normalizeUrl(''), isNull);
      expect(normalizeUrl('ftp://192.168.1.1'), isNull);
    });
  });

  test('settings survive a JSON round trip', () {
    const original = AppSettings(
      title: 'My Home',
      devices: [
        Device(name: 'Hall', url: 'http://192.168.1.10/'),
        Device(name: 'Garden', url: 'http://192.168.1.11/'),
      ],
      activeDevice: 1,
      themeMode: ThemeMode.dark,
      accent: 0xFF2563EB,
      showToolbar: false,
      pageZoom: 120,
      autoRefreshSeconds: 30,
      timeoutSeconds: 10,
      javaScript: false,
      hindi: false,
    );
    final copy = AppSettings.fromJson(original.toJson());
    expect(copy.title, 'My Home');
    expect(copy.devices.length, 2);
    expect(copy.activeUrl, 'http://192.168.1.11/');
    expect(copy.themeMode, ThemeMode.dark);
    expect(copy.accent, 0xFF2563EB);
    expect(copy.showToolbar, isFalse);
    expect(copy.pageZoom, 120);
    expect(copy.autoRefreshSeconds, 30);
    expect(copy.timeoutSeconds, 10);
    expect(copy.javaScript, isFalse);
    expect(copy.hindi, isFalse);
  });

  test('out-of-range saved values are clamped', () {
    final s = AppSettings.fromJson({'pageZoom': 999, 'activeDevice': 7});
    expect(s.pageZoom, 150);
    expect(s.activeUrl, defaultEsp32Url);
  });

  test('rooms, room assignments, API key and poll interval survive a JSON round trip', () {
    const original = AppSettings(
      apiKey: 'k1',
      pollSeconds: 5,
      rooms: ['Hall', 'Bedroom'],
      roomOf: {'fan': 1},
    );
    final copy = AppSettings.fromJson(original.toJson());
    expect(copy.apiKey, 'k1');
    expect(copy.pollSeconds, 5);
    expect(copy.rooms, ['Hall', 'Bedroom']);
    expect(copy.roomOf['fan'], 1);
  });

  group('ESP32 state JSON', () {
    final sample = <String, dynamic>{
      'name': 'Home',
      'fw': '2.0.0',
      'ssid': 'चाय वाला',
      'ip': '192.168.1.37',
      'rssi': -52,
      'uptime': 120,
      'temp': 22.5,
      'hum': null,
      'dnd': false,
      'camera': '',
      'powerW': 72.0,
      'devices': [
        {
          'id': 'light',
          'name': 'Main Light',
          'type': 'switch',
          'group': 'Lighting',
          'icon': 'light',
          'room': 0,
          'on': true,
          'watts': 12,
          'wh': 1.5,
        },
        {'id': 'lock', 'name': 'Smart Lock', 'type': 'lock', 'on': true},
        {'id': '', 'name': 'broken entry without id'},
      ],
    };

    test('parses fields and skips entries without an id', () {
      final st = Esp32State.fromJson(sample);
      expect(st.ssid, 'चाय वाला');
      expect(st.temp, 22.5);
      expect(st.hum, isNull);
      expect(st.devices.length, 2);
      expect(st.devices.first.watts, 12);
      expect(st.devices[1].isLock, isTrue);
      expect(st.devices[1].controllable, isTrue);
    });

    test('withDevice flips only the chosen device', () {
      final st = Esp32State.fromJson(sample).withDevice('light', on: false);
      expect(st.devices.first.on, isFalse);
      expect(st.devices[1].on, isTrue);
    });
  });

  group('voice commands', () {
    const devices = [
      Esp32Device(
          id: 'light', name: 'Main Light', type: 'switch', group: 'Lighting',
          icon: 'light', room: 0, on: false),
      Esp32Device(
          id: 'fan', name: 'Ceiling Fan', type: 'switch', group: 'Climate',
          icon: 'fan', room: 0, on: false),
      Esp32Device(
          id: 'lock', name: 'Smart Lock', type: 'lock', group: 'Security',
          icon: 'lock', room: 0, on: true),
    ];

    test('English and Hindi device commands', () {
      expect(parseVoice('light on', devices)?.set, {'light': true});
      expect(parseVoice('पंखा बंद करो', devices)?.set, {'fan': false});
      expect(parseVoice('light and fan off', devices)?.set,
          {'light': false, 'fan': false});
    });

    test('scenes', () {
      expect(parseVoice('sab band', devices)?.scene, 'alloff');
      expect(parseVoice('good night', devices)?.scene, 'goodnight');
    });

    test('a door can be locked but never unlocked by voice', () {
      expect(parseVoice('lock the door', devices)?.set, {'lock': true});
      final blocked = parseVoice('unlock the door', devices);
      expect(blocked?.lockBlocked, isTrue);
      expect(blocked?.set, isEmpty);
    });

    test('unknown or incomplete sentences return null', () {
      expect(parseVoice('hello there', devices), isNull);
      expect(parseVoice('light', devices), isNull);
    });
  });
}
