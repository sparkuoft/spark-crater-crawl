#include "AS5600.h"

AS5600::AS5600()
{
}

// initializing AS5600
void AS5600::init()
{
    // Wire.begin();

    // set read pointer in reg file
    Wire.beginTransmission(addr);
    Wire.write(angleReg);
    Wire.endTransmission();
}

// Read encoder value (0-4095), or -1 if the AS5600 didn't answer (GL)
int AS5600::readEncoder()
{
    // GL: set the read pointer on every read, not only in init(). If the AS5600
    // powers up after init() or restarts, its pointer goes back to register 0
    // and every read would return the same wrong value.
    Wire.beginTransmission(addr);
    Wire.write(angleReg);
    if (Wire.endTransmission() != 0)
    {
        return -1; // GL: no answer (sensor, wiring or mux channel)
    }

    // GL: without this check a missing sensor reads as 4095, i.e. a steady 359.9 deg
    if (Wire.requestFrom(addr, 2) != 2)
    {
        return -1;
    }

    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    int encoderVal = ((msb & 0x0F) << 8) | lsb; // masking

    return encoderVal;
}

// Read angle value (0-360), NAN if the read failed (GL)
float AS5600::readAngle()
{
    int encoderVal = readEncoder();
    if (encoderVal < 0)
    {
        return NAN; // GL: failed read, same as MotorEncoder::readAngle() when not ready
    }
    float degrees = (encoderVal / 4096.0) * 360.0;

    // handles negative values after subtracting offset
    float correctedAngle = fmod(degrees - offset, 360.0);
    if (correctedAngle < 0)
    {
        correctedAngle += 360.0;
    }

    return correctedAngle;
}

// Read current angle and set it as offset
void AS5600::setZeroPos()
{
    float currentAngle = readAngle();
    if (isnan(currentAngle))
    {
        return; // GL: failed read, keep the old offset (a NAN offset would make every later angle NAN)
    }
    offset = currentAngle;
}