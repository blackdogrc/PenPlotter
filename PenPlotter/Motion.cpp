#include "Motion.h"
#include "Config.h"
#include <Preferences.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>
#include <math.h>

static AccelStepper xMotor(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);
static AccelStepper yMotor(AccelStepper::DRIVER, Y_STEP_PIN, Y_DIR_PIN);
static Servo penServo;
  
void Motion::begin() {
  pinMode(X_MIN_PIN, INPUT_PULLUP);
  pinMode(Y_MIN_PIN, INPUT_PULLUP); 
  Preferences preferences;
  if (preferences.begin("fergoplotter", true)) {
      const float savedXRate =
        preferences.getFloat("x_rate", MAX_FEED_MM_MIN);
    const float savedYRate =
        preferences.getFloat("y_rate", MAX_FEED_MM_MIN);
    const float savedHomeRate =
        preferences.getFloat("home_rate", homingSeekRateMmMin_);

    const float savedX = preferences.getFloat("x_steps", X_STEPS_PER_MM);
    const float savedY = preferences.getFloat("y_steps", Y_STEPS_PER_MM);
    preferences.end();

    if (isfinite(savedX) && savedX >= 1.0f && savedX <= 2000.0f) {
      xStepsPerMm_ = savedX;
    }
    if (isfinite(savedY) && savedY >= 1.0f && savedY <= 2000.0f) {
      yStepsPerMm_ = savedY;
    }
  }

  xMotor.setMaxSpeed(3000);
  yMotor.setMaxSpeed(3000);
  xMotor.setAcceleration(1500);
  yMotor.setAcceleration(1500);

  penServo.setPeriodHertz(50);
  penServo.attach(SERVO_PIN, 500, 2500);
  pen(true);  // Pen up
}

void Motion::service() {
  if (!busy_) return;

  xMotor.run();
  yMotor.run();

  if (xMotor.distanceToGo() == 0 && yMotor.distanceToGo() == 0) {
    busy_ = false;
    xMm_ = xMotor.currentPosition() / xStepsPerMm_;
    yMm_ = yMotor.currentPosition() / yStepsPerMm_;
  }
}

bool Motion::moveTo(float xMm, float yMm, float feedMmMin) {
  if (busy_ || feedMmMin <= 0) return false;

  const long xt = lroundf(xMm * xStepsPerMm_);
  const long yt = lroundf(yMm * yStepsPerMm_);

  const float dx =
      (xt - xMotor.currentPosition()) / xStepsPerMm_;
  const float dy =
      (yt - yMotor.currentPosition()) / yStepsPerMm_;
  const float distance = hypotf(dx, dy);

  if (distance < 0.001f) {
    xMm_ = xMm;
    yMm_ = yMm;
    return true;
  }

  const float ux = fabsf(dx / distance);
  const float uy = fabsf(dy / distance);

  // Limit the overall feed to satisfy both axis limits while preserving
  // the requested direction of travel.
  float limitedFeed = min(feedMmMin, MAX_FEED_MM_MIN);
  if (ux > 0.0f) limitedFeed = min(limitedFeed, xMaxRateMmMin_ / ux);
  if (uy > 0.0f) limitedFeed = min(limitedFeed, yMaxRateMmMin_ / uy);

  const float feed = limitedFeed / 60.0f;

  xMotor.setMaxSpeed(max(1.0f, ux * feed * xStepsPerMm_));
  yMotor.setMaxSpeed(max(1.0f, uy * feed * yStepsPerMm_));

  xMotor.moveTo(xt);
  yMotor.moveTo(yt);
  busy_ = true;
  return true;
}

bool Motion::switchPressed(int pin) {
  const int value = digitalRead(pin);
  return ENDSTOPS_ACTIVE_LOW ? value == LOW : value == HIGH;
}

bool Motion::home() {
  if (busy_) return false;

  xMotor.setMaxSpeed(homingSeekRateMmMin_ / 60.0f * xStepsPerMm_);
  while (!switchPressed(X_MIN_PIN)) {
    xMotor.moveTo(xMotor.currentPosition() - 20);
    xMotor.run();
    if (xMotor.currentPosition() < -200000) return false;
    yield();
  }

  xMotor.stop();
  while (xMotor.isRunning()) {
    xMotor.run();
    yield();
  }

  xMotor.setCurrentPosition(0);
  xMotor.moveTo(lroundf(HOME_BACKOFF_MM * xStepsPerMm_));
  while (xMotor.distanceToGo()) {
    xMotor.run();
    yield();
  }
  xMotor.setCurrentPosition(0);

  yMotor.setMaxSpeed(homingSeekRateMmMin_ / 60.0f * yStepsPerMm_);
  while (!switchPressed(Y_MIN_PIN)) {
    yMotor.moveTo(yMotor.currentPosition() - 20);
    yMotor.run();
    if (yMotor.currentPosition() < -200000) return false;
    yield();
  }

  yMotor.stop();
  while (yMotor.isRunning()) {
    yMotor.run();
    yield();
  }

  yMotor.setCurrentPosition(0);
  yMotor.moveTo(lroundf(HOME_BACKOFF_MM * yStepsPerMm_));
  while (yMotor.distanceToGo()) {
    yMotor.run();
    yield();
  }
  yMotor.setCurrentPosition(0);

  xMm_ = yMm_ = 0;
  return true;
}

void Motion::setPosition(float xMm, float yMm) {
  if (busy_) return;

  xMotor.setCurrentPosition(lroundf(xMm * xStepsPerMm_));
  yMotor.setCurrentPosition(lroundf(yMm * yStepsPerMm_));
  xMm_ = xMm;
  yMm_ = yMm;
}

void Motion::pen(bool up) {
  const int pulse = up ? PEN_UP_US : PEN_DOWN_US;
  penServo.writeMicroseconds(pulse);
}

void Motion::stop() {
  xMotor.setCurrentPosition(xMotor.currentPosition());
  yMotor.setCurrentPosition(yMotor.currentPosition());

  xMm_ = xMotor.currentPosition() / xStepsPerMm_;
  yMm_ = yMotor.currentPosition() / yStepsPerMm_;
  busy_ = false;
  movePaused_ = false;
  pen(true);
}

void Motion::pause() {
  if (!busy_) return;

  pausedXTarget_ = xMotor.targetPosition();
  pausedYTarget_ = yMotor.targetPosition();

  // Cancel the current targets at the current step positions.
  xMotor.setCurrentPosition(xMotor.currentPosition());
  yMotor.setCurrentPosition(yMotor.currentPosition());

  xMm_ = xMotor.currentPosition() / xStepsPerMm_;
  yMm_ = yMotor.currentPosition() / yStepsPerMm_;
  busy_ = false;
  movePaused_ = true;
}

void Motion::resume() {
  if (!movePaused_) return;

  xMotor.moveTo(pausedXTarget_);
  yMotor.moveTo(pausedYTarget_);
  busy_ = true;
  movePaused_ = false;
}
bool Motion::setStepsPerMm(float x, float y) {
  if (busy_) return false;
  if (!isfinite(x) || !isfinite(y)) return false;
  if (x < 1.0f || x > 2000.0f ||
      y < 1.0f || y > 2000.0f) return false;

  Preferences preferences;
  if (!preferences.begin("fergoplotter", false)) {
    return false;
  }

  const size_t savedX = preferences.putFloat("x_steps", x);
  const size_t savedY = preferences.putFloat("y_steps", y);
  preferences.end();

  if (savedX != sizeof(float) || savedY != sizeof(float)) {
    return false;
  }
 
  xStepsPerMm_ = x;
  yStepsPerMm_ = y;

  // Keep the reported coordinates consistent with the current motor steps.
  xMm_ = xMotor.currentPosition() / xStepsPerMm_;
  yMm_ = yMotor.currentPosition() / yStepsPerMm_;
  return true;
}

bool Motion::setMaxRates(float x, float y) {
  if (busy_) return false;
  if (!isfinite(x) || !isfinite(y)) return false;
  if (x < 1.0f || x > 10000.0f ||
      y < 1.0f || y > 10000.0f) return false;

  Preferences preferences;
  if (!preferences.begin("fergoplotter", false)) return false;

  const size_t savedX = preferences.putFloat("x_rate", x);
  const size_t savedY = preferences.putFloat("y_rate", y);
  preferences.end();

  if (savedX != sizeof(float) || savedY != sizeof(float)) return false;

  xMaxRateMmMin_ = x;
  yMaxRateMmMin_ = y;
  return true;
}

bool Motion::setHomingSeekRate(float rate) {
  if (busy_) return false;
  if (!isfinite(rate) || rate < 1.0f || rate > 10000.0f) return false;

  Preferences preferences;
  if (!preferences.begin("fergoplotter", false)) return false;

  const size_t saved = preferences.putFloat("home_rate", rate);
  preferences.end();

  if (saved != sizeof(float)) return false;

  homingSeekRateMmMin_ = rate;
  return true;
}
