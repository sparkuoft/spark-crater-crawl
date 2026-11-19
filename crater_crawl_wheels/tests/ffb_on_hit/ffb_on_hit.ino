// FFB on hit resisting change 
// target speed = 0, so the wheel resists turning when enabled.

#include "DCMotor.h"
#include "MotorEncoder.h"
#include "WheelResistance.h"
#include "WheelsPin.h"
#include <TimerOne.h>

// MotorA:: contains pin settings; motorA is the actual motor object.
DCMotor motorA;
MotorEncoder encoderA(MotorA::EN_A, MotorA::EN_B, WheelsConfig::ENCODER_CPR);
WheelResistance resistance(motorA, encoderA);

// set this from game logic: true = resist turning, false = release the wheel.
bool resistEnabled = true;


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

//---------------- Setup ----------------

void setup()
{
    Serial.begin(115200);

    // set up motor A with the attached pin configs
    motorA.attach(MotorA::IN1, MotorA::IN2, MotorA::PWM, MotorA::STBY);

    // Encoder class sets up the pins and both hardware interrupts.
    if (!encoderA.begin(encoderA_ISR, encoderB_ISR))
    {
        Serial.println(F("Encoder setup failed: check pins and counts per revolution."));
        resistEnabled = false;
    }

    // set up the wheel resistance controller
    resistance.begin();
    resistance.setGains(WheelsConfig::KP, WheelsConfig::KI, WheelsConfig::KD);

    // set up the timer interrupt for the wheel resistance controller
    Timer1.initialize(WheelsConfig::PERIOD_MS * 1000UL);
    Timer1.attachInterrupt(timerISR);
}

//---------------- Loop ----------------

void loop()
{
    // test code: toggle resistance on/off with serial commands
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == '1')
        {
            resistEnabled = true;
            Serial.println(F("Resistance ON"));
        }
        else if (command == '0')
        {
            resistEnabled = false;
            Serial.println(F("Resistance OFF"));
        }
    }

    // Call on EVERY pass, so false releases immediately without waiting for a timer tick
    if (resistance.update(resistEnabled))
    {
        Serial.print("Target Speed = 0 | Actual Speed = ");
        Serial.print(resistance.speed());
        Serial.print(" | PWM = ");
        Serial.println(resistance.effort());
    }
}
