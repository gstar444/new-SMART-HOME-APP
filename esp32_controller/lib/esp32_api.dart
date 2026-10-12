import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/widgets.dart';

import 'models.dart';
import 'settings.dart';
import 'strings.dart';

enum ApiErrorKind { network, auth, server, firmware }

class ApiException implements Exception {
  ApiException(this.kind, [this.detail]);

  final ApiErrorKind kind;
  final String? detail;

  @override
  String toString() => 'ApiException($kind, $detail)';
}

/// Talks to the ESP32 firmware over plain HTTP on the local network.
///
///   GET  /api/state                     -> everything (sensors, devices, wifi)
///   POST /api/set?id=light&on=1         -> turn one relay on / off
///   POST /api/dnd?on=1                  -> Do-Not-Disturb flag
///   POST /api/scene?id=alloff|goodnight|dinner
///
/// The controller polls /api/state every few seconds, applies changes
/// optimistically (the tile reacts instantly) and rolls back on failure.
class Esp32Controller extends ChangeNotifier with WidgetsBindingObserver {
  Esp32Controller(this._settings) {
    _applied = _settings.value;
    _settings.addListener(_onSettings);
    WidgetsBinding.instance.addObserver(this);
    _startTimer();
    refresh();
  }

  final SettingsController _settings;
  late AppSettings _applied;
  final HttpClient _http = HttpClient()
    ..connectionTimeout = const Duration(seconds: 4);

  Timer? _timer;
  bool _refreshing = false;
  bool _disposed = false;
  int _failures = 0;
  DateTime _lastWrite = DateTime.fromMillisecondsSinceEpoch(0);
  DateTime? _lastSample;

  Esp32State? _state;
  bool _online = false;
  bool _loaded = false; // true after the first answer *or* the first failure
  String? _error;
  final Set<String> _pending = <String>{};
  final List<Map<String, double>> _samples = <Map<String, double>>[];

  Esp32State? get state => _state;
  bool get online => _online;
  bool get loaded => _loaded;
  String? get error => _error;
  bool isBusy(String key) => _pending.contains(key);

  /// Recent power samples (one every ~15 s, newest last): device id -> watts.
  /// The dashboard sums them per room to draw the small bar chart.
  List<Map<String, double>> get samples => List.unmodifiable(_samples);

  Tr get _tr => Tr.of(_settings.value);

  // ------------------------------------------------------------ lifecycle
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _startTimer();
      refresh();
    } else if (state == AppLifecycleState.paused) {
      _timer?.cancel();
    }
  }

  void _startTimer() {
    _timer?.cancel();
    _timer = Timer.periodic(
      Duration(seconds: _settings.value.pollSeconds),
      (_) => refresh(),
    );
  }

  void _onSettings() {
    final prev = _applied;
    final next = _settings.value;
    _applied = next;
    if (prev.activeUrl != next.activeUrl || prev.apiKey != next.apiKey) {
      _state = null;
      _online = false;
      _loaded = false;
      _error = null;
      _failures = 0;
      _samples.clear();
      _lastSample = null;
      notifyListeners();
      refresh();
    }
    if (prev.pollSeconds != next.pollSeconds) _startTimer();
  }

  @override
  void dispose() {
    _disposed = true;
    _timer?.cancel();
    _settings.removeListener(_onSettings);
    WidgetsBinding.instance.removeObserver(this);
    _http.close(force: true);
    super.dispose();
  }

  // -------------------------------------------------------------- polling
  Future<void> refresh() async {
    if (_refreshing || _disposed) return;
    _refreshing = true;
    final startedAt = DateTime.now();
    final urlAtStart = _settings.value.activeUrl;
    try {
      final json = await _call('GET', 'state');
      if (urlAtStart == _settings.value.activeUrl) {
        final next = Esp32State.fromJson(json);
        // An answer that left before our latest write may still contain the
        // old value, so it must not overwrite the optimistic state.
        if (!startedAt.isBefore(_lastWrite)) {
          _state = next;
          _recordSample(next);
        }
        _online = true;
        _failures = 0;
        _error = null;
      }
    } catch (e) {
      if (urlAtStart == _settings.value.activeUrl) {
        _failures++;
        _error = _friendly(e);
        if (_failures >= 2 || _state == null) _online = false;
      }
    } finally {
      _refreshing = false;
      if (urlAtStart == _settings.value.activeUrl) _loaded = true;
      if (!_disposed) notifyListeners();
    }
  }

  void _recordSample(Esp32State s) {
    final now = DateTime.now();
    final last = _lastSample;
    if (last != null && now.difference(last) < const Duration(seconds: 15)) {
      return;
    }
    _lastSample = now;
    _samples.add({
      for (final d in s.devices)
        if (d.on && d.watts > 0) d.id: d.watts,
    });
    if (_samples.length > 20) _samples.removeAt(0);
  }

  // --------------------------------------------------------------- writes
  /// Returns null on success, otherwise a message that can be shown to the user.
  Future<String?> setDevice(String id, bool on) => _write(
        'set',
        {'id': id, 'on': on ? '1' : '0'},
        busyKey: id,
        optimistic: (s) => s.withDevice(id, on: on),
        revert: (s) => s.withDevice(id, on: !on),
      );

  Future<String?> setDnd(bool on) => _write(
        'dnd',
        {'on': on ? '1' : '0'},
        busyKey: 'dnd',
        optimistic: (s) => s.withDnd(on),
        revert: (s) => s.withDnd(!on),
      );

  Future<String?> runScene(String id) =>
      _write('scene', {'id': id}, busyKey: 'scene:$id');

  Future<String?> _write(
    String name,
    Map<String, String> query, {
    required String busyKey,
    Esp32State Function(Esp32State s)? optimistic,
    Esp32State Function(Esp32State s)? revert,
  }) async {
    final before = _state;
    if (before == null) return _tr.notConnected;
    _pending.add(busyKey);
    if (optimistic != null) _state = optimistic(before);
    _lastWrite = DateTime.now();
    notifyListeners();
    try {
      final json = await _call('POST', name, query);
      if (json['ok'] == false) {
        throw ApiException(ApiErrorKind.server, json['error']?.toString());
      }
      _online = true;
      _failures = 0;
      _error = null;
      return null;
    } catch (e) {
      final current = _state;
      if (revert != null && current != null) _state = revert(current);
      return _friendly(e);
    } finally {
      _pending.remove(busyKey);
      _lastWrite = DateTime.now();
      if (!_disposed) notifyListeners();
      // Re-read the real state shortly after, so the UI always converges.
      unawaited(Future<void>.delayed(const Duration(milliseconds: 300), refresh));
    }
  }

  // ----------------------------------------------------------------- http
  Future<Map<String, dynamic>> _call(
    String method,
    String name, [
    Map<String, String>? query,
  ]) async {
    try {
      final base = Uri.parse(_settings.value.activeUrl);
      final uri = base.replace(path: '/api/$name', queryParameters: query);
      final req = await _http
          .openUrl(method, uri)
          .timeout(const Duration(seconds: 5));
      final key = _settings.value.apiKey;
      if (key.isNotEmpty) req.headers.set('X-API-Key', key);
      req.headers.set(HttpHeaders.acceptHeader, 'application/json');
      if (method != 'GET') req.contentLength = 0;
      final res = await req.close().timeout(const Duration(seconds: 6));
      final body = await res
          .transform(utf8.decoder)
          .join()
          .timeout(const Duration(seconds: 6));

      if (res.statusCode == 401 || res.statusCode == 403) {
        throw ApiException(ApiErrorKind.auth);
      }
      Map<String, dynamic>? json;
      try {
        final decoded = jsonDecode(body);
        if (decoded is Map<String, dynamic>) json = decoded;
      } catch (_) {
        // handled below
      }
      // The ESP32 answered, but with a web page / 404 page instead of JSON:
      // it is still running an old firmware that has no /api/ endpoints.
      if (json == null && (res.statusCode == 404 || res.statusCode == 200)) {
        throw ApiException(ApiErrorKind.firmware);
      }
      if (res.statusCode < 200 || res.statusCode >= 300) {
        throw ApiException(
          ApiErrorKind.server,
          json?['error']?.toString() ?? 'HTTP ${res.statusCode}',
        );
      }
      if (json == null) throw ApiException(ApiErrorKind.firmware);
      return json;
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(ApiErrorKind.network, 'timeout');
    } on SocketException catch (e) {
      throw ApiException(ApiErrorKind.network, e.message);
    } on HttpException catch (e) {
      throw ApiException(ApiErrorKind.network, e.message);
    } on FormatException catch (e) {
      throw ApiException(ApiErrorKind.network, e.message);
    }
  }

  String _friendly(Object e) {
    final tr = _tr;
    if (e is ApiException) {
      return switch (e.kind) {
        ApiErrorKind.auth => tr.errAuth,
        ApiErrorKind.server => e.detail ?? tr.errServer,
        ApiErrorKind.firmware => tr.errFirmware,
        ApiErrorKind.network => tr.errNetwork,
      };
    }
    return tr.errNetwork;
  }
}
