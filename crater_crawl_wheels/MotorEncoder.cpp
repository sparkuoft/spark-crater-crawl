#include "MotorEncoder.h"
#include <util/atomic.h>
#include <math.h>

MotorEncoder::MotorEncoder(uint8_t channelA, uint8_t channelB, unsigned long cpr)
    : pinA(channelA), pinB(channelB), countsPerRevolution(cpr) {}

bool MotorEncoder::begin(void (*channelA_ISR)(), void (*channelB_ISR)())
{
    if (ready) return true; // Do not reset an encoder already being used
    if (countsPerRevolution == 0 || pinA == pinB ||
        channelA_ISR == nullptr || channelB_ISR == nullptr ||
        digitalPinToInterrupt(pinA) == NOT_AN_INTERRUPT ||
        digitalPinToInterrupt(pinB) == NOT_AN_INTERRUPT) return false;

    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        count = 0;
        attachInterrupt(digitalPinToInterrupt(pinA), channelA_ISR, CHANGE);
        attachInterrupt(digitalPinToInterrupt(pinB), channelB_ISR, CHANGE);
        ready = true;
    }
    return true;
}

bool MotorEncoder::isReady() const
{
    return ready;
}

void MotorEncoder::onChannelA()
{
    // Same direction/counting convention as the original sketch.
    if (digitalRead(pinA) ^ digitalRead(pinB)) count--;
    else count++;
}

void MotorEncoder::onChannelB()
{
    if (digitalRead(pinA) ^ digitalRead(pinB)) count++;
    else count--;
}

long MotorEncoder::readCount() const
{
    long snapshot;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        snapshot = count;
    }
    return snapshot;
}

float MotorEncoder::countsToDegrees(long counts) const
{
    if (countsPerRevolution == 0) return NAN;
    return counts * 360.0f / countsPerRevolution;
}

float MotorEncoder::readAngle() const
{
    if (!ready) return NAN;
    return countsToDegrees(readCount()); // 1320 counts -> 360 degrees 
}
