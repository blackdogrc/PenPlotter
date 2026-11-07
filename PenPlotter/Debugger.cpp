#include "Debugger.h"

bool Debugger::enabled_ = false;
String Debugger::pendingLog_;

void Debugger::setEnabled(bool enabled) {
  enabled_ = enabled;
}

bool Debugger::isEnabled() {
  return enabled_;
}

void Debugger::log(const String& msg) {
  if (!enabled_) return;

  pendingLog_ += msg;
  pendingLog_ += '\n';
}

String Debugger::takePendingLog() {
  String result = pendingLog_;
  pendingLog_ = "";
  return result;
}

