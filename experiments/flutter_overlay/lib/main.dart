import 'package:flutter/widgets.dart';

import 'companion_channel.dart';
import 'overlay_screen.dart';

void main() {
  runApp(OverlayApp(companion: MethodChannelCompanion()));
}
