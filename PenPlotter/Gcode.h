#pragma once
#include <Arduino.h>
#include "Motion.h"

class Gcode {
public:
  float xStepsPerMm() const { return motion_.xStepsPerMm(); }
  float yStepsPerMm() const { return motion_.yStepsPerMm(); }
  enum Source : uint8_t { NONE, USB, WEB };

  explicit Gcode(Motion& motion) : motion_(motion) {}

  bool enqueue(const String& line, Source source, uint16_t lineNumber = 0);
  void service();

  bool claimWeb();
  bool pause();
  void resume();
  void stop();

  bool busy() const;
  bool paused() const { return paused_; }
  Source owner() const { return owner_; }
  uint16_t currentLine() const { return currentLine_; }

  String status();
  String webStatus();
  float xMaxRate() const { return motion_.xMaxRate(); }
  float yMaxRate() const { return motion_.yMaxRate(); }
  float homingSeekRate() const { return motion_.homingSeekRate(); }

private:
  static constexpr uint8_t QUEUE_SIZE = 24;

  struct Entry {
    String text;
    uint16_t lineNumber = 0;
    bool endsJob = false;
  };

  Motion& motion_;
  Entry queue_[QUEUE_SIZE];
  uint8_t head_ = 0;
  uint8_t tail_ = 0;
  uint8_t count_ = 0;

  Source owner_ = NONE;
  bool paused_ = false;
  bool endPending_ = false;
  uint16_t currentLine_ = 0;

  float feed_ = 600.0f;
  bool absolute_ = true;
  bool inches_ = false;

  bool executeLine(const String& line);
  void clearQueue();
  };

