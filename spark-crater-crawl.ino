/*
 * Crater Crawl -- main sketch, Arduino Mega 2560
 * Spark Design Club, 2026-2027
 *
 * Owns g_state and the main loop. Everything is non-blocking: each module's
 * update runs once per loop with the same millis() snapshot.
 *
 * serial_console is for bench testing without hardware: open the Serial
 * Monitor at SERIAL_BAUD with "Newline" line ending. Commands are listed in
 * serial_console.h and printed on boot.
 */

#include "shared_state.h"
#include "game_fsm.h"
#include "serial_console.h"

#define SERIAL_BAUD 115200

SharedState g_state;

void setup()
{
    Serial.begin(SERIAL_BAUD);
    uint32_t now_ms = millis();
    game_fsm_init(now_ms);
    serial_console_init(now_ms);
}

void loop()
{
    uint32_t now_ms = millis();
    serial_console_poll(now_ms);
    game_fsm_update(now_ms);
}
