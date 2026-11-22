// Laptop build only: there's no LED strip, so the LED calls do nothing. The
// real code is LEDGameLogic.cpp (Mega only).
#ifndef ARDUINO

#include "pillar_led.h"

void pillarLedsInit()
{
}

void pillarLedsHandleCommand(const PillarCommand &cmd, uint32_t now_ms)
{
    (void)cmd;
    (void)now_ms;
}

void pillarLedsUpdate(uint32_t now_ms)
{
    (void)now_ms;
}

#endif // !ARDUINO
