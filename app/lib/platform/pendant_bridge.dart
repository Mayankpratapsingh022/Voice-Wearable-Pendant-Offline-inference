import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import '../models/pendant_models.dart';

class PendantBridge {
  static const _methods = MethodChannel('ai.pendant/service');
  static const _events = EventChannel('ai.pendant/events');

  Stream<PendantEvent> get events => _events
      .receiveBroadcastStream()
      .where((Object? event) => event is Map<Object?, Object?>)
      .cast<Map<Object?, Object?>>()
      .map(PendantEvent.fromMap);

  Future<void> start() => _methods.invokeMethod<void>('start');

  Future<void> requestBluetoothPermissions() =>
      _methods.invokeMethod<void>('requestBluetoothPermissions');

  Future<void> startPairing() => _methods.invokeMethod<void>('startPairing');

  Future<void> provisionWifi(String ssid, String password) =>
      _methods.invokeMethod<void>('provisionWifi', <String, Object?>{
        'ssid': ssid,
        'password': password,
      });

  Future<List<NoteSummary>> listNotes({String query = ''}) async {
    final result = await _methods.invokeListMethod<Map<Object?, Object?>>(
      'listNotes',
      <String, Object?>{'query': query},
    );
    return (result ?? const <Map<Object?, Object?>>[])
        .map(NoteSummary.fromMap)
        .toList(growable: false);
  }

  Future<ModelPackStatus> modelStatus() async {
    final result = await _methods.invokeMapMethod<Object?, Object?>(
      'modelStatus',
    );
    return ModelPackStatus.fromMap(result ?? const <Object?, Object?>{});
  }

  Future<void> downloadModels() =>
      _methods.invokeMethod<void>('downloadModels');
}

class PendantController extends ChangeNotifier {
  PendantController(this._bridge);

  final PendantBridge _bridge;
  StreamSubscription<PendantEvent>? _subscription;
  PendantConnection connection = PendantConnection.disconnected;
  AssistantPhase phase = AssistantPhase.idle;
  String message = 'Start the companion service to pair your pendant.';
  List<NoteSummary> notes = const <NoteSummary>[];
  ModelPackStatus? modelStatus;
  List<ActivityLogEntry> activity = const <ActivityLogEntry>[];

  Future<void> initialize() async {
    _subscription ??= _bridge.events.listen(
      _onEvent,
      onError: (Object error) {
        message = 'Service error: $error';
        phase = AssistantPhase.error;
        notifyListeners();
      },
    );
    await _bridge.requestBluetoothPermissions();
    await _bridge.start();
    await Future.wait(<Future<void>>[refreshNotes(), refreshModelStatus()]);
  }

  Future<void> pair() => _bridge.startPairing();

  Future<void> provisionWifi(String ssid, String password) =>
      _bridge.provisionWifi(ssid, password);

  Future<void> refreshNotes([String query = '']) async {
    notes = await _bridge.listNotes(query: query);
    notifyListeners();
  }

  Future<void> refreshModelStatus() async {
    modelStatus = await _bridge.modelStatus();
    notifyListeners();
  }

  Future<void> downloadModels() async {
    await _bridge.downloadModels();
    await refreshModelStatus();
  }

  void _onEvent(PendantEvent event) {
    if (event.type == 'model') {
      unawaited(refreshModelStatus());
    }
    connection = event.connection ?? connection;
    phase = event.phase ?? phase;
    if (event.message.isNotEmpty) {
      message = event.message;
      activity = <ActivityLogEntry>[
        ActivityLogEntry(
          at: DateTime.now(),
          category: event.type,
          message: event.message,
        ),
        ...activity,
      ].take(30).toList(growable: false);
    }
    if (event.note != null) {
      notes = <NoteSummary>[event.note!, ...notes];
    }
    notifyListeners();
  }

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }
}
