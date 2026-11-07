#pragma once
#include <Arduino.h>

class Debugger {
public:
  static void setEnabled(bool enabled);
  static bool isEnabled();

  static void log(const String& msg);
  static String takePendingLog();

private:
  static bool enabled_;
  static String pendingLog_;
};
