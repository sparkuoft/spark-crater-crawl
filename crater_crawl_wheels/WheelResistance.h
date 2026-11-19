#ifndef WHEEL_RESISTANCE_H
#define WHEEL_RESISTANCE_H

#include <Arduino.h>
#include "DCMotor.h"
#include "MotorEncoder.h"
#include "WheelsPin.h"

// One instance per motor. MotorEncoder owns the count and angle measurements.
class WheelResistance 
{
public:
    WheelResistance(DCMotor &motor, MotorEncoder &encoder);

    void begin(); // Call after motor.attach() and encoder.begin(); starts disabled.
    void onTimer(); // Call from the shared timer ISR; only sets a sample flag.

    // Call every loop: true enables resistance, false releases the motor.
    // Returns true only when a fresh control calculation was performed.
    bool update(bool resistEnabled);

    void setGains(float kp, float ki, float kd);
    float angle() const; // Convenience wrapper for encoder.readAngle(), also while disabled.
    float speed() const; // Last measured RPM.
    int effort() const;  // Last signed PWM command, -255..255.

private:
    // passed motor and encoder objects
    DCMotor &motor;
    MotorEncoder &encoder;

    // State for the timer ISR and update() to communicate
    volatile bool sampleReady = false;
    bool resisting = false;
    bool initialized = false;

    // State for PID calculations
    long previousCount = 0;
    unsigned long previousSampleUs = 0;
    float previousError = 0.0f;

    // PID gains
    float kp = WheelsConfig::KP;
    float ki = WheelsConfig::KI;
    float kd = WheelsConfig::KD;

    // Last measured speed and PWM output
    float measuredSpeed = 0.0f;
    int pwmOutput = 0;
};

#endif

// helena was here again trying again
//practicing one more time