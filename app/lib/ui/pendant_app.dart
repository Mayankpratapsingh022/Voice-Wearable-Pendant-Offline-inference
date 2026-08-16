import 'dart:io';

import 'package:camera/camera.dart';
import 'package:flutter/material.dart';
import 'package:mobile_scanner/mobile_scanner.dart';

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
  final hotspotNameController = TextEditingController();
  final hotspotPasswordController = TextEditingController();
  final sarvamKeyController = TextEditingController();
  late final Future<void> bootstrap = controller.initialize();

  @override
  void dispose() {
    searchController.dispose();
    hotspotNameController.dispose();
    hotspotPasswordController.dispose();
    sarvamKeyController.dispose();
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
              const SizedBox(height: 16),
              _WifiFallbackSetup(
                connected: controller.connection == PendantConnection.connected,
                ssidController: hotspotNameController,
                passwordController: hotspotPasswordController,
                onProvision: controller.provisionWifi,
              ),
              const SizedBox(height: 16),
              _ScreensaverPanel(controller: controller),
              const SizedBox(height: 16),
              _CloudVoiceSettingsPanel(
                controller: controller,
                sarvamKeyController: sarvamKeyController,
              ),
              const SizedBox(height: 16),
              _LivePipeline(controller: controller),
              const SizedBox(height: 16),
              _ModelManager(
                status: controller.modelStatus,
                onRefresh: controller.refreshModelStatus,
                onDownload: controller.downloadModels,
              ),
              const SizedBox(height: 28),
              TextField(
                controller: searchController,
                onSubmitted: controller.refreshNotes,
                decoration: InputDecoration(
                  labelText: 'Search saved notes',
                  prefixIcon: const Icon(Icons.search),
                  suffixIcon: IconButton(
                    icon: const Icon(Icons.arrow_forward),
                    onPressed: () =>
                        controller.refreshNotes(searchController.text),
                  ),
                ),
              ),
              const SizedBox(height: 20),
              Text(
                'Saved notes',
                style: Theme.of(context).textTheme.titleLarge,
              ),
              const SizedBox(height: 8),
              if (snapshot.hasError)
                Text('Could not start the service: ${snapshot.error}'),
              if (controller.notes.isEmpty)
                const Padding(
                  padding: EdgeInsets.only(top: 28),
                  child: Text(
                    'No saved notes yet. Say “remember …” through the pendant.',
                  ),
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

class _WifiFallbackSetup extends StatelessWidget {
  const _WifiFallbackSetup({
    required this.connected,
    required this.ssidController,
    required this.passwordController,
    required this.onProvision,
  });

  final bool connected;
  final TextEditingController ssidController;
  final TextEditingController passwordController;
  final Future<void> Function(String ssid, String password) onProvision;

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(20),
    decoration: BoxDecoration(
      color: const Color(0xff071013),
      borderRadius: BorderRadius.circular(24),
      border: Border.all(color: const Color(0xff56e5ff).withValues(alpha: .35)),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        const Row(
          children: <Widget>[
            Icon(Icons.wifi_rounded, color: Color(0xff56e5ff)),
            SizedBox(width: 10),
            Text('Phone hotspot voice link'),
          ],
        ),
        const SizedBox(height: 8),
        const Text(
          'Turn on your Android Personal Hotspot, then enter its name and password once. The pendant joins it and audio moves from BLE to Wi‑Fi.',
        ),
        const SizedBox(height: 14),
        TextField(
          controller: ssidController,
          textInputAction: TextInputAction.next,
          decoration: const InputDecoration(
            labelText: 'Hotspot name (SSID)',
            prefixIcon: Icon(Icons.wifi),
          ),
        ),
        const SizedBox(height: 10),
        TextField(
          controller: passwordController,
          obscureText: true,
          onSubmitted: (_) => _send(context),
          decoration: const InputDecoration(
            labelText: 'Hotspot password',
            prefixIcon: Icon(Icons.lock_outline),
          ),
        ),
        const SizedBox(height: 14),
        FilledButton.icon(
          onPressed: connected ? () => _send(context) : null,
          icon: const Icon(Icons.send_rounded),
          label: const Text('Connect pendant to hotspot'),
        ),
        const SizedBox(height: 8),
        const Text(
          'The password is sent only to the paired pendant and is not stored by the phone app.',
          style: TextStyle(color: Colors.white60, fontSize: 12),
        ),
      ],
    ),
  );

  Future<void> _send(BuildContext context) async {
    final ssid = ssidController.text.trim();
    if (ssid.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Enter your phone hotspot name first.')),
      );
      return;
    }
    await onProvision(ssid, passwordController.text);
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Sending hotspot setup to pendant…')),
      );
    }
  }
}

class _ScreensaverPanel extends StatefulWidget {
  const _ScreensaverPanel({required this.controller});
  final PendantController controller;

  @override
  State<_ScreensaverPanel> createState() => _ScreensaverPanelState();
}

class _ScreensaverPanelState extends State<_ScreensaverPanel> {
  late double moonPhase = widget.controller.moonPhase.toDouble();

  @override
  Widget build(BuildContext context) {
    final selected = widget.controller.visualProfile;
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0xff071013),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(
          color: const Color(0xffdbe7ff).withValues(alpha: .28),
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          const Row(
            children: <Widget>[
              Icon(Icons.auto_awesome_rounded, color: Color(0xffdbe7ff)),
              SizedBox(width: 10),
              Text('Pendant screensaver'),
            ],
          ),
          const SizedBox(height: 8),
          const Text(
            'Choose the exact idle visual shown on the pendant. Listening and speaking always use the original orbital strands with only subtle brightness and speed response.',
          ),
          const SizedBox(height: 16),
          GridView.count(
            crossAxisCount: 2,
            shrinkWrap: true,
            physics: const NeverScrollableScrollPhysics(),
            crossAxisSpacing: 12,
            mainAxisSpacing: 12,
            childAspectRatio: .92,
            children: <Widget>[
              _VisualChoice(
                profile: VisualProfile.moonLoop,
                selected: selected,
                asset: 'assets/visuals/moon-loop.gif',
                onTap: _select,
              ),
              _VisualChoice(
                profile: VisualProfile.moonStill,
                selected: selected,
                asset: 'assets/visuals/moon-still.png',
                onTap: _select,
              ),
              _VisualChoice(
                profile: VisualProfile.orbitalLoop,
                selected: selected,
                asset: 'assets/visuals/orbital-loop.gif',
                onTap: _select,
              ),
              _VisualChoice(
                profile: VisualProfile.orbitalStill,
                selected: selected,
                asset: 'assets/visuals/orbital-still.png',
                onTap: _select,
              ),
            ],
          ),
          if (selected == VisualProfile.moonStill) ...<Widget>[
            const SizedBox(height: 18),
            Text(
              'Moon phase — ${_moonLabel(moonPhase)}',
              style: Theme.of(context).textTheme.titleSmall,
            ),
            Slider(
              value: moonPhase,
              min: 0,
              max: 100,
              divisions: 100,
              label: _moonLabel(moonPhase),
              onChanged: (value) => setState(() => moonPhase = value),
              onChangeEnd: (value) => widget.controller.setVisualProfile(
                VisualProfile.moonStill,
                moonPhase: value.round(),
              ),
            ),
            const Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: <Widget>[
                Text('Crescent'),
                Text('Half'),
                Text('Gibbous'),
                Text('Full'),
              ],
            ),
          ],
        ],
      ),
    );
  }

  Future<void> _select(VisualProfile profile) async {
    if (profile == VisualProfile.moonStill)
      moonPhase = widget.controller.moonPhase.toDouble();
    setState(() {});
    await widget.controller.setVisualProfile(
      profile,
      moonPhase: moonPhase.round(),
    );
  }

  static String _moonLabel(double value) {
    if (value < 25) return 'Crescent';
    if (value < 55) return 'Half moon';
    if (value < 85) return 'Gibbous';
    return 'Full moon';
  }
}

class _VisualChoice extends StatelessWidget {
  const _VisualChoice({
    required this.profile,
    required this.selected,
    required this.asset,
    required this.onTap,
  });
  final VisualProfile profile;
  final VisualProfile selected;
  final String asset;
  final Future<void> Function(VisualProfile) onTap;

  @override
  Widget build(BuildContext context) {
    final active = profile == selected;
    final color = active ? const Color(0xff56e5ff) : Colors.white24;
    return InkWell(
      onTap: () => onTap(profile),
      borderRadius: BorderRadius.circular(18),
      child: Container(
        decoration: BoxDecoration(
          color: Colors.black,
          borderRadius: BorderRadius.circular(18),
          border: Border.all(color: color, width: active ? 2 : 1),
        ),
        padding: const EdgeInsets.all(8),
        child: Column(
          children: <Widget>[
            Expanded(
              child: ClipOval(
                child: Image.asset(
                  asset,
                  fit: BoxFit.cover,
                  width: double.infinity,
                ),
              ),
            ),
            const SizedBox(height: 7),
            Text(
              profile.label,
              style: TextStyle(
                color: active ? const Color(0xff56e5ff) : Colors.white,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _CloudVoiceSettingsPanel extends StatefulWidget {
  const _CloudVoiceSettingsPanel({
    required this.controller,
    required this.sarvamKeyController,
  });
  final PendantController controller;
  final TextEditingController sarvamKeyController;

  @override
  State<_CloudVoiceSettingsPanel> createState() =>
      _CloudVoiceSettingsPanelState();
}

class _CloudVoiceSettingsPanelState extends State<_CloudVoiceSettingsPanel> {
  String mode = 'cloud';
  String voice = '';
  bool hydrated = false;
  static const voices = <String>[
    'ratan',
    'priya',
    'ishita',
    'shubh',
    'ritu',
    'mani',
  ];

  @override
  Widget build(BuildContext context) {
    final settings = widget.controller.cloudSettings;
    if (!hydrated && settings != null) {
      hydrated = true;
      mode = settings.mode;
      voice = settings.voice;
    }
    final cloudReady =
        settings != null && settings.hasApiKey && settings.voice.isNotEmpty;
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0xff0b0d11),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(
          color: cloudReady
              ? const Color(0xff52f39a).withValues(alpha: .55)
              : const Color(0xffffc857).withValues(alpha: .45),
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          const Row(
            children: <Widget>[
              Icon(Icons.cloud_rounded, color: Color(0xff56e5ff)),
              SizedBox(width: 10),
              Text('Cloud Voice — Sarvam'),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            cloudReady
                ? 'Cloud Voice ready — live Saaras STT, Sarvam-30B, and Bulbul v3.'
                : 'Add your Sarvam API key and choose a voice. The key stays encrypted on this phone.',
          ),
          const SizedBox(height: 14),
          SegmentedButton<String>(
            segments: const <ButtonSegment<String>>[
              ButtonSegment(value: 'cloud', label: Text('Cloud')),
              ButtonSegment(value: 'local', label: Text('Local')),
            ],
            selected: <String>{mode},
            onSelectionChanged: (value) => setState(() => mode = value.first),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: widget.sarvamKeyController,
            obscureText: true,
            decoration: InputDecoration(
              labelText: settings?.hasApiKey == true
                  ? 'Replace Sarvam API key (optional)'
                  : 'Sarvam API key',
            ),
          ),
          const SizedBox(height: 10),
          DropdownButtonFormField<String>(
            value: voice.isEmpty ? null : voice,
            decoration: const InputDecoration(labelText: 'Bulbul v3 voice'),
            items: voices
                .map((item) => DropdownMenuItem(value: item, child: Text(item)))
                .toList(),
            onChanged: (value) => setState(() => voice = value ?? ''),
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 10,
            runSpacing: 10,
            children: <Widget>[
              FilledButton.icon(
                onPressed: _save,
                icon: const Icon(Icons.lock_outline),
                label: const Text('Save secure settings'),
              ),
            ],
          ),
          const SizedBox(height: 8),
          const Text(
            'Local is reserved for the future native runtime. It does not pretend that downloaded model files can already process speech.',
            style: TextStyle(fontSize: 12, color: Colors.white60),
          ),
        ],
      ),
    );
  }

  Future<void> _save() async {
    await widget.controller.saveCloudSettings(
      mode: mode,
      voice: voice,
      apiKey: widget.sarvamKeyController.text.isEmpty
          ? null
          : widget.sarvamKeyController.text,
    );
    widget.sarvamKeyController.clear();
    if (mounted)
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Cloud Voice settings saved securely.')),
      );
  }
}

class _LivePipeline extends StatelessWidget {
  const _LivePipeline({required this.controller});

  final PendantController controller;

  @override
  Widget build(BuildContext context) {
    final phase = controller.phase;
    final connected = controller.connection == PendantConnection.connected;
    final modelFilesReady = controller.modelStatus?.ready == true;
    final theme = Theme.of(context);
    final mode = _pipelineMode(connected, phase);
    final modeColor = _modeColor(mode);
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0xff071013),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: modeColor.withValues(alpha: .62), width: 1.2),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          Row(
            children: <Widget>[
              Icon(Icons.graphic_eq_rounded, color: modeColor),
              const SizedBox(width: 10),
              Text('Live voice pipeline', style: theme.textTheme.titleLarge),
              const Spacer(),
              _StatePill(label: _modeLabel(mode), color: modeColor),
            ],
          ),
          const SizedBox(height: 14),
          _PipelineStep(
            icon: Icons.touch_app_rounded,
            title: 'Pendant trigger',
            detail: phase == AssistantPhase.idle
                ? 'Waiting for PWR press'
                : 'Request received',
            state: phase == AssistantPhase.idle
                ? _StepState.inactive
                : _StepState.done,
          ),
          _PipelineStep(
            icon: Icons.mic_rounded,
            title: 'Microphone',
            detail: phase == AssistantPhase.listening
                ? 'Capturing audio from pendant'
                : 'Waiting',
            state: phase == AssistantPhase.listening
                ? _StepState.active
                : _StepState.inactive,
          ),
          _PipelineStep(
            icon: Icons.record_voice_over_rounded,
            title: 'Moonshine STT',
            detail: _sttDetail(phase, modelFilesReady),
            state: _sttState(phase, modelFilesReady),
          ),
          _PipelineStep(
            icon: Icons.auto_awesome_rounded,
            title: 'Qwen local LLM',
            detail: _llmDetail(phase, modelFilesReady),
            state: _llmState(phase, modelFilesReady),
          ),
          _PipelineStep(
            icon: Icons.volume_up_rounded,
            title: 'Kokoro TTS',
            detail: phase == AssistantPhase.speaking
                ? 'Generating and streaming voice'
                : 'Waiting',
            state: phase == AssistantPhase.speaking
                ? _StepState.active
                : _StepState.inactive,
          ),
          const SizedBox(height: 10),
          _ActivityLog(entries: controller.activity),
        ],
      ),
    );
  }

  static String _sttDetail(AssistantPhase phase, bool filesReady) {
    if (!filesReady) return 'Model files not installed';
    if (phase == AssistantPhase.transcribing ||
        phase == AssistantPhase.thinking) {
      return 'Transcribing captured speech';
    }
    if (phase == AssistantPhase.error) return 'Waiting for native STT runtime';
    return 'Waiting';
  }

  static _StepState _sttState(AssistantPhase phase, bool filesReady) {
    if (!filesReady || phase == AssistantPhase.error) return _StepState.blocked;
    if (phase == AssistantPhase.transcribing ||
        phase == AssistantPhase.thinking) {
      return _StepState.processing;
    }
    return _StepState.inactive;
  }

  static String _llmDetail(AssistantPhase phase, bool filesReady) {
    if (!filesReady) return 'Model files not installed';
    if (phase == AssistantPhase.thinking) return 'Generating local reply';
    if (phase == AssistantPhase.error) return 'Waiting for native LLM runtime';
    return 'Waiting';
  }

  static _StepState _llmState(AssistantPhase phase, bool filesReady) {
    if (!filesReady || phase == AssistantPhase.error) return _StepState.blocked;
    return phase == AssistantPhase.thinking
        ? _StepState.processing
        : _StepState.inactive;
  }

  static _PipelineMode _pipelineMode(bool connected, AssistantPhase phase) {
    if (!connected || phase == AssistantPhase.error) {
      return _PipelineMode.inactive;
    }
    if (phase == AssistantPhase.transcribing ||
        phase == AssistantPhase.thinking) {
      return _PipelineMode.processing;
    }
    if (phase == AssistantPhase.listening || phase == AssistantPhase.speaking) {
      return _PipelineMode.active;
    }
    return _PipelineMode.inactive;
  }

  static Color _modeColor(_PipelineMode mode) => switch (mode) {
    _PipelineMode.active => const Color(0xff52f39a),
    _PipelineMode.processing => const Color(0xffffc857),
    _PipelineMode.inactive => const Color(0xffff6875),
  };

  static String _modeLabel(_PipelineMode mode) => switch (mode) {
    _PipelineMode.active => 'LISTENING / ACTIVE',
    _PipelineMode.processing => 'PROCESSING',
    _PipelineMode.inactive => 'INACTIVE',
  };
}

enum _PipelineMode { inactive, active, processing }

enum _StepState { inactive, active, processing, done, blocked }

class _PipelineStep extends StatelessWidget {
  const _PipelineStep({
    required this.icon,
    required this.title,
    required this.detail,
    required this.state,
  });

  final IconData icon;
  final String title;
  final String detail;
  final _StepState state;

  @override
  Widget build(BuildContext context) {
    final color = switch (state) {
      _StepState.active => const Color(0xff52f39a),
      _StepState.processing => const Color(0xffffc857),
      _StepState.done => const Color(0xff64efb2),
      _StepState.blocked => const Color(0xffff6875),
      _StepState.inactive => const Color(0xffff6875),
    };
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        children: <Widget>[
          Icon(icon, size: 19, color: color),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: <Widget>[
                Text(title),
                Text(
                  detail,
                  style: Theme.of(
                    context,
                  ).textTheme.bodySmall?.copyWith(color: color),
                ),
              ],
            ),
          ),
          Icon(
            state == _StepState.active
                ? Icons.radio_button_checked
                : state == _StepState.processing
                ? Icons.hourglass_top_rounded
                : state == _StepState.done
                ? Icons.check_circle
                : state == _StepState.blocked
                ? Icons.error_outline
                : Icons.circle_outlined,
            color: color,
            size: 18,
          ),
        ],
      ),
    );
  }
}

class _StatePill extends StatelessWidget {
  const _StatePill({required this.label, required this.color});
  final String label;
  final Color color;
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 5),
    decoration: BoxDecoration(
      color: color.withValues(alpha: .16),
      borderRadius: BorderRadius.circular(99),
    ),
    child: Text(
      label,
      style: Theme.of(
        context,
      ).textTheme.labelSmall?.copyWith(color: color, letterSpacing: 1.1),
    ),
  );
}

class _ActivityLog extends StatelessWidget {
  const _ActivityLog({required this.entries});
  final List<ActivityLogEntry> entries;
  @override
  Widget build(BuildContext context) => ExpansionTile(
    tilePadding: EdgeInsets.zero,
    title: const Text('Activity log'),
    subtitle: Text(
      entries.isEmpty
          ? 'Waiting for pendant activity'
          : '${entries.length} recent events',
    ),
    children: entries.isEmpty
        ? const <Widget>[
            Padding(
              padding: EdgeInsets.only(bottom: 12),
              child: Text(
                'Press the pendant PWR button to see the request flow here.',
              ),
            ),
          ]
        : entries
              .map(
                (entry) => ListTile(
                  dense: true,
                  leading: Text(
                    '${entry.at.hour.toString().padLeft(2, '0')}:${entry.at.minute.toString().padLeft(2, '0')}:${entry.at.second.toString().padLeft(2, '0')}',
                    style: Theme.of(context).textTheme.labelSmall,
                  ),
                  title: Text(entry.message),
                  subtitle: Text(entry.category.toUpperCase()),
                ),
              )
              .toList(growable: false),
  );
}

class _ModelManager extends StatelessWidget {
  const _ModelManager({
    required this.status,
    required this.onRefresh,
    required this.onDownload,
  });

  final ModelPackStatus? status;
  final Future<void> Function() onRefresh;
  final Future<void> Function() onDownload;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final currentStatus = status;
    final ready = currentStatus?.ready == true;
    final missingFiles = currentStatus?.missing.length ?? 8;
    final downloading = currentStatus?.downloading == true;
    final progress = currentStatus == null || currentStatus.totalBytes == 0
        ? 0.0
        : (currentStatus.downloadedBytes / currentStatus.totalBytes).clamp(
            0.0,
            1.0,
          );
    final colour = ready ? const Color(0xff64efb2) : const Color(0xffffc978);
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0xff0b0d11),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: colour.withValues(alpha: .38)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          Row(
            children: <Widget>[
              Icon(Icons.memory_rounded, color: colour),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'On-device voice models',
                  style: theme.textTheme.titleLarge,
                ),
              ),
              IconButton(
                tooltip: 'Refresh model status',
                onPressed: onRefresh,
                icon: const Icon(Icons.refresh),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            ready
                ? 'Offline speech and language files are installed.'
                : 'Three model systems need $missingFiles support files. The pendant can pair, but voice processing is unavailable.',
          ),
          const SizedBox(height: 14),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: <Widget>[
              _ModelChip(label: 'Moonshine STT', ready: ready),
              _ModelChip(label: 'Qwen local LLM', ready: ready),
              _ModelChip(label: 'Kokoro TTS', ready: ready),
            ],
          ),
          const SizedBox(height: 16),
          if (downloading) ...<Widget>[
            LinearProgressIndicator(value: progress),
            const SizedBox(height: 8),
            Text(
              currentStatus!.downloadMessage,
              style: theme.textTheme.bodySmall,
            ),
          ] else
            FilledButton.icon(
              onPressed: () => _startDownload(context),
              icon: const Icon(Icons.download_rounded),
              label: const Text('Download models'),
            ),
          const SizedBox(height: 8),
          Text(
            'Downloads a pinned Hugging Face model pack (~2.5 GB). Keep the app open and use Wi‑Fi. The native voice engine is the remaining setup step after files finish downloading.',
            style: theme.textTheme.bodySmall?.copyWith(color: Colors.white60),
          ),
        ],
      ),
    );
  }

  Future<void> _startDownload(BuildContext context) => showDialog<void>(
    context: context,
    builder: (dialogContext) => AlertDialog(
      title: const Text('Model downloads'),
      content: const Text(
        'This downloads Moonshine speech recognition, Qwen local language model and Kokoro speech files from fixed Hugging Face revisions. It needs about 2.7 GB free device storage and Wi‑Fi.\n\nThe files are ready for the on-device engine; voice capture will remain unavailable until that native runtime is included.',
      ),
      actions: <Widget>[
        TextButton(
          onPressed: () => Navigator.of(dialogContext).pop(),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () async {
            Navigator.of(dialogContext).pop();
            await onDownload();
          },
          child: const Text('Download'),
        ),
      ],
    ),
  );
}

class _ModelChip extends StatelessWidget {
  const _ModelChip({required this.label, required this.ready});

  final String label;
  final bool ready;

  @override
  Widget build(BuildContext context) => Chip(
    avatar: Icon(
      ready ? Icons.check_circle : Icons.download_outlined,
      size: 18,
    ),
    label: Text(label),
  );
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
          OutlinedButton.icon(
            onPressed: () => Navigator.of(context).push<void>(
              MaterialPageRoute<void>(
                builder: (_) => const _PendantCameraDebugView(),
              ),
            ),
            icon: const Icon(Icons.camera_alt_outlined),
            label: const Text('Open pendant camera'),
          ),
          const SizedBox(height: 12),
          if (connected)
            FilledButton.icon(
              onPressed: null,
              icon: Icon(Icons.check_circle_outline),
              label: Text('Pendant connected'),
            )
          else
            Column(
              children: <Widget>[
                FilledButton.icon(
                  onPressed: () => _scanAndPair(context),
                  icon: const Icon(Icons.qr_code_scanner_rounded),
                  label: const Text('Connect with pendant Wi-Fi QR'),
                ),
                const SizedBox(height: 10),
                const Text(
                  'Press PWR or BOOT once. Scanning connects directly over Wi-Fi—no Bluetooth pairing.',
                  textAlign: TextAlign.center,
                  style: TextStyle(color: Colors.white60, fontSize: 12),
                ),
              ],
            ),
        ],
      ),
    );
  }

  Future<void> _scanAndPair(BuildContext context) async {
    final code = await Navigator.of(context).push<String>(
      MaterialPageRoute<String>(builder: (_) => const _PendantQrScanner()),
    );
    if (code == null || !context.mounted) return;
    try {
      await controller.pairFromQr(code);
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Connecting to your Pendant…')),
        );
      }
    } on Exception catch (error) {
      if (context.mounted) {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Could not pair: $error')));
      }
    }
  }

  static String _title(PendantConnection connection, AssistantPhase phase) {
    if (connection != PendantConnection.connected) {
      return connection == PendantConnection.pairing
          ? 'Pairing'
          : 'Not connected';
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

class _PendantQrScanner extends StatefulWidget {
  const _PendantQrScanner();

  @override
  State<_PendantQrScanner> createState() => _PendantQrScannerState();
}

class _PendantQrScannerState extends State<_PendantQrScanner> {
  final MobileScannerController scanner = MobileScannerController(
    facing: CameraFacing.back,
    formats: const <BarcodeFormat>[BarcodeFormat.qrCode],
    detectionSpeed: DetectionSpeed.normal,
    autoZoom: true,
    cameraResolution: const Size(1280, 720),
  );
  bool found = false;
  double zoom = 1;

  @override
  void dispose() {
    scanner.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: Colors.black,
    appBar: AppBar(title: const Text('Scan Pendant QR')),
    body: LayoutBuilder(
      builder: (context, constraints) {
        final target = (constraints.maxWidth * .78).clamp(260.0, 340.0);
        final window = Rect.fromCenter(
          center: Offset(constraints.maxWidth / 2, constraints.maxHeight / 2),
          width: target,
          height: target,
        );
        return Stack(
          fit: StackFit.expand,
          children: <Widget>[
            MobileScanner(
              controller: scanner,
              scanWindow: window,
              tapToFocus: true,
              errorBuilder: (_, error) => Center(
                child: Padding(
                  padding: const EdgeInsets.all(24),
                  child: Text(
                    'Camera unavailable: ${error.errorDetails?.message ?? error.errorCode.name}',
                  ),
                ),
              ),
              onDetect: (capture) {
                if (found) return;
                final raw = capture.barcodes.firstOrNull?.rawValue?.trim();
                if (raw == null ||
                    !RegExp(
                      r'^PENDANT:W:[0-9A-Fa-f]{6}:.{8,63}$',
                    ).hasMatch(raw)) {
                  return;
                }
                found = true;
                scanner.stop();
                Navigator.of(context).pop(raw);
              },
            ),
            Center(
              child: Container(
                width: target,
                height: target,
                decoration: BoxDecoration(
                  border: Border.all(color: const Color(0xff52f39a), width: 3),
                  borderRadius: BorderRadius.circular(24),
                  boxShadow: const <BoxShadow>[
                    BoxShadow(color: Color(0x6652f39a), blurRadius: 26),
                  ],
                ),
              ),
            ),
            Positioned(
              top: 18,
              right: 18,
              child: ValueListenableBuilder<MobileScannerState>(
                valueListenable: scanner,
                builder: (_, state, _) => IconButton.filledTonal(
                  tooltip: 'Toggle torch',
                  onPressed: scanner.toggleTorch,
                  icon: Icon(
                    state.torchState == TorchState.on
                        ? Icons.flashlight_on_rounded
                        : Icons.flashlight_off_rounded,
                  ),
                ),
              ),
            ),
            Positioned(
              left: 30,
              right: 30,
              bottom: 24,
              child: Column(
                children: <Widget>[
                  Text(
                    'Fit the whole pendant QR inside the green frame. Use scale if it is small.',
                    textAlign: TextAlign.center,
                  ),
                  SizedBox(height: 8),
                  Text(
                    'Rear camera • QR-only • tap to focus • Wi-Fi pairing',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: Colors.white70, fontSize: 12),
                  ),
                  const SizedBox(height: 8),
                  Row(
                    children: <Widget>[
                      const Icon(Icons.zoom_out_rounded, size: 18),
                      Expanded(
                        child: Slider(
                          value: zoom,
                          min: 1,
                          max: 4,
                          divisions: 12,
                          label: '${zoom.toStringAsFixed(1)}×',
                          onChanged: (value) => setState(() => zoom = value),
                          onChangeEnd: scanner.setZoomScale,
                        ),
                      ),
                      const Icon(Icons.zoom_in_rounded, size: 18),
                    ],
                  ),
                ],
              ),
            ),
          ],
        );
      },
    ),
  );
}

/// Full-screen camera view for visually checking the pendant during hardware
/// debugging. QR detections are intentionally ignored so this view remains
/// open while a button is pressed or the screen is tapped.
class _PendantCameraDebugView extends StatefulWidget {
  const _PendantCameraDebugView();

  @override
  State<_PendantCameraDebugView> createState() =>
      _PendantCameraDebugViewState();
}

class _PendantCameraDebugViewState extends State<_PendantCameraDebugView> {
  CameraController? camera;
  late final Future<void> cameraReady = _openCamera();
  XFile? capturedFrame;
  bool torchEnabled = false;

  Future<void> _openCamera() async {
    final cameras = await availableCameras();
    if (cameras.isEmpty) {
      throw StateError('No camera is available on this phone.');
    }
    final rearCameras = cameras
        .where((device) => device.lensDirection == CameraLensDirection.back)
        .toList();
    camera = CameraController(
      rearCameras.isNotEmpty ? rearCameras.first : cameras.first,
      ResolutionPreset.high,
      enableAudio: false,
    );
    await camera!.initialize();
  }

  Future<void> _takePicture() async {
    final activeCamera = camera;
    if (activeCamera == null || !activeCamera.value.isInitialized) return;
    try {
      final frame = await activeCamera.takePicture();
      if (mounted) setState(() => capturedFrame = frame);
    } on CameraException catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            'Could not capture pendant: ${error.description ?? error.code}',
          ),
        ),
      );
    }
  }

  Future<void> _toggleTorch() async {
    final activeCamera = camera;
    if (activeCamera == null) return;
    final next = !torchEnabled;
    try {
      await activeCamera.setFlashMode(next ? FlashMode.torch : FlashMode.off);
      if (mounted) setState(() => torchEnabled = next);
    } on CameraException {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Torch is not available on this camera.'),
          ),
        );
      }
    }
  }

  @override
  void dispose() {
    camera?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (capturedFrame case final frame?) {
      return _CapturedPendantFrame(
        file: File(frame.path),
        onRetake: () => setState(() => capturedFrame = null),
      );
    }
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        title: const Text('Pendant camera'),
        actions: <Widget>[
          IconButton(
            tooltip: torchEnabled ? 'Turn torch off' : 'Turn torch on',
            onPressed: _toggleTorch,
            icon: Icon(
              torchEnabled
                  ? Icons.flashlight_on_rounded
                  : Icons.flashlight_off_rounded,
            ),
          ),
        ],
      ),
      body: FutureBuilder<void>(
        future: cameraReady,
        builder: (context, snapshot) {
          if (snapshot.hasError) {
            return Center(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Text(
                  'Camera unavailable: ${snapshot.error}',
                  textAlign: TextAlign.center,
                ),
              ),
            );
          }
          if (snapshot.connectionState != ConnectionState.done ||
              camera == null) {
            return const Center(child: CircularProgressIndicator());
          }
          return Stack(
            fit: StackFit.expand,
            children: <Widget>[
              CameraPreview(camera!),
              Center(
                child: IgnorePointer(
                  child: Container(
                    width: 260,
                    height: 260,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      border: Border.all(
                        color: const Color(0xff52f39a).withValues(alpha: .85),
                        width: 2,
                      ),
                      boxShadow: const <BoxShadow>[
                        BoxShadow(color: Color(0x4452F39A), blurRadius: 28),
                      ],
                    ),
                  ),
                ),
              ),
              Positioned(
                left: 20,
                right: 20,
                bottom: 28,
                child: IgnorePointer(
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      color: const Color(0xdd071013),
                      borderRadius: BorderRadius.circular(18),
                      border: Border.all(color: const Color(0x6652f39a)),
                    ),
                    child: const Padding(
                      padding: EdgeInsets.all(16),
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: <Widget>[
                          Text('Live pendant debug view'),
                          SizedBox(height: 6),
                          Text(
                            'Centre the pendant in the ring, then tap its screen. '
                            'Tap the phone preview to focus. Use your phone’s '
                            'screenshot or screen recording controls to capture it.',
                            textAlign: TextAlign.center,
                            style: TextStyle(
                              color: Colors.white70,
                              fontSize: 12,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
              Positioned(
                bottom: 216,
                left: 0,
                right: 0,
                child: Center(
                  child: FilledButton.icon(
                    onPressed: _takePicture,
                    icon: const Icon(Icons.camera_alt_rounded),
                    label: const Text('Capture'),
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _CapturedPendantFrame extends StatelessWidget {
  const _CapturedPendantFrame({required this.file, required this.onRetake});

  final File file;
  final VoidCallback onRetake;

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: Colors.black,
    appBar: AppBar(title: const Text('Captured pendant frame')),
    body: Column(
      children: <Widget>[
        Expanded(
          child: Center(child: InteractiveViewer(child: Image.file(file))),
        ),
        SafeArea(
          top: false,
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Row(
              children: <Widget>[
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: onRetake,
                    icon: const Icon(Icons.refresh_rounded),
                    label: const Text('Retake'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: FilledButton.icon(
                    onPressed: () => Navigator.of(context).pop(),
                    icon: const Icon(Icons.check_rounded),
                    label: const Text('Done'),
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    ),
  );
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
          if (phase == AssistantPhase.listening ||
              phase == AssistantPhase.thinking)
            Container(
              width: 136,
              height: 136,
              decoration: BoxDecoration(
                border: Border.all(
                  color: color.withValues(alpha: 0.55),
                  width: 2,
                ),
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
              decoration: BoxDecoration(
                color: color,
                borderRadius: BorderRadius.circular(20),
              ),
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
        boxShadow: <BoxShadow>[
          BoxShadow(color: color.withValues(alpha: 0.55), blurRadius: 18),
        ],
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
        subtitle: Text(
          note.summary,
          maxLines: 2,
          overflow: TextOverflow.ellipsis,
        ),
        trailing: Text('${note.createdAt.month}/${note.createdAt.day}'),
      ),
    );
  }
}
