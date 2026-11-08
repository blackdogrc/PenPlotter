#pragma once

// EXAMPLE GPIOs ONLY: replace with pins available on your exact ESP32-C3 board.
constexpr int X_STEP_PIN = 4;
constexpr int X_DIR_PIN = 5;
constexpr int Y_STEP_PIN = 6;
constexpr int Y_DIR_PIN = 7;

constexpr int X_MIN_PIN = 0;
constexpr int Y_MIN_PIN = 1;
constexpr int SERVO_PIN = 10;

// Set true if your endstop connects the input to GND when pressed.
constexpr bool ENDSTOPS_ACTIVE_LOW = true;

// Assumes GT2 (2 mm pitch), 20-tooth pulleys, 1.8° motors, A4988 at 1/16.
constexpr float X_STEPS_PER_MM = 80.0f;
constexpr float Y_STEPS_PER_MM = 80.0f;

constexpr float MAX_FEED_MM_MIN = 20000.0f;
constexpr float MAX_ACCELERATION = 3000.0f;
constexpr float HOMING_FEED_MM_MIN = 700.0f;
constexpr float HOME_BACKOFF_MM = 3.0f;
constexpr float DEFAULT_DRAWING_FEED_MM_MIN = 4000.0f;

// Calibrate these for your pen linkage and servo.
constexpr int PEN_UP_US = 1700;
constexpr int PEN_DOWN_US = 1100;

constexpr char WIFI_SSID[] = "FergieBot-pen-plotter";
constexpr char WIFI_PASSWORD[] = "ferg1234";

// Prevent USB and web commands from controlling the plotter simultaneously.
constexpr bool CONTROL_SOURCE_LOCKING_ENABLED = true;


