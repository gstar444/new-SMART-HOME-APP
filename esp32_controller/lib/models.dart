/// Data classes for the JSON that the ESP32 firmware returns from GET /api/state.

double? _num(Object? v) => v is num ? v.toDouble() : null;
int? _int(Object? v) => v is num ? v.toInt() : null;

class Esp32Device {
  const Esp32Device({
    required this.id,
    required this.name,
    required this.type,
    required this.group,
    required this.icon,
    required this.room,
    required this.on,
    this.watts = 0,
    this.wh = 0,
  });

  final String id;
  final String name;

  /// "switch" (relay), "lock" (on = locked) or "pir" (on = motion, read-only).
  final String type;
  final String group;
  final String icon;
  final int room;
  final bool on;
  final double watts; // typical load, used for the energy estimate
  final double wh; // energy used since the ESP32 booted

  bool get isLock => type == 'lock';
  bool get isSwitch => type == 'switch';
  bool get controllable => type == 'switch' || type == 'lock';

  Esp32Device copyWith({bool? on}) => Esp32Device(
        id: id,
        name: name,
        type: type,
        group: group,
        icon: icon,
        room: room,
        on: on ?? this.on,
        watts: watts,
        wh: wh,
      );

  factory Esp32Device.fromJson(Map<String, dynamic> j) => Esp32Device(
        id: (j['id'] as String?) ?? '',
        name: (j['name'] as String?) ?? (j['id'] as String?) ?? 'Device',
        type: (j['type'] as String?) ?? 'switch',
        group: (j['group'] as String?) ?? 'Other',
        icon: (j['icon'] as String?) ?? 'plug',
        room: _int(j['room']) ?? 0,
        on: j['on'] == true || j['on'] == 1,
        watts: _num(j['watts']) ?? 0,
        wh: _num(j['wh']) ?? 0,
      );
}

class Esp32State {
  const Esp32State({
    required this.name,
    required this.fw,
    required this.ssid,
    required this.ip,
    required this.rssi,
    required this.uptime,
    required this.temp,
    required this.hum,
    required this.dnd,
    required this.camera,
    required this.powerW,
    required this.devices,
  });

  final String name;
  final String fw;
  final String ssid;
  final String ip;
  final int rssi;
  final int uptime; // seconds
  final double? temp; // null = no sensor / read failed
  final double? hum;
  final bool dnd;
  final String camera; // stream URL, empty = not configured
  final double powerW;
  final List<Esp32Device> devices;

  Esp32State withDevice(String id, {required bool on}) => _copy(
        devices: [for (final d in devices) d.id == id ? d.copyWith(on: on) : d],
      );

  Esp32State withDnd(bool value) => _copy(dnd: value);

  Esp32State _copy({bool? dnd, List<Esp32Device>? devices}) => Esp32State(
        name: name,
        fw: fw,
        ssid: ssid,
        ip: ip,
        rssi: rssi,
        uptime: uptime,
        temp: temp,
        hum: hum,
        dnd: dnd ?? this.dnd,
        camera: camera,
        powerW: powerW,
        devices: devices ?? this.devices,
      );

  factory Esp32State.fromJson(Map<String, dynamic> j) {
    final raw = j['devices'];
    final list = <Esp32Device>[
      if (raw is List)
        for (final item in raw)
          if (item is Map) Esp32Device.fromJson(Map<String, dynamic>.from(item)),
    ].where((d) => d.id.isNotEmpty).toList();
    return Esp32State(
      name: (j['name'] as String?) ?? 'ESP32',
      fw: (j['fw'] as String?) ?? '',
      ssid: (j['ssid'] as String?) ?? '',
      ip: (j['ip'] as String?) ?? '',
      rssi: _int(j['rssi']) ?? 0,
      uptime: _int(j['uptime']) ?? 0,
      temp: _num(j['temp']),
      hum: _num(j['hum']),
      dnd: j['dnd'] == true,
      camera: (j['camera'] as String?) ?? '',
      powerW: _num(j['powerW']) ?? 0,
      devices: list,
    );
  }
}
