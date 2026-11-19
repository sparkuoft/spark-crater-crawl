#ifndef WHEELS_PIN_H
#define WHEELS_PIN_H

#include <Arduino.h>

// Motor A wiring
namespace MotorA {
    // motor driver pins
    const uint8_t PWM = 6;
    const uint8_t IN1 = 8;
    const uint8_t IN2 = 7;
    const uint8_t STBY = 10;

    // encoder pins
    const uint8_t EN_A = 2;
    const uint8_t EN_B = 3;
}

// PID settings and other constants
namespace WheelsConfig {
    const unsigned long ENCODER_CPR = 1320; // Counts per revolution with A/B CHANGE.
    const unsigned long PERIOD_MS = 50;
    const int MAX_PWM = 255; // Resistance effort limit: 0..255.

    // PID gains 
    const float KP = 7.0f;
    const float KI = 1.0f;
    const float KD = 0.0f;
}

#endif
