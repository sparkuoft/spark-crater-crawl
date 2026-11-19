#include <Arduino.h>
#include <Servo.h>

bool hit_status = false;
bool previous_hit_status = false;

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


void setup() {
    animate_servo.attach(9);
    animate_servo.write(180);
    Serial.begin(9600);
}


void loop() {
    game_logic();
}

void game_logic() {

    // Read input from Serial Monitor
    if (Serial.available() > 0) {
        char input = Serial.read();

        if (input == 'h') {
            hit_status = true;
        }
        else if (input == 'r') {
            hit_status = false;
        }
    }

    servo_animate_update();
}


void servo_animate_update() {

    current_time_stamp = millis();

    // Detect rising edge: false -> true
    bool hit_rising_edge = hit_status && !previous_hit_status;

    switch (servo_state) {

        case REST:

            animate_servo.write(180);

            // Start only when a new hit is detected
            if (hit_rising_edge) {
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
                Serial.println("react");
            }

            break;


        case ANIMATE:

            // Small animation around 90 degrees
            if (current_time_stamp - start_time < 1000) {
                animate_servo.write(100);
                Serial.println("animate");
            }
            else if (current_time_stamp - start_time < 2000) {
                animate_servo.write(90);
            }
            else if (current_time_stamp - start_time < 3000) {
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
            Serial.println("return");

            if (current_time_stamp - start_time >= 5000) {
                servo_state = REST;
            }

            break;
    }

    // Update previous hit status every loop
    previous_hit_status = hit_status;
}