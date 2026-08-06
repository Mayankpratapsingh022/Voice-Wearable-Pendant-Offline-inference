import 'package:flutter/material.dart';

import '../models/pendant_models.dart';
import '../platform/pendant_bridge.dart';

class PendantApp extends StatelessWidget {
  const PendantApp({required this.controller, super.key});

  final PendantController controller;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Pendant',
      theme: ThemeData(
        brightness: Brightness.dark,
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xff56e5ff),
          brightness: Brightness.dark,
        ),
        scaffoldBackgroundColor: Colors.black,
        useMaterial3: true,
      ),
      home: PendantHome(controller: controller),
    );
  }
}

class PendantHome extends StatefulWidget {
  const PendantHome({required this.controller, super.key});

  final PendantController controller;

  @override
  State<PendantHome> createState() => _PendantHomeState();
}

class _PendantHomeState extends State<PendantHome> {
  late final PendantController controller = widget.controller;
  final searchController = TextEditingController();
  late final Future<void> bootstrap = controller.initialize();

  @override
  void dispose() {
    searchController.dispose();
    controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: controller,
      builder: (context, child) => Scaffold(
        appBar: AppBar(
          title: const Text('Pendant'),
          actions: <Widget>[
            IconButton(
              tooltip: 'Refresh notes',
              onPressed: () => controller.refreshNotes(searchController.text),
              icon: const Icon(Icons.refresh),
            ),
          ],
        ),
        body: FutureBuilder<void>(
          future: bootstrap,
          builder: (context, snapshot) => ListView(
            padding: const EdgeInsets.all(20),
            children: <Widget>[
              _CompanionCard(controller: controller),
              const SizedBox(height: 28),
              TextField(
                controller: searchController,
                onSubmitted: controller.refreshNotes,
                decoration: InputDecoration(
                  labelText: 'Search saved notes',
                  prefixIcon: const Icon(Icons.search),
                  suffixIcon: IconButton(
                    icon: const Icon(Icons.arrow_forward),
                    onPressed: () => controller.refreshNotes(searchController.text),
                  ),
                ),
              ),
              const SizedBox(height: 20),
              Text('Saved notes', style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 8),
              if (snapshot.hasError)
                Text('Could not start the service: ${snapshot.error}'),
              if (controller.notes.isEmpty)
                const Padding(
                  padding: EdgeInsets.only(top: 28),
                  child: Text('No saved notes yet. Say “remember …” through the pendant.'),
                )
              else
                ...controller.notes.map(_NoteTile.new),
            ],
          ),
        ),
      ),
    );
  }
}

class _CompanionCard extends StatelessWidget {
  const _CompanionCard({required this.controller});

  final PendantController controller;

  @override
  Widget build(BuildContext context) {
    final connected = controller.connection == PendantConnection.connected;
    final color = connected ? const Color(0xff56e5ff) : const Color(0xff6e8085);
    return Container(
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        color: const Color(0xff071013),
        borderRadius: BorderRadius.circular(32),
        border: Border.all(color: color.withValues(alpha: 0.45)),
      ),
      child: Column(
        children: <Widget>[
          _FacePreview(color: color, phase: controller.phase),
          const SizedBox(height: 18),
          Text(
            _title(controller.connection, controller.phase),
            style: Theme.of(context).textTheme.headlineSmall,
          ),
          const SizedBox(height: 8),
          Text(controller.message, textAlign: TextAlign.center),
          const SizedBox(height: 20),
          FilledButton.icon(
            onPressed: connected ? null : controller.pair,
            icon: const Icon(Icons.bluetooth_searching),
            label: Text(connected ? 'Pendant connected' : 'Pair pendant'),
          ),
        ],
      ),
    );
  }

  static String _title(PendantConnection connection, AssistantPhase phase) {
    if (connection != PendantConnection.connected) {
      return connection == PendantConnection.pairing ? 'Pairing' : 'Not connected';
    }
    return switch (phase) {
      AssistantPhase.idle => 'Ready to listen',
      AssistantPhase.listening => 'Listening',
      AssistantPhase.transcribing => 'Transcribing',
      AssistantPhase.thinking => 'Thinking',
      AssistantPhase.speaking => 'Speaking',
      AssistantPhase.saved => 'Note saved',
      AssistantPhase.error => 'Needs attention',
    };
  }
}

class _FacePreview extends StatelessWidget {
  const _FacePreview({required this.color, required this.phase});

  final Color color;
  final AssistantPhase phase;

  @override
  Widget build(BuildContext context) {
    final mouthHeight = phase == AssistantPhase.speaking ? 26.0 : 8.0;
    return SizedBox(
      height: 136,
      width: 190,
      child: Stack(
        alignment: Alignment.center,
        children: <Widget>[
          if (phase == AssistantPhase.listening || phase == AssistantPhase.thinking)
            Container(
              width: 136,
              height: 136,
              decoration: BoxDecoration(
                border: Border.all(color: color.withValues(alpha: 0.55), width: 2),
                shape: BoxShape.circle,
              ),
            ),
          Positioned(top: 44, left: 34, child: _Eye(color: color)),
          Positioned(top: 44, right: 34, child: _Eye(color: color)),
          Positioned(
            bottom: 27,
            child: AnimatedContainer(
              duration: const Duration(milliseconds: 90),
              width: 44,
              height: mouthHeight,
              decoration: BoxDecoration(color: color, borderRadius: BorderRadius.circular(20)),
            ),
          ),
        ],
      ),
    );
  }
}

class _Eye extends StatelessWidget {
  const _Eye({required this.color});

  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 42,
      height: 23,
      decoration: BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(20),
        boxShadow: <BoxShadow>[BoxShadow(color: color.withValues(alpha: 0.55), blurRadius: 18)],
      ),
    );
  }
}

class _NoteTile extends StatelessWidget {
  const _NoteTile(this.note);

  final NoteSummary note;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: ListTile(
        title: Text(note.title),
        subtitle: Text(note.summary, maxLines: 2, overflow: TextOverflow.ellipsis),
        trailing: Text('${note.createdAt.month}/${note.createdAt.day}'),
      ),
    );
  }
}
