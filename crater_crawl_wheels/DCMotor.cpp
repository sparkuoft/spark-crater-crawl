#include "DCMotor.h"
#include <Arduino.h>

#define MAX_VALUE 255

DCMotor::DCMotor(){}
DCMotor::~DCMotor(){}

void DCMotor::attach(unsigned int IN1, unsigned int IN2, unsigned int PWM, unsigned int STBY)
{
    in1 = IN1;
    in2 = IN2;
    pwm = PWM;
    stby = STBY;
    pinMode(in1, OUTPUT);
    pinMode(in2, OUTPUT);
    pinMode(pwm, OUTPUT);
    pinMode(stby, OUTPUT);
    isAttached = 1;
    stop(); // Establish coast outputs before enabling the driver.
    digitalWrite(STBY, HIGH);
}

void DCMotor::invertDirection()
{
    if (!isAttached) return;
    isInvertDirection = !isInvertDirection;
}

void DCMotor::setSpeed(bool direction, unsigned int value)
{
    if (!isAttached) return;

    digitalWrite(in1, direction ^ isInvertDirection);
    digitalWrite(in2, (!direction) ^ isInvertDirection);
    if (value > 255) value = 255;
    analogWrite(pwm, value);
    isSetDirection = 1;
    // printf("in1 direction now = %d\n", direction^isInvertDirection);
    // printf("in2 direction now = %d\n", !direction^isInvertDirection);
    // Serial.println("set direction");
}

void DCMotor::setSpeed(unsigned int value)
{
    if (!isAttached) return;
    if (value > 255) value = 255;
    if (isSetDirection) analogWrite(pwm, value);
}

void DCMotor::brake()
{
    if (!isAttached) return;

    digitalWrite(in1, HIGH);
    digitalWrite(in2, HIGH);
    isSetDirection = 0;
}

void DCMotor::stop()
{
    if (!isAttached) return;
    analogWrite(pwm, 0);
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
    analogWrite(pwm, 255);
    isSetDirection = 0;
}
