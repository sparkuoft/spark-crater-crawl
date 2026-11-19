#ifndef SERVO_MOTOR_H
#define SERVO_MOTOR_H

#include <Arduino.h>
#include <Servo.h>

// One pose commands all five servos, then gives them time to move/hold.
struct ServoPose {
    uint8_t angles[5];    // Degrees for servos 0..4. Use tested linkage-safe angles.
    uint16_t durationMs; // Time allocated to this pose before the next pose.
};

// One object controls one character's FIVE standard positional servos.
// Create two objects for two characters. Arduino Servo handles individual pulses.
// Call update() frequently from loop() to advance the animation without blocking.
class ServoMotor {
public:
    // Call once in setup(): attach five signal pins and command five initial angles.
    // Both arrays must contain exactly five entries in the same servo order.
    // Return false if already attached or setup fails; all five must attach successfully.
    bool attach(const uint8_t pins[5], const uint8_t initialAngles[5]);

    // Call ONCE when a hit starts. Command the first pose and record millis().
    // Return false if unattached, already playing, or given an invalid pattern.
    // Require a non-null pattern, at least one pose, angles 0..180, positive durations,
    // and a total duration <= 1200 ms for the game's hit animation.
    // Keep the pattern array alive and unchanged during playback (e.g. global const).
    bool start(const ServoPose pattern[], uint8_t poseCount);

    // Call every loop(). Use elapsed millis() to select the current pose.
    // Command all five servos for each new pose with Servo::write().
    // No delay() and no waiting for a servo to arrive. If late, skip expired poses.
    // At the total duration, command the final pose if needed and clear playing.
    // Completion means the schedule ended; ordinary servos provide no arrival feedback.
    void update();

    // Cancel playback. Leave the servos attached, holding their last commanded angles.
    void stop();

    // Return whether the animation schedule is still running.
    bool isPlaying() const;

private:
    Servo servos[5];
    const ServoPose *pattern = nullptr;
    uint8_t poseCount = 0;
    uint8_t currentPose = 0;
    unsigned long startedAtMs = 0;
    bool attached = false;
    bool playing = false;
};

// Example pattern (illustrative angles; tune for the actual mechanism):
// const ServoPose hitPattern[] = {
//     {{90, 90, 90, 90, 90}, 400}, // First pose: 400 ms.
//     {{60, 120, 60, 120, 90}, 400}, // Second pose: 400 ms.
//     {{90, 90, 90, 90, 90}, 400}  // Return pose: 400 ms. Total = 1200 ms.
// };
// character.start(hitPattern, 3); // Once per hit, after attach().
// character.update();             // Every loop, alongside game logic/resistance.

#endif
