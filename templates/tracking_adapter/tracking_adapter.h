#ifndef TRACKING_ADAPTER_H
#define TRACKING_ADAPTER_H

#include <stdint.h>

/* Tracking adapter (GL). The only place the game calls Tracking's code.
 * Tracking reports where each player is and when a player's limit switch is
 * pressed; the game decides what a hit means.
 *
 * Call order in crater_crawl.ino:
 *   setup(): game_fsm_init(now); console_init(now);
 *            tracking_init();
 *            tracking_home(0, now); tracking_home(1, now);
 *   loop():  [Serial command] -> tracking_update(now) -> game_fsm_update(now)
 *            -> console_tick(now)
 */

/* Sets up both players' Tracking modules, then the pins and interrupts.
 * Call once, after game_fsm_init(). Returns false if Tracking rejects the
 * configuration. */
bool tracking_init();

/* Starts homing one player. true means homing STARTED, not finished: keep
 * calling tracking_update() and wait for tracking_ready(). */
bool tracking_home(uint8_t player_id, uint32_t now_ms);

/* One tracking pass. Call every loop, in every game phase, before
 * game_fsm_update(). */
void tracking_update(uint32_t now_ms);

/* True when both players are homed and their latest angles are valid. Use it
 * as the start check before game_start(). */
bool tracking_ready();

/* Called from the photo-interrupter interrupt for that player (see
 * tracking_hal.cpp). Not for normal code. */
void tracking_reference_edge(uint8_t player_id, uint32_t crossing_ms);

#endif
