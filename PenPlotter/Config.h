#pragma once

// EXAMPLE GPIOs ONLY: replace with pins available on your exact ESP32-C3 board.
constexpr int X_STEP_PIN = 4;
constexpr int X_DIR_PIN = 5;
constexpr int Y_STEP_PIN = 6;
constexpr int Y_DIR_PIN = 7;

constexpr int X_MIN_PIN = 0;
constexpr int Y_MIN_PIN = 1;
constexpr int SERVO_PIN = 10;

// Defaults used until overridden by saved web settings.
// With INPUT_PULLUP, pressed-to-GND switches are active-low.
constexpr bool X_ENDSTOP_ACTIVE_LOW_DEFAULT = true;
constexpr bool Y_ENDSTOP_ACTIVE_LOW_DEFAULT = true;

// Assumes GT2 (2 mm pitch), 20-tooth pulleys, 1.8° motors, A4988 at 1/16.
constexpr float X_STEPS_PER_MM = 80.0f;
constexpr float Y_STEPS_PER_MM = 80.0f;
constexpr long BEDSIZE_X = 240;
constexpr long BEDSIZE_Y = 320;
constexpr float MAX_FEED_MM_MIN = 20000.0f;
constexpr float MAX_ACCELERATION = 3000.0f;
constexpr float HOMING_FEED_MM_MIN = 2000.0f;              //not to fast homing speed.  it could slam into the end stop.
constexpr float HOMING_CHUNK_SIZE = 10 * 80.0f;            //1cm
constexpr long HOMING_MAX_SEEK_STEPS_X = BEDSIZE_X * 80L;  //Maximun travel before we give up.
constexpr long HOMING_MAX_SEEK_STEPS_Y = BEDSIZE_Y * 80L;  //Maximun travel before we give up.

constexpr float HOME_BACKOFF_MM = 3.0f;
constexpr float DEFAULT_DRAWING_FEED_MM_MIN = 4000.0f;

// Calibrate these for your pen linkage and servo.
constexpr int PEN_UP_US = 1700;
constexpr int PEN_DOWN_US = 1100;

constexpr char WIFI_SSID[] = "FergieBot-pen-plotter";
constexpr char WIFI_PASSWORD[] = "ferg1234";

// Prevent USB and web commands from controlling the plotter simultaneously.
constexpr bool CONTROL_SOURCE_LOCKING_ENABLED = true;
