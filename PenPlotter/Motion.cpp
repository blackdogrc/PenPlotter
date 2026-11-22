#include "Motion.h"
#include "Config.h"
#include <Preferences.h>
#include <AccelStepper.h>
#include <ESP32Servo.h>
#include <math.h>
#include "Debugger.h"

static AccelStepper xMotor(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);
static AccelStepper yMotor(AccelStepper::DRIVER, Y_STEP_PIN, Y_DIR_PIN);
static Servo penServo;
uint32_t moveStartedMs;


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
    const bool savedXEndstop = preferences.getBool("x_endstop_low", X_ENDSTOP_ACTIVE_LOW_DEFAULT);
    const bool savedYEndstop = preferences.getBool("y_endstop_low", Y_ENDSTOP_ACTIVE_LOW_DEFAULT);

    preferences.end();
    xEndstopActiveLow_ = savedXEndstop;
    yEndstopActiveLow_ = savedYEndstop;


    if (isfinite(savedX) && savedX >= 1.0f && savedX <= 2000.0f) {
      xStepsPerMm_ = savedX;
    }
    if (isfinite(savedY) && savedY >= 1.0f && savedY <= 2000.0f) {
      yStepsPerMm_ = savedY;
    }
    xMotor.setCurrentPosition(0);
    yMotor.setCurrentPosition(0);
  }


  //xMotor.setMaxSpeed(3000);
  setMaxSpeed(xMotor, "X", MAX_FEED_MM_MIN);
  setMaxSpeed(yMotor, "Y", MAX_FEED_MM_MIN);
  xMotor.setAcceleration(MAX_ACCELERATION);
  yMotor.setAcceleration(MAX_ACCELERATION);

  penServo.setPeriodHertz(50);
  penServo.attach(SERVO_PIN, 500, 2500);
  pen(true);  // Pen up
}

void Motion::setMaxSpeed(AccelStepper& motor, String axis, int max) {
  Debugger::log(String("Setting maxSpeed for motor ") + axis + " to " + String(max));
  motor.setMaxSpeed(max);
}

void Motion::service() {
  if (!busy_) return;

  xMotor.run();
  yMotor.run();

  if (xMotor.distanceToGo() == 0 && yMotor.distanceToGo() == 0) {
    busy_ = false;
    xMm_ = xMotor.currentPosition() / xStepsPerMm_;
    yMm_ = yMotor.currentPosition() / yStepsPerMm_;

    const uint32_t elapsedMs = millis() - moveStartedMs;

    Debugger::log(String("[") + String(millis()) + " ms] Motion complete: elapsed=" + String(elapsedMs) + " ms" + " X steps=" + String(xMotor.currentPosition()) + " Y steps=" + String(yMotor.currentPosition()));
  }
}


bool Motion::moveTo(float xMm, float yMm, float feedMmMin) {
  if (busy_) {
    Debugger::log("moveTo rejected: motion is already busy");
    return false;
  }

  moveStartedMs = millis();


  if (feedMmMin > MAX_FEED_MM_MIN) {
    Debugger::log("Feed WAS " + String(feedMmMin) + " but capped at MAX_FEED_MM_MIN=" + String(MAX_FEED_MM_MIN));
    feedMmMin = MAX_FEED_MM_MIN;
  }
  if (!isfinite(xMm) || !isfinite(yMm)) {
    Debugger::log("moveTo rejected: X or Y position is not finite");
    return false;
  }

  if (!isfinite(feedMmMin) || feedMmMin <= 0) {
    Debugger::log(String("moveTo rejected: invalid feed ") + String(feedMmMin));
    return false;
  }

  const long xt = lroundf(xMm * xStepsPerMm_);
  const long yt = lroundf(yMm * yStepsPerMm_);

  const float dx =
    (xt - xMotor.currentPosition()) / xStepsPerMm_;
  const float dy =
    (yt - yMotor.currentPosition()) / yStepsPerMm_;
  const float distance = hypotf(dx, dy);

  if (distance < 0.001f) {
    Debugger::log("moveTo accepted as no-op: target is already reached");
    xMm_ = xMm;
    yMm_ = yMm;
    return true;
  }

  const float ux = fabsf(dx / distance);
  const float uy = fabsf(dy / distance);

  float limitedFeed = min(feedMmMin, MAX_FEED_MM_MIN);
  if (ux > 0.0f) limitedFeed = min(limitedFeed, xMaxRateMmMin_ / ux);
  if (uy > 0.0f) limitedFeed = min(limitedFeed, yMaxRateMmMin_ / uy);

  const float feed = limitedFeed / 60.0f;
  const float xSpeed = max(1.0f, ux * feed * xStepsPerMm_);
  const float ySpeed = max(1.0f, uy * feed * yStepsPerMm_);

  Debugger::log(String("[") + String(moveStartedMs) + " ms] moveTo start: target X=" + String(xMm, 3) + " Y=" + String(yMm, 3) + " distance=" + String(distance, 3) + " mm" + " requested F=" + String(feedMmMin, 1) + " effective F=" + String(limitedFeed, 1) + " mm/min" + " Xspeed=" + String(xSpeed, 1) + " Yspeed=" + String(ySpeed, 1) + " steps/s");

  setMaxSpeed(xMotor, "X", xSpeed);
  setMaxSpeed(yMotor, "Y", ySpeed);
  xMotor.moveTo(xt);
  yMotor.moveTo(yt);
  busy_ = true;

  Debugger::log("moveTo accepted: motor targets set; busy=true");
  return true;
}


bool Motion::switchPressed(int pin) {
  const int value = digitalRead(pin);
  const bool activeLow = (pin == X_MIN_PIN)
                           ? xEndstopActiveLow_
                           : yEndstopActiveLow_;

  return activeLow ? value == LOW : value == HIGH;
}
bool Motion::setEndstopPolarity(bool xActiveLow, bool yActiveLow) {
  if (busy_) return false;

  Preferences preferences;
  if (!preferences.begin("fergoplotter", false)) return false;

  const size_t savedX = preferences.putBool("x_endstop_low", xActiveLow);
  const size_t savedY = preferences.putBool("y_endstop_low", yActiveLow);
  preferences.end();

  if (savedX != sizeof(bool) || savedY != sizeof(bool)) return false;

  xEndstopActiveLow_ = xActiveLow;
  yEndstopActiveLow_ = yActiveLow;
  return true;
}


bool Motion::home() {
  if (busy_) return false;

  //xMotor.setMaxSpeed(homingSeekRateMmMin_ / 60.0f * xStepsPerMm_);
  setMaxSpeed(xMotor, "X", homingSeekRateMmMin_ / 60.0f * xStepsPerMm_);

  //run the motor in the negative until we hit the switch or run out of space.
  /*
   * X AXIS.
  */
  long traveled = 0;
  while (traveled < HOMING_MAX_SEEK_STEPS_X && !switchPressed(X_MIN_PIN)) {
    long start = xMotor.currentPosition();
    xMotor.move(-HOMING_CHUNK_SIZE);  // relative move toward the switch
    //run the motor a small chunk.
    while (xMotor.distanceToGo() != 0 && !switchPressed(X_MIN_PIN)) {
      xMotor.run();
      yield();
    }

    if (switchPressed(X_MIN_PIN)) break;

    traveled += labs(xMotor.currentPosition() - start);
  }

  //move away from the stop
  xMotor.setCurrentPosition(0);
  xMotor.moveTo(lroundf(HOME_BACKOFF_MM * xStepsPerMm_));

  while (xMotor.distanceToGo() != 0) {
    xMotor.run();
    yield();
  }

  xMotor.setCurrentPosition(0);



  /*
   * Y motor home
  */
  traveled = 0;
  while (traveled < HOMING_MAX_SEEK_STEPS_Y && !switchPressed(Y_MIN_PIN)) {
    long start = yMotor.currentPosition();
    yMotor.move(-HOMING_CHUNK_SIZE);  // relative move toward the switch
    //run the motor a small chunk.
    while (yMotor.distanceToGo() != 0 && !switchPressed(Y_MIN_PIN)) {
      yMotor.run();
      yield();
    }

    if (switchPressed(Y_MIN_PIN)) break;

    traveled += labs(yMotor.currentPosition() - start);
  }

  //move away from the stop
  yMotor.setCurrentPosition(0);
  yMotor.moveTo(lroundf(HOME_BACKOFF_MM * yStepsPerMm_));

  while (yMotor.distanceToGo() != 0) {
    yMotor.run();
    yield();
  }

  yMotor.setCurrentPosition(0);

  xMm_ = yMm_ = 0;
  return true;
}

void Motion::setPosition(float xMm, float yMm) {
  if (busy_) {
    Debugger::log("setPosition rejected: motion is busy");
    return;
  }

  long pos = lroundf(xMm * xStepsPerMm_);
  Debugger::log(String("setCurrentPosition X=") + String(pos));
  xMotor.setCurrentPosition(pos);

  pos = lroundf(yMm * yStepsPerMm_);
  Debugger::log(String("setCurrentPosition Y=") + String(pos));
  yMotor.setCurrentPosition(pos);

  xMm_ = xMm;
  yMm_ = yMm;
}

void Motion::pen(bool up) {
  const int pulse = up ? PEN_UP_US : PEN_DOWN_US;
  penServo.writeMicroseconds(pulse);
  penUp_ = up;
}

void Motion::stop() {
  penUpBeforeStop_ = penUp_;  // capture the state before raising the pen

  xMotor.setCurrentPosition(xMotor.currentPosition());
  yMotor.setCurrentPosition(yMotor.currentPosition());
  xMm_ = xMotor.currentPosition() / xStepsPerMm_;
  yMm_ = yMotor.currentPosition() / yStepsPerMm_;
  busy_ = false;
  movePaused_ = false;
  pen(true);  // stop still safely raises the physical pen
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
  pausedXTarget_ = xMotor.targetPosition();  // retain your existing logic
  pausedYTarget_ = yMotor.targetPosition();

  moveStartedMs = millis();  // start timing the resumed portion

  xMotor.moveTo(pausedXTarget_);
  yMotor.moveTo(pausedYTarget_);
  busy_ = true;
  movePaused_ = false;
}

bool Motion::setStepsPerMm(float x, float y) {
  if (busy_) return false;
  if (!isfinite(x) || !isfinite(y)) return false;
  if (x < 1.0f || x > 2000.0f || y < 1.0f || y > 2000.0f) return false;

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
  if (x < 1.0f || x > 10000.0f || y < 1.0f || y > 10000.0f) return false;

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
