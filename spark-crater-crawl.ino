/*
 * Crater Crawl -- main sketch, Arduino Mega 2560
 * Spark Design Club, 2026-2027
 *
 * Owns g_state and the main loop. Everything is non-blocking: each module's
 * update runs once per loop with the same millis() snapshot.
 */

#include "shared_state.h"
#include "game_fsm.h"

#define SERIAL_BAUD 115200

SharedState g_state;

void setup()
{
    Serial.begin(SERIAL_BAUD);
    game_fsm_init(millis());
}

void loop()
{
    uint32_t now_ms = millis();
    game_fsm_update(now_ms);
}
