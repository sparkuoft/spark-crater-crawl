// testing stepper movement with a target angle of 180 degrees

#define STEPS_PER_REV 1600
#define TARGET_ANGLE 180

#include <Arduino.h>

int stepsRequired = TARGET_ANGLE * STEPS_PER_REV / 360;

int stepCount = 0;

const int stepPin = 3;

const int dirPin = 4; 

void setup() {

  // Sets the two pins as Outputs

  pinMode(stepPin,OUTPUT);

  pinMode(dirPin,OUTPUT);

  digitalWrite(dirPin,HIGH); //Enables the motor to move in a particular direction

  Serial.begin(9600);

  Serial.println(stepsRequired);

  }

void loop() {

  if (stepCount < stepsRequired) {

    digitalWrite(stepPin, HIGH);

    delayMicroseconds(1000);

    digitalWrite(stepPin, LOW);

    delayMicroseconds(1000);

    stepCount++;
  }

}


