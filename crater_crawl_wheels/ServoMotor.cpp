#include "ServoMotor.h"
#include <Arduino.h>

// ServoMotor state machine constants and limits
namespace {
    const unsigned long REACTION_MS = 200;
    const unsigned long ANIMATION_MS = 600;
    const unsigned long RETURN_MS = 1000;
    const uint8_t REACTION_ANGLE = 90;
    const uint8_t ANIMATION_ANGLE = 100;
}

// Attach the servo to a pin and set its rest angle. Returns true if successful.
bool ServoMotor::attach(uint8_t pin, uint8_t restAngleDegrees)
{
    // attaches the pin and checks for success
    if (servo.attach(pin) == INVALID_SERVO)
        return false;

    // sets the angle
    restAngle = restAngleDegrees;
    servo.write(restAngle);

    attached = true;
    return true;
}

// Update the servo's state machine based on the hit signal and elapsed time.
void ServoMotor::update(bool hitActive, unsigned long nowMs)
{
    if (!attached) return;
    if (!hitActive) hitPlayed = false;

    const unsigned long elapsed = nowMs - stateStartedMs;
    switch (state)
    {
    case State::Rest:
        if (hitActive && !hitPlayed)
        {
            hitPlayed = true;
            stateStartedMs = nowMs;
            state = State::React;
            servo.write(REACTION_ANGLE);
        }
        break;

    case State::React:
        if (elapsed >= REACTION_MS)
        {
            stateStartedMs = nowMs;
            state = State::Animate;
            servo.write(ANIMATION_ANGLE);
        }
        break;

    case State::Animate:
        if (elapsed >= ANIMATION_MS)
        {
            stateStartedMs = nowMs;
            state = State::Return;
            servo.write(restAngle);
        }
        else
        {
            // Dip back to the reaction angle for 100 ms halfway through the wiggle.
            servo.write(elapsed >= 300 && elapsed < 400 ? REACTION_ANGLE : ANIMATION_ANGLE);
        }
        break;

    case State::Return:
        if (elapsed >= RETURN_MS) state = State::Rest;
        break;
    }
}

// All servos in a group receive the same hit signal and timestamp. 
// ANimate all at the same time
void ServoMotor::updateAll(ServoMotor* const servos[], size_t count, bool hitActive, unsigned long nowMs)
{
    for (size_t i = 0; i < count; ++i)
    {
        ServoMotor* servo = servos[i];
        if (servo != nullptr)
            servo->update(hitActive, nowMs);
    }
}
