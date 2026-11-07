#include "Gcode.h"
#include "Config.h"
#include <math.h>
#include "Debugger.h"

static bool wordValue(const String& s, char letter, float& value) {
  const int i = s.indexOf(letter);
  if (i < 0) return false;
  value = s.substring(i + 1).toFloat();
  return true;
}

static bool hasCode(const String& s, const char* code) {
  return s.indexOf(code) >= 0;
}
static bool usbCommandClaimsOwnership(String line) {
  line.trim();
  line.toUpperCase();

  const int comment = line.indexOf(';');
  if (comment >= 0) line = line.substring(0, comment);
  line.trim();

  return hasCode(line, "G0") || hasCode(line, "G1") || hasCode(line, "G28") || line == "M3" || line.startsWith("M3 ") || line == "M4" || line.startsWith("M4 ") || line == "M5" || line.startsWith("M5 ") || line == "M9" || line.startsWith("M9 ") || line.indexOf('Z') >= 0;
}
bool Gcode::enqueue(const String& input, Source source, uint16_t lineNumber) {
  String line = input;
  line.trim();
  if (!line.length() || line == "?") return true;

  if (ownershipLockEnabled_ && owner_ != NONE && owner_ != source) return false;
  if (count_ >= QUEUE_SIZE) return false;

  if (ownershipLockEnabled_ && owner_ == NONE) {
    // Web explicitly claims control for its job. USB setup/modal commands
    // alone should not lock out web jogging.
    if (source == WEB || usbCommandClaimsOwnership(line)) owner_ = source;
  }

  lastActivityMs_ = millis();

  Entry& e = queue_[tail_];
  e.text = line;
  e.lineNumber = lineNumber;
  e.endsJob = hasCode(line, "M2") || hasCode(line, "M30");
  tail_ = (tail_ + 1) % QUEUE_SIZE;
  ++count_;
  return true;
}


bool Gcode::claimWeb() {
  if (!CONTROL_SOURCE_LOCKING_ENABLED) return true;

  if (owner_ != NONE && owner_ != WEB) return false;
  owner_ = WEB;
  lastActivityMs_ = millis();
  return true;
}
void Gcode::setOwnershipLockEnabled(bool enabled) {
  ownershipLockEnabled_ = enabled;
  if (!enabled) {
    owner_ = NONE;
  } else if (owner_ != NONE) {
    lastActivityMs_ = millis();
  }
Debugger::log(String("USB/web ownership lock ") +
              (enabled ? "enabled" : "disabled"));
}


bool Gcode::busy() const {
  return count_ > 0 || motion_.busy() || endPending_;
}

bool Gcode::pause() {
  if (owner_ == NONE) return false;
  paused_ = true;
  motion_.pause();
  return true;
}

void Gcode::resume() {
  motion_.resume();
  paused_ = false;
}

void Gcode::clearQueue() {
  while (count_) {
    queue_[head_].text = "";
    head_ = (head_ + 1) % QUEUE_SIZE;
    --count_;
  }
  tail_ = head_;
}

void Gcode::stop() {
  clearQueue();
  motion_.stop();
  paused_ = false;
  endPending_ = false;
  owner_ = NONE;
}

void Gcode::service() {
  if (ownershipLockEnabled_ && owner_ != NONE && count_ == 0 && !motion_.busy() && !endPending_ && millis() - lastActivityMs_ >= 2000) {
    owner_ = NONE;
  }

  if (motion_.busy()) return;

  if (endPending_) {
    endPending_ = false;
    if (count_ == 0) owner_ = NONE;
  }

  if (paused_ || count_ == 0) return;

  Entry e = queue_[head_];
  queue_[head_].text = "";
  head_ = (head_ + 1) % QUEUE_SIZE;
  --count_;

  currentLine_ = e.lineNumber;
  executeLine(e.text);

  if (e.endsJob) endPending_ = true;

  if (!motion_.busy() && endPending_ && count_ == 0) {
    endPending_ = false;
    owner_ = NONE;
  }
}

bool Gcode::executeLine(const String& input) {
  String line = input;
  line.trim();
  line.toUpperCase();

  const int comment = line.indexOf(';');
  if (comment >= 0) {
    line = line.substring(0, comment);
    line.trim();
  }
  if (!line.length()) return true;
  // Runtime steps-per-mm settings.
  if (line.startsWith("$100=")) {
    const float x = line.substring(5).toFloat();
    return motion_.setStepsPerMm(x, motion_.yStepsPerMm());
  }

  if (line.startsWith("$101=")) {
    const float y = line.substring(5).toFloat();
    return motion_.setStepsPerMm(motion_.xStepsPerMm(), y);
  }
  if (line.startsWith("$110=")) {
    const float x = line.substring(5).toFloat();
    return motion_.setMaxRates(x, motion_.yMaxRate());
  }

  if (line.startsWith("$111=")) {
    const float y = line.substring(5).toFloat();
    return motion_.setMaxRates(motion_.xMaxRate(), y);
  }

  if (line.startsWith("$25=")) {
    const float rate = line.substring(4).toFloat();
    return motion_.setHomingSeekRate(rate);
  }
  // Units and distance mode.
  if (hasCode(line, "G20")) inches_ = true;
  if (hasCode(line, "G21")) inches_ = false;
  if (hasCode(line, "G90")) absolute_ = true;
  if (hasCode(line, "G91")) absolute_ = false;

  // Pen commands: M3/M4 down; M5/M9 up.
  if (hasCode(line, "M5") || hasCode(line, "M9")) motion_.pen(true);
  if (hasCode(line, "M3 ") || line == "M3" || hasCode(line, "M4 ") || line == "M4") motion_.pen(false);

  float value;
  if (wordValue(line, 'F', value) && value > 0) feed_ = value;

  if (hasCode(line, "G28")) return motion_.home();

  if (wordValue(line, 'Z', value)) motion_.pen(value > 0);

  // G92 sets the current coordinate without moving.
  if (hasCode(line, "G92")) {
    float x = motion_.x(), y = motion_.y();
    const float scale = inches_ ? 25.4f : 1.0f;
    if (wordValue(line, 'X', value)) x = value * scale;
    if (wordValue(line, 'Y', value)) y = value * scale;
    motion_.setPosition(x, y);
    return true;
  }

  const bool move = hasCode(line, "G0") || hasCode(line, "G1");
  if (!move) return true;

  float x = motion_.x(), y = motion_.y();
  const float scale = inches_ ? 25.4f : 1.0f;

  if (wordValue(line, 'X', value))
    x = absolute_ ? value * scale : x + value * scale;
  if (wordValue(line, 'Y', value))
    y = absolute_ ? value * scale : y + value * scale;

  return motion_.moveTo(x, y, feed_ * scale);
}

String Gcode::status() {
  return String("<") + motion_.state()
         + "|MPos:" + String(motion_.x(), 3)
         + "," + String(motion_.y(), 3)
         + "|FS:0," + String(feed_, 0) + ">";
}

String Gcode::webStatus() {
  String s = "{\"state\":\"";
  s += busy() ? (paused_ ? "Hold" : "Run") : "Idle";
  s += "\",\"busy\":";
  s += busy() ? "true" : "false";
  s += ",\"paused\":";
  s += paused_ ? "true" : "false";
  s += ",\"owner\":\"";
  s += owner_ == USB ? "USB" : owner_ == WEB ? "WEB"
                                             : "NONE";
  s += "\",\"line\":";
  s += String(currentLine_);
  s += ",\"x\":";
  s += String(motion_.x(), 3);
  s += ",\"y\":";
  s += String(motion_.y(), 3);
  s += "}";
  return s;
}
String Gcode::settings() const {
  String result;

  result += "$100=";
  result += String(xStepsPerMm(), 2);
  result += '\n';

  result += "$101=";
  result += String(yStepsPerMm(), 2);
  result += '\n';

  result += "$25=";
  result += String(homingSeekRate(), 2);
  result += '\n';

  result += "$110=";
  result += String(xMaxRate(), 2);
  result += '\n';

  result += "$111=";
  result += String(yMaxRate(), 2);

  return result;
}
static bool isActionableUsbCommand(const String& line) {
  // Commands that can initiate movement or change the pen state.
  return hasCode(line, "G0") || hasCode(line, "G1") || hasCode(line, "G28") || hasCode(line, "M3") || hasCode(line, "M4") || hasCode(line, "M5") || hasCode(line, "M9") || line.indexOf('Z') >= 0;
}
