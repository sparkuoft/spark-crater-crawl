#include "DCMotor.h"
#include "MotorEncoder.h"
#include "WheelResistance.h"
#include "WheelsPin.h"
#include "AS5600.h"
#include "tracking_hal.h"
#include "game_link.h"

#include <TCA9548A.h>
#include <TimerOne.h> // might need to change
#include <Wire.h>
#include <math.h> // lroundf() rounds target positions to whole steps
#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>
#include <Servo.h>

// MotorA:: contains pin settings; motorA is the actual motor object.
DCMotor motorA;
MotorEncoder encoderA(MotorA::EN_A, MotorA::EN_B, WheelsConfig::ENCODER_CPR);
WheelResistance resistance(motorA, encoderA);

// mux and encoder channel
TCA9548A mux;
const bool USE_STEPPER_MUX = true; // Set false if AS5600 connects directly to Arduino I2C.
const uint8_t STEPPER_MUX_CHANNEL = 0; // Match the channel used by the stepper AS5600.

// Nema 17 
const int stepPin = 52;
const int dirPin = 53;
AS5600 stepperEncoder;

// Stepper motor configuration
const int FULL_STEPS_PER_REV = 200; 
const int MICROSTEPS = 8;
const float GEAR_RATIO = 1.0; // 1:1 gearing

// actual steps per revolution of the stepper shaft
const int stepsPerRev = FULL_STEPS_PER_REV * MICROSTEPS * GEAR_RATIO;

// Stepper motor state variables
const int DIRECTION_SIGN = -1; // 1 for normal, -1 for inverted direction
const float MAX_STEP_SPEED = 600.0; // Maximum speed in degrees per second (one turn/sec)
const float ACCELERATION_DEG_PER_SEC2 = 180.0; // Acceleration in degrees per second squared

// Stepper motor control variables
float controller_inputZeroDeg = 0.0f; // Controller Wheel: encoder angle at startup for zero position
long character_targetSteps = 0; // Character Wheel: target stepper position 
long controller_commandedSteps = 0; // Controller Wheel: Signed step count issued to stepper 
float character_stepperDeg = 0.0f; // AS5600 absolute angle (0..360), monitoring only
bool followingReady = false;
const unsigned long AS5600_READ_INTERVAL_MS = 20;
const unsigned long TELEMETRY_INTERVAL_MS = 200;
unsigned long lastAS5600ReadMs = 0;

// Nonblocking STEP pulse timing
unsigned long lastStepMicros = 0; // Time of last step pulse
unsigned long pulseStartMicros = 0; // Time when STEP pin HIGH
bool stepPulseHigh = false; // whether STEP is currently HIGH

// Acceleration/deceleration state
float currentSpeed = 0.0f; // Current speed in degrees per second
unsigned long lastSpeedUpdatedMicros = 0; // Time when speed was last updated

// set this from game logic
// true = DC resistance with stepper paused; false = release DC and follow input
// Set every loop: on while the Game Mega has this player frozen (hit) or held
// (fatal hit), or while manualResist is on.
bool resistEnabled = false;
bool manualResist = false; // '1' / '0' in the Serial Monitor, for testing

//---------------- Encoder ISRs ----------------

void encoderA_ISR()
{
    encoderA.onChannelA();
}

void encoderB_ISR()
{
    encoderA.onChannelB();
}

//---------------- Timer ISR ----------------

void timerISR()
{
    resistance.onTimer();
}

//---------------- Servo 9g ----------------

bool hit_status = resistEnabled;
bool dead = false;
// bool previous_hit_status = false;

Servo animate_servo;

int pos = 180;

unsigned long start_time = 0;
unsigned long current_time_stamp = 0;

// State machine
enum ServoState {
    REST,
    REACT,
    ANIMATE,
    RETURN
};

ServoState servo_state = REST;

void servo_animate_update() {

    hit_status = resistEnabled;  // Refresh the hit status each call
    current_time_stamp = millis();

    switch (servo_state) {

        case REST:

            animate_servo.write(180);

            // only go into first state of FSM if hit = true and its not already dead
            if (hit_status && !dead) {
                dead = true;
                start_time = current_time_stamp;
                servo_state = REACT;
                Serial.println("Starting");
            }

            break;


        case REACT:

            // Move to reaction position
            animate_servo.write(90);

            if (current_time_stamp - start_time >= 200) {
                start_time = current_time_stamp;
                servo_state = ANIMATE;
                //Serial.println("react");
            }

            break;


        case ANIMATE:

            // Small animation around 90 degrees
            if (current_time_stamp - start_time < 300) {
                animate_servo.write(100);
                // Serial.println("animate");
            }
            else if (current_time_stamp - start_time < 400) {
                animate_servo.write(90);
            }
            else if (current_time_stamp - start_time < 600) {
                animate_servo.write(100);
            }
            else {
                start_time = current_time_stamp;
                servo_state = RETURN;
            }

            break;


        case RETURN:

            // Return to resting position
            animate_servo.write(180);
            // Serial.println("return");

            // after one second, go back to rest state
            // within the 1.2 second limit
            if (current_time_stamp - start_time >= 1000) {
                servo_state = REST;
            }

            break;
    }

    // Update previous hit status every loop
    // previous_hit_status = hit_status;
}

//motor code


float readStepperAngle()
{
    if (USE_STEPPER_MUX)
    {
        mux.closeAll();
        mux.openChannel(STEPPER_MUX_CHANNEL);
    }

    // The existing AS5600 library already returns degrees.
    float degrees = stepperEncoder.readAngle();

    if (USE_STEPPER_MUX)
    {
        mux.closeChannel(STEPPER_MUX_CHANNEL);
    }
    return degrees;
}

// Tracking asks for a fresh angle right after a photo-interrupter fires.
// Only player 0's AS5600 is wired so far.
bool readCharacterAngle(uint8_t player, float *degrees)
{
    if (player != 0)
        return false;
    *degrees = readStepperAngle();
    return true;
}

void updateStepperTarget()
{
    if (!followingReady || !encoderA.isReady())
    {
        followingReady = false;
        return;
    }

    // Keep signed, multi-turn travel relative to the startup angle.
    float inputTravelDeg = encoderA.readAngle() - controller_inputZeroDeg;
    if (!isfinite(inputTravelDeg))
    {
        followingReady = false;
        return;
    }
    character_targetSteps = lroundf(inputTravelDeg * stepsPerRev / 360.0f * DIRECTION_SIGN);
}

void serviceStepper()
{
    // TMC2209 datasheet Rev. 1.08, section 13.1 (page 63), DEDGE=0:
    // https://www.analog.com/media/en/technical-documentation/data-sheets/tmc2209_datasheet_rev1.08.pdf
    // STEP HIGH/LOW: max(tFILTSD, tCLK + 20 ns); DIR setup/hold: 20 ns.
    // Use 5 us margins for the internal driver clock and coarse Arduino micros().
    const unsigned long STEP_HIGH_US = 5;
    const unsigned long STEP_LOW_US = 5;
    const unsigned long DIR_SETUP_US = 5;
    const unsigned long DIR_HOLD_US = 5;
    static unsigned long lastLowMicros = 0;
    static unsigned long directionChangedMicros = 0;
    static int pulseDirection = 0;
    static bool initialized = false;
    unsigned long now = micros();

    if (!initialized)
    {
        lastStepMicros = lastSpeedUpdatedMicros = lastLowMicros = now;
        initialized = true;
    }

    // Finish every pulse before considering another step or direction change.
    if (stepPulseHigh)
    {
        if (now - pulseStartMicros >= STEP_HIGH_US)
        {
            digitalWrite(stepPin, LOW);
            lastLowMicros = micros();
            stepPulseHigh = false;
        }
        return;
    }

    // Resistance mode pauses pulses but preserves position for catch-up on release.
    if (resistEnabled || !followingReady || !encoderA.isReady() || stepsPerRev <= 0 ||
        MAX_STEP_SPEED <= 0.0f || ACCELERATION_DEG_PER_SEC2 <= 0.0f)
    {
        currentSpeed = 0.0f;
        lastStepMicros = lastSpeedUpdatedMicros = now;
        return;
    }

    long remainingSteps = character_targetSteps - controller_commandedSteps;
    if (remainingSteps == 0)
    {
        currentSpeed = 0.0f;
        lastStepMicros = lastSpeedUpdatedMicros = now;
        return;
    }

    // Limit speed by the distance available for braking; keep units in degrees.
    float remainingDeg = fabsf((float)remainingSteps) * 360.0f / stepsPerRev;
    float desiredSpeed = fminf(MAX_STEP_SPEED,
        sqrtf(2.0f * ACCELERATION_DEG_PER_SEC2 * remainingDeg));
    if (remainingSteps < 0) desiredSpeed = -desiredSpeed;

    // Brake to zero before reversing when the input wheel changes direction.
    if ((currentSpeed > 0.0f && desiredSpeed < 0.0f) ||
        (currentSpeed < 0.0f && desiredSpeed > 0.0f))
    {
        desiredSpeed = 0.0f;
    }

    unsigned long elapsed = now - lastSpeedUpdatedMicros;
    if (elapsed >= 1000UL)
    {
        // A slow loop must not cause a large jump in speed when it resumes.
        float dt = fminf(elapsed * 0.000001f, 0.01f);
        float speedChange = ACCELERATION_DEG_PER_SEC2 * dt;
        bool starting = (currentSpeed == 0.0f);
        if (currentSpeed < desiredSpeed)
            currentSpeed = fminf(currentSpeed + speedChange, desiredSpeed);
        else
            currentSpeed = fmaxf(currentSpeed - speedChange, desiredSpeed);
        lastSpeedUpdatedMicros = now;
        if (starting) lastStepMicros = now;
    }
    if (currentSpeed == 0.0f) return;

    // Preserve STEP LOW time between pulses.
    if (now - lastLowMicros < STEP_LOW_US) return;
    int nextDirection = currentSpeed > 0.0f ? 1 : -1;
    if (nextDirection != pulseDirection)
    {
        // DIR must remain stable after the previous STEP rising edge.
        if (now - lastStepMicros < DIR_HOLD_US) return;
        digitalWrite(dirPin, nextDirection > 0 ? HIGH : LOW);
        directionChangedMicros = micros();
        pulseDirection = nextDirection;
        return;
    }
    if (now - directionChangedMicros < DIR_SETUP_US) return;

    float intervalUs = 360.0f * 1000000.0f / (fabsf(currentSpeed) * stepsPerRev);
    if ((float)(now - lastStepMicros) < intervalUs) return;

    // Issue at most one pulse per call; count its rising edge exactly once.
    digitalWrite(stepPin, HIGH);
    pulseStartMicros = micros();
    lastStepMicros = pulseStartMicros;
    stepPulseHigh = true;
    controller_commandedSteps += pulseDirection;
}

// Report angles without waiting for the serial transmit buffer to empty.
void reportTelemetry()
{
    static unsigned long lastReportMs = 0;
    static char message[128];
    static size_t length = 0;
    static size_t sent = 0;
    unsigned long now = millis();

    if (sent == length && now - lastReportMs >= TELEMETRY_INTERVAL_MS)
    {
        lastReportMs = now;
        char inputText[20], commandedText[20], measuredText[20];
        dtostrf(encoderA.readAngle() - controller_inputZeroDeg, 1, 1, inputText);
        dtostrf(controller_commandedSteps * 360.0f / stepsPerRev, 1, 1, commandedText);
        dtostrf(character_stepperDeg, 1, 1, measuredText);
        int count = snprintf(message, sizeof(message),
            "Input=%s Cmd=%s AS5600=%s deg | Follow=%d Resist=%d DC_PWM=%d\n",
            inputText, commandedText, measuredText,
            followingReady && !resistEnabled, resistEnabled, resistance.effort());
        length = count > 0 ? (size_t)count : 0;
        if (length >= sizeof(message)) length = sizeof(message) - 1;
        sent = 0;
    }

    // Send small chunks so pulse generation gets serviced between writes.
    int available = Serial.availableForWrite();
    if (sent < length && available > 0)
    {
        size_t chunk = length - sent;
        if (chunk > 16) chunk = 16;
        if (chunk > (size_t)available) chunk = (size_t)available;
        sent += Serial.write((const uint8_t *)message + sent, chunk);
    }
}

//---------------- Setup ----------------

void setup()
{
    Serial.begin(115200);

    // motor writing
    digitalWrite(stepPin, LOW);
    digitalWrite(dirPin, LOW);
    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    Wire.begin();

    // stepper mux init ------------
    if (USE_STEPPER_MUX)
    {
        mux.begin(Wire);
        mux.closeAll();
        mux.openChannel(STEPPER_MUX_CHANNEL);
    }

    stepperEncoder.init();

    if (USE_STEPPER_MUX)
    {
        mux.closeChannel(STEPPER_MUX_CHANNEL);
    }

    character_stepperDeg = readStepperAngle();
    lastAS5600ReadMs = millis();

    // set up motor A with the attached pin configs
    motorA.attach(MotorA::IN1, MotorA::IN2, MotorA::PWM, MotorA::STBY);

    // Encoder class sets up the pins and both hardware interrupts.
    if (!encoderA.begin(encoderA_ISR, encoderB_ISR))
    {
        Serial.println(F("Encoder setup failed: check pins and counts per revolution."));
        resistEnabled = false;
    }
    
    // Treat the current input and stepper positions as the start of relative motion.
    // AS5600 readings are for display only and do not enable or correct motion.
    if (encoderA.isReady())
    {
        controller_inputZeroDeg = encoderA.readAngle();
        character_targetSteps = 0;
        controller_commandedSteps = 0;
        currentSpeed = 0.0f;
        followingReady = isfinite(controller_inputZeroDeg);
    }

    // set up the wheel resistance controller
    resistance.begin();
    resistance.setGains(WheelsConfig::KP, WheelsConfig::KI, WheelsConfig::KD);

    // set up the timer interrupt for the wheel resistance controller
    Timer1.initialize(WheelsConfig::PERIOD_MS * 1000UL);
    Timer1.attachInterrupt(timerISR);

    animate_servo.attach(9); // motor pin 
    animate_servo.write(180);

    // Tracking (photo-interrupters, hit switches) and the link to the Game Mega.
    if (!tracking_begin(readCharacterAngle, millis()))
    {
        Serial.println(F("Tracking setup failed: check tracking_hal.cpp settings."));
    }
    game_link_begin();
}

//---------------- Loop ----------------

void loop()
{
    // Read mode changes before issuing any new step pulses.
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == '1')
        {
            manualResist = true;
        }
        else if (command == '0')
        {
            manualResist = false;
            dead = false;
        }
    }

    resistEnabled = manualResist || game_link_player_frozen(0, millis());
    if (!resistEnabled)
        dead = false; // released: the next hit plays the servo animation again

    servo_animate_update();

    updateStepperTarget();
    resistance.update(resistEnabled);
    serviceStepper();
    // Finish any already-started pulse even when switching into resistance mode.
    if (stepPulseHigh) return;

    // Read the library's absolute angle periodically; it never changes the target.
    unsigned long now = millis();
    if (now - lastAS5600ReadMs >= AS5600_READ_INTERVAL_MS)
    {
        character_stepperDeg = readStepperAngle();
        lastAS5600ReadMs = millis();
        tracking_note_angle(0, character_stepperDeg, lastAS5600ReadMs);
    }
    serviceStepper();
    if (stepPulseHigh) return;

    // Tracking and the Game Mega link: neither waits on anything.
    tracking_update(millis());
    game_link_update(millis());
    serviceStepper();
    if (stepPulseHigh) return;

    reportTelemetry();
    serviceStepper();
}
