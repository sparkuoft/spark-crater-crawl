#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <stdint.h>
#include "shared_state.h"

/* Pillar sequencer. Owner: Mahad (GL).
 * Picks which pillars to warn/raise and when, for the current difficulty.
 *
 * Called by the game FSM:
 *   sequencer_update -- every loop while PHASE_RUNNING
 *   sequencer_reset  -- at round start and round end
 *
 * The game FSM calls pillarFsmUpdate() itself every loop, in every phase,
 * so the sequencer should not call it too. */
void sequencer_update(uint32_t now_ms, WarningMode difficulty);
void sequencer_reset();

#endif
