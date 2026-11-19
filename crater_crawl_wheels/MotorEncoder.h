#ifndef MOTOR_ENCODER_H
#define MOTOR_ENCODER_H

#include <Arduino.h>

// One object per quadrature encoder
// Counts both edges of both channels
class MotorEncoder 
{
public:
    MotorEncoder(uint8_t pinA, uint8_t pinB, unsigned long countsPerRevolution);

    // Call in setup()
    bool begin(void (*channelA_ISR)(), void (*channelB_ISR)());
    bool isReady() const;

    void onChannelA(); // Call only from this encoder's channel A ISR
    void onChannelB(); // Call only from this encoder's channel B ISR

    long readCount() const;  // read the signed count
    float readAngle() const; // degrees, NAN if not ready
    float countsToDegrees(long counts) const; // conversion function

private:
    const uint8_t pinA, pinB;
    const unsigned long countsPerRevolution;
    volatile long count = 0;
    bool ready = false;
};

#endif
