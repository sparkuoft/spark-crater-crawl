#ifndef WHEELS_LINK_H
#define WHEELS_LINK_H

#include <stdint.h>
#include "shared_state.h"

/* Game Mega side of the serial link to the Wheels Mega (format in
 * board_link_protocol.h). The Wheels Mega runs Alice's wheel code and
 * Tracking; this file is how the game talks to both.
 *
 * In:  switch hits (turned into CollisionEvents for game_on_collision) and
 *      player angles (written to g_state.players[i].angle_x10).
 * Out: hit/death triggers (WheelsTrigger), "release everyone" on reset, and
 *      homing requests.
 *
 * In the laptop build there's no second board: sends do nothing and nothing
 * arrives, but wheels_link_handle_line() still works for the tests and the
 * console's "l" command. */

void wheels_link_init();               /* once, in setup() */
void wheels_link_update(uint32_t now_ms); /* every loop: reads incoming lines */

void wheels_link_send_trigger(const WheelsTrigger &t, uint16_t hold_ms);
void wheels_link_release_all();
void wheels_link_home(uint8_t player_id);

/* Handles one line as if it had just arrived from the Wheels Mega. */
void wheels_link_handle_line(const char *line, uint32_t now_ms);

/* While muted nothing is sent (the unit tests use this so they don't move
 * real motors). */
void wheels_link_set_muted(bool muted);

/* Forgets everything the link has seen and drops any half-received line.
 * The unit tests call this when they finish, since they feed it pretend
 * messages at pretend times. */
void wheels_link_restart();

/* What the link has seen, for the console's ? command. */
struct WheelsLinkStatus
{
    bool heard;                         /* any valid message since start-up */
    uint32_t last_heard_ms;
    uint8_t track_state[NUM_PLAYERS];   /* LinkTrackState from the last A message */
    uint16_t bad_lines;                 /* malformed lines received */
};
const WheelsLinkStatus &wheels_link_status();

#endif
