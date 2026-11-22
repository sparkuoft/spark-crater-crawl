#ifndef PILLAR_LED_H
#define PILLAR_LED_H

#include <stdint.h>
#include "shared_state.h"

/* Pillar warning LEDs (Pillars, P|E; code in LEDGameLogic.cpp). One WS2812B
 * strip on the Game Mega: 5 LEDs per pillar, pillar 0 first. In the laptop
 * build these do nothing (pillar_led_laptop.cpp).
 *
 *   pillarLedsInit()          once, in setup()
 *   pillarLedsHandleCommand() for every PillarCommand the pillar FSM sends
 *   pillarLedsUpdate()        regularly: moves the animations on and sends
 *                             the colours to the strip (FastLED.show()) */
void pillarLedsInit();
void pillarLedsHandleCommand(const PillarCommand &cmd, uint32_t now_ms);
void pillarLedsUpdate(uint32_t now_ms);

#endif
