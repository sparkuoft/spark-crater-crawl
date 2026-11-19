#include "ServoMotor.h"

bool ServoMotor::attach(const uint8_t pins[5], const uint8_t initialAngles[5])
{
    if (attached || pins == nullptr || initialAngles == nullptr) return false;

    // Check the whole configuration before changing any outputs.
    for (uint8_t i = 0; i < 5; ++i) {
        if (pins[i] >= NUM_DIGITAL_PINS || initialAngles[i] > 180) return false;
        for (uint8_t j = 0; j < i; ++j) {
            if (pins[i] == pins[j]) return false;
        }
    }

    for (uint8_t i = 0; i < 5; ++i) {
        // Servo index 0 is valid. INVALID_SERVO means allocation failed.
        if (servos[i].attach(pins[i]) == INVALID_SERVO) {
            for (uint8_t j = 0; j < i; ++j) servos[j].detach();
            return false;
        }
    }
    for (uint8_t i = 0; i < 5; ++i) servos[i].write(initialAngles[i]);
    attached = true;
    return true;
}

bool ServoMotor::start(const ServoPose newPattern[], uint8_t newPoseCount)
{
    if (!attached || playing || newPattern == nullptr || newPoseCount == 0) return false;

    unsigned long totalMs = 0;
    for (uint8_t pose = 0; pose < newPoseCount; ++pose) {
        if (newPattern[pose].durationMs == 0) return false;
        totalMs += newPattern[pose].durationMs;
        if (totalMs > 1200UL) return false;
        for (uint8_t servo = 0; servo < 5; ++servo) {
            if (newPattern[pose].angles[servo] > 180) return false;
        }
    }

    pattern = newPattern;
    poseCount = newPoseCount;
    currentPose = 0;
    startedAtMs = millis();
    for (uint8_t i = 0; i < 5; ++i) servos[i].write(pattern[0].angles[i]);
    playing = true;
    return true;
}

void ServoMotor::update()
{
    if (!playing) return;

    // Unsigned subtraction works across millis() rollover.
    unsigned long elapsedMs = millis() - startedAtMs;
    unsigned long poseEndMs = 0;
    uint8_t nextPose = poseCount - 1;
    for (uint8_t pose = 0; pose < poseCount; ++pose) {
        poseEndMs += pattern[pose].durationMs;
        if (elapsedMs < poseEndMs) {
            nextPose = pose;
            break;
        }
    }

    // If loop() was delayed, send only the latest pose rather than replaying old ones.
    if (nextPose != currentPose) {
        for (uint8_t i = 0; i < 5; ++i) servos[i].write(pattern[nextPose].angles[i]);
        currentPose = nextPose;
    }

    // When no pose matched, poseEndMs is the total pattern duration.
    // Final targets remain active after playback ends.
    if (elapsedMs >= poseEndMs) playing = false;
}

void ServoMotor::stop()
{
    playing = false;
}

bool ServoMotor::isPlaying() const
{
    return playing;
}
