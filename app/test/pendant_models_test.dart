import 'package:flutter_test/flutter_test.dart';
import 'package:pendant_app/models/pendant_models.dart';

void main() {
  test('decodes a note event from the native bridge', () {
    final event = PendantEvent.fromMap(<Object?, Object?>{
      'type': 'noteSaved',
      'message': 'Saved',
      'connection': 'connected',
      'phase': 'saved',
      'note': <Object?, Object?>{
        'id': 'note-1',
        'title': 'Buy milk',
        'summary': 'Remember to buy milk.',
        'createdAt': '2026-08-06T12:00:00.000Z',
        'tags': <Object?>['home'],
      },
    });

    expect(event.connection, PendantConnection.connected);
    expect(event.phase, AssistantPhase.saved);
    expect(event.note?.title, 'Buy milk');
  });
}
