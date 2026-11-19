#include "WheelResistance.h"
#include <util/atomic.h>

WheelResistance::WheelResistance(DCMotor &dcMotor, MotorEncoder &motorEncoder)
    : motor(dcMotor), encoder(motorEncoder) {}

void WheelResistance::begin()
{
    motor.stop();
    resisting = false;
    initialized = encoder.isReady();
    previousError = 0.0f;
    measuredSpeed = 0.0f;
    pwmOutput = 0;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        previousCount = encoder.readCount();
        sampleReady = false;
    }
    previousSampleUs = micros();
}

void WheelResistance::onTimer()
{
    sampleReady = true;
}

bool WheelResistance::update(bool resistEnabled)
{
    if (!initialized) return false;

    // If resistance is disabled, stop the motor and reset state
    if (!resistEnabled) {
        if (resisting) motor.stop();
        resisting = false;
        previousError = 0.0f;
        measuredSpeed = 0.0f;
        pwmOutput = 0;
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            sampleReady = false;
        }
        return false;
    }

    // If resistance is enabled, but we haven't started a measurement yet, do that now
    if (!resisting) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            previousCount = encoder.readCount();
            sampleReady = false;
        }
        previousSampleUs = micros();
        previousError = 0.0f;
        resisting = true;
        return false;
    }

    bool ready;
    long count = 0;

    // Read the encoder count and check if a new sample is ready
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        ready = sampleReady;
        if (ready) {
            sampleReady = false;
            count = encoder.readCount();
        }
    }
    if (!ready) return false;

    // Calculate the time since last sample
    unsigned long nowUs = micros();
    unsigned long elapsedUs = nowUs - previousSampleUs;
    if (elapsedUs == 0) return false;
    float dt = elapsedUs / 1000000.0f;

    // PID calculations
    measuredSpeed = encoder.countsToDegrees(count - previousCount) / (6.0f * dt);

    float error = -measuredSpeed; // Target speed is zero RPM.
    float sampleError = error * dt;
    float derivative = (error - previousError) / dt;
    float output = kp * error + ki * sampleError + kd * derivative;

    // cap PWM output
    if (output > WheelsConfig::MAX_PWM) output = WheelsConfig::MAX_PWM;
    if (output < -WheelsConfig::MAX_PWM) output = -WheelsConfig::MAX_PWM;
    pwmOutput = static_cast<int>(output);

    if (pwmOutput > 0) motor.setSpeed(false, pwmOutput);
    else if (pwmOutput < 0) motor.setSpeed(true, -pwmOutput);
    else motor.stop(); // Clear the previous command when resistance becomes zero.

    previousCount = count;
    previousSampleUs = nowUs;
    previousError = error;
    return true;
}

void WheelResistance::setGains(float proportional, float integral, float derivative)
{
    kp = proportional;
    ki = integral;
    kd = derivative;
    previousError = 0.0f;
}

float WheelResistance::angle() const { return encoder.readAngle(); }
float WheelResistance::speed() const { return measuredSpeed; }
int WheelResistance::effort() const { return pwmOutput; }
