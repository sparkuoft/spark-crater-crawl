#ifndef GAME_LINK_H
#define GAME_LINK_H

#include <Arduino.h>

/* Wheels Mega side of the serial link to the Game Mega (format in
 * src/board_link_protocol.h, Serial2 on pins 16/17).
 *
 * In:  T (player hit: freeze for a while, or hold until reset if fatal),
 *      X (release everyone), M (home a player again).
 * Out: B once at boot, H for every hit-switch press, A with each player's
 *      angle and tracking state every 100 ms.
 *
 * Never waits on the serial port: anything that doesn't fit in the send
 * buffer goes out on a later loop, so the stepper timing isn't disturbed. */

void game_link_begin();                 /* once in setup(), after tracking_begin() */
void game_link_update(uint32_t now_ms); /* every loop */

/* true while that player is frozen by a hit, or held after a fatal hit.
 * The sketch turns this into resistEnabled. */
bool game_link_player_frozen(uint8_t player_id, uint32_t now_ms);

#endif
