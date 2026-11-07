#pragma once
#include <Arduino.h>
#include "Config.h"
#include <AccelStepper.h>

class Motion {
public:
  void begin();
  void service();
   float xMaxRate() const { return xMaxRateMmMin_; }
  float yMaxRate() const { return yMaxRateMmMin_; }
  float homingSeekRate() const { return homingSeekRateMmMin_; }

  bool setMaxRates(float x, float y);
  bool setHomingSeekRate(float rate);


  float xStepsPerMm() const { return xStepsPerMm_; }// for steps calculator
  float yStepsPerMm() const { return yStepsPerMm_; }// for steps calculator
  bool setStepsPerMm(float x, float y);

  bool moveTo(float xMm, float yMm, float feedMmMin);
  bool home();
  void setPosition(float xMm, float yMm);
  void pen(bool up);
  void stop();

  void pause();
  void resume();
  void setMaxSpeed(AccelStepper& motor, String axis, int max);

  float x() const { return xMm_; }
  float y() const { return yMm_; }
  bool busy() const { return busy_; }
  const char* state() const { return busy_ ? "Run" : "Idle"; }
  float xMaxRateMmMin_ = MAX_FEED_MM_MIN;
  float yMaxRateMmMin_ = MAX_FEED_MM_MIN;
  float homingSeekRateMmMin_ = HOMING_FEED_MM_MIN;
private:

  bool switchPressed(int pin);
  float xStepsPerMm_ = X_STEPS_PER_MM;// for steps calculator
  float yStepsPerMm_ = Y_STEPS_PER_MM;// for steps calculator
  float xMm_ = 0;
  float yMm_ = 0;
  bool busy_ = false;

  long pausedXTarget_ = 0;
  long pausedYTarget_ = 0;
  bool movePaused_ = false;

};
