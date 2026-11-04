#include "Config.h"
#include "Motion.h"
#include "Gcode.h"
#include "Web.h"
//v1.1

Motion motion;
Gcode gcode(motion);

static String serialLine;
static String pendingLine;
static bool hasPendingLine = false;

static void handleRealtime(char c) {
  if (c == '?') {
    Serial.println(gcode.status());
  } else if (c == '!') {
    gcode.pause();
  } else if (c == '~') {
    gcode.resume();
  } else if (c == 0x18) {  // Ctrl-X
    gcode.stop();
    Serial.println("Grbl 1.1-compatible FergoPlotter v1.1");
  }
}

static bool handleSystemCommand(const String& line) {
  if (line == "$I") {
    Serial.println("[VER:FergoPlotter v1.1]");
    Serial.println("[OPT:V,24,120]");
    Serial.println("ok");
    return true;
  }
  if (line.startsWith("$100=") || line.startsWith("$101=")) {
    return false;  // Let Gcode enqueue and execute the setting.
  }
  if (line.startsWith("$100=") || line.startsWith("$101=") ||
      line.startsWith("$110=") || line.startsWith("$111=") ||
      line.startsWith("$25=")) {
    return false;  // Let Gcode enqueue and execute the setting.
  }
  // if (line == "$$") {
  //   Serial.println("$0=10");
  //   Serial.println("$1=25");
  //   Serial.println("$10=3");
  //   Serial.println("$25=" + String(gcode.homingSeekRate(), 2));
  //   Serial.println("$110=" + String(gcode.xMaxRate(), 2));
  //   Serial.println("$111=" + String(gcode.yMaxRate(), 2));
  //   Serial.println("ok");
  //   return true;
  // }
  if (line == "$" || line == "$$") {
  Serial.print(gcode.settings());
  Serial.println("ok");
  return true;
}

  if (line == "$G") {
    Serial.println("[GC:G0 G54 G17 G21 G90 G94 M5 M9]");
    Serial.println("ok");
    return true;
  }

  if (line == "$X") {
    Serial.println("ok");
    return true;
  }

  if (line.startsWith("$")) {
    Serial.println("error:unsupported system command");
    return true;
  }

  return false;
}

static void submitSerialLine() {
  if (!hasPendingLine) return;

  String line = pendingLine;
  line.trim();

  if (handleSystemCommand(line)) {
    pendingLine = "";
    hasPendingLine = false;
    return;
  }

  if (gcode.enqueue(line, Gcode::USB)) {
    Serial.println("ok");
    pendingLine = "";
    hasPendingLine = false;
  } else if (gcode.owner() != Gcode::NONE &&
             gcode.owner() != Gcode::USB) {
    Serial.println("error:controller owned by web job");
    pendingLine = "";
    hasPendingLine = false;
  }
  // If the queue is full, retain the line and retry next loop.
}

void setup() {
  Serial.begin(115200);
  motion.begin();
  webBegin(gcode);

  // Keep protocol output clean: no debug messages on this port.
  Serial.println("Grbl 1.1-compatible FergoPlotter v1.1");
}

void loop() {
  motion.service();
  gcode.service();
  webService();

  submitSerialLine();

  // While a completed line is waiting for queue space, don't consume
  // more input; UGS will naturally pause when it stops receiving "ok".
  if (hasPendingLine) return;

  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());

    if (c == '?' || c == '!' || c == '~' || c == 0x18) {
      handleRealtime(c);
      continue;
    }

    if (c == '\r' || c == '\n') {
      if (serialLine.length()) {
        pendingLine = serialLine;
        serialLine = "";
        hasPendingLine = true;
        submitSerialLine();
        if (hasPendingLine) return;
      }
    } else if (serialLine.length() < 120) {
      serialLine += c;
    } else {
      serialLine = "";
      Serial.println("error:line too long");
    }
  }
}

