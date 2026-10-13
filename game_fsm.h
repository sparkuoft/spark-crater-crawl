#ifndef GAME_FSM_H
#define GAME_FSM_H

#include <stdint.h>
#include "shared_state.h"

/* Top-level game FSM. Owner: Antoine (GL).
 *
 *   IDLE --start--> COUNTDOWN --COUNTDOWN_MS--> RUNNING
 *   RUNNING --fatal hit--> PLAYER_ELIMINATED --all pillars down--> GAME_OVER
 *   any phase --reset--> IDLE
 *
 * Post-hit lockout is per player (PlayerState.locked_out), not a game phase:
 * one player being frozen doesn't pause the round for the other. */

/* Game-FSM-only tuning. Nobody outside GL reads these. */
#define COUNTDOWN_MS 3000UL /* PROVISIONAL -- GL (Antoine), 2026-10-13 */
#define PLAYER_LIVES 1      /* PROVISIONAL -- GL (Antoine), 2026-10-13. One hit is fatal for now. */

void game_fsm_init(uint32_t now_ms);
void game_fsm_update(uint32_t now_ms); /* call every loop */

void game_start(uint32_t now_ms); /* IDLE -> COUNTDOWN, ignored in any other phase */
void game_reset(uint32_t now_ms); /* any phase -> IDLE */

/* Collision report from Tracking (T|E). Only counts while RUNNING, and is
 * ignored for a player who is locked out or already eliminated. Forwards
 * player_id + timestamp + fatal to Wheels; the pillar stays on the GL side. */
void game_on_collision(const CollisionEvent &ev, uint32_t now_ms);

WarningMode game_difficulty();

#endif
