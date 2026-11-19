#ifndef __DC_MOTOR_H__
#define __DC_MOTOR_H__

class DCMotor
{
public:
    DCMotor();
    ~DCMotor();

    // set up function
    void attach(unsigned int IN1, unsigned int IN2, unsigned int PWM, unsigned int STBY);
    void invertDirection();

    // function to set speed and direction
    void setSpeed(unsigned int value);
    void setSpeed(bool direction, unsigned int value);

    void stop(); // coast: release active drive so the wheel can turn freely
    void brake(); // sudden stop

private:
    unsigned int in1, in2, pwm, stby;
    bool isAttached = 0;
    bool isInvertDirection = 0;
    bool isSetDirection = 0;
};

#endif
