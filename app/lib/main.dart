import 'package:flutter/material.dart';

import 'platform/pendant_bridge.dart';
import 'ui/pendant_app.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(PendantApp(controller: PendantController(PendantBridge())));
}
