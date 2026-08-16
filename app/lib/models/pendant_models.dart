enum PendantConnection { pairing, disconnected, connecting, connected }

enum VisualProfile { moonLoop, moonStill, orbitalLoop, orbitalStill }

extension VisualProfileWire on VisualProfile {
  int get wireValue => index;
  String get label => switch (this) {
    VisualProfile.moonLoop => 'Moon loop',
    VisualProfile.moonStill => 'Moon still',
    VisualProfile.orbitalLoop => 'Orbital loop',
    VisualProfile.orbitalStill => 'Orbital still',
  };
}

class CloudVoiceSettings {
  const CloudVoiceSettings({
    required this.mode,
    required this.voice,
    required this.hasApiKey,
    required this.visualProfile,
    required this.moonPhase,
  });

  factory CloudVoiceSettings.fromMap(Map<Object?, Object?> map) =>
      CloudVoiceSettings(
        mode: map['mode'] as String? ?? 'cloud',
        voice: map['voice'] as String? ?? '',
        hasApiKey: map['hasApiKey'] == true,
        visualProfile: VisualProfile.values.elementAt(
          ((map['visualProfile'] as num?)?.toInt() ?? 0).clamp(0, 3).toInt(),
        ),
        moonPhase: ((map['moonPhase'] as num?)?.toInt() ?? 100)
            .clamp(0, 100)
            .toInt(),
      );

  final String mode;
  final String voice;
  final bool hasApiKey;
  final VisualProfile visualProfile;
  final int moonPhase;
}

class ModelPackStatus {
  const ModelPackStatus({
    required this.ready,
    required this.missing,
    required this.installedBytes,
    required this.downloading,
    required this.downloadMessage,
    required this.downloadedBytes,
    required this.totalBytes,
  });

  factory ModelPackStatus.fromMap(Map<Object?, Object?> map) {
    final rawMissing = map['missing'];
    return ModelPackStatus(
      ready: map['ready'] == true,
      missing: rawMissing is Iterable<Object?>
          ? rawMissing.whereType<String>().toList(growable: false)
          : const <String>[],
      installedBytes: (map['installedBytes'] as num?)?.toInt() ?? 0,
      downloading: map['downloading'] == true,
      downloadMessage: map['downloadMessage'] as String? ?? '',
      downloadedBytes: (map['downloadedBytes'] as num?)?.toInt() ?? 0,
      totalBytes: (map['totalBytes'] as num?)?.toInt() ?? 0,
    );
  }

  final bool ready;
  final List<String> missing;
  final int installedBytes;
  final bool downloading;
  final String downloadMessage;
  final int downloadedBytes;
  final int totalBytes;
}

enum AssistantPhase {
  idle,
  listening,
  transcribing,
  thinking,
  speaking,
  saved,
  error,
}

class ActivityLogEntry {
  const ActivityLogEntry({
    required this.at,
    required this.category,
    required this.message,
  });

  final DateTime at;
  final String category;
  final String message;
}

class NoteSummary {
  const NoteSummary({
    required this.id,
    required this.title,
    required this.summary,
    required this.createdAt,
    required this.tags,
  });

  factory NoteSummary.fromMap(Map<Object?, Object?> map) {
    return NoteSummary(
      id: map['id'] as String,
      title: map['title'] as String,
      summary: map['summary'] as String,
      createdAt: DateTime.parse(map['createdAt'] as String),
      tags: (map['tags'] as List<Object?>? ?? const <Object?>[]).cast<String>(),
    );
  }

  final String id;
  final String title;
  final String summary;
  final DateTime createdAt;
  final List<String> tags;
}

class PendantEvent {
  const PendantEvent({
    required this.type,
    this.message = '',
    this.connection,
    this.phase,
    this.note,
    this.visualProfile,
    this.moonPhase,
  });

  factory PendantEvent.fromMap(Map<Object?, Object?> map) {
    final connectionName = map['connection'] as String?;
    final phaseName = map['phase'] as String?;
    return PendantEvent(
      type: map['type'] as String? ?? 'unknown',
      message: map['message'] as String? ?? '',
      connection: connectionName == null
          ? null
          : PendantConnection.values.byName(connectionName),
      // The service has a few informational/transitional phase names. Keep
      // rendering its event stream even if a newer firmware sends one the UI
      // does not yet model explicitly.
      phase: _phaseOrNull(phaseName),
      note: map['note'] is Map<Object?, Object?>
          ? NoteSummary.fromMap(map['note'] as Map<Object?, Object?>)
          : null,
      visualProfile: map['visualProfile'] is num
          ? VisualProfile.values.elementAt(
              (map['visualProfile'] as num).toInt().clamp(0, 3).toInt(),
            )
          : null,
      moonPhase: map['moonPhase'] is num
          ? (map['moonPhase'] as num).toInt().clamp(0, 100).toInt()
          : null,
    );
  }

  final String type;
  final String message;
  final PendantConnection? connection;
  final AssistantPhase? phase;
  final NoteSummary? note;
  final VisualProfile? visualProfile;
  final int? moonPhase;
}

AssistantPhase? _phaseOrNull(String? name) {
  if (name == null) return null;
  for (final phase in AssistantPhase.values) {
    if (phase.name == name) return phase;
  }
  return null;
}
