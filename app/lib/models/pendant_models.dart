enum PendantConnection { pairing, disconnected, connecting, connected }

enum AssistantPhase { idle, listening, transcribing, thinking, speaking, saved, error }

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
      phase: phaseName == null ? null : AssistantPhase.values.byName(phaseName),
      note: map['note'] is Map<Object?, Object?>
          ? NoteSummary.fromMap(map['note'] as Map<Object?, Object?>)
          : null,
    );
  }

  final String type;
  final String message;
  final PendantConnection? connection;
  final AssistantPhase? phase;
  final NoteSummary? note;
}
