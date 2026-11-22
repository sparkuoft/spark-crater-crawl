#ifndef SERVO_MOTOR_H
#define SERVO_MOTOR_H

#include <stddef.h>
#include <Servo.h>

// One positional servo and its hit animation.
class ServoMotor
{
public:
    ServoMotor() = default;
    ServoMotor(const ServoMotor&) = delete;
    ServoMotor& operator=(const ServoMotor&) = delete;

    bool attach(uint8_t pin, uint8_t restAngleDegrees);
    void update(bool hitActive, unsigned long nowMs);
    static void updateAll(ServoMotor* const servos[], size_t count, bool hitActive, unsigned long nowMs);

private:
    enum class State { Rest, React, Animate, Return };

    Servo servo;
    State state = State::Rest;
    unsigned long stateStartedMs = 0;
    uint8_t restAngle = 180;
    bool attached = false;
    bool hitPlayed = false;
};

#endif
