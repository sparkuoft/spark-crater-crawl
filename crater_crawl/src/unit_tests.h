#ifndef UNIT_TESTS_H
#define UNIT_TESTS_H

#include <stdint.h>

/* Unit tests for the Game Mega's logic: game FSM, pillar FSM, sequencer and
 * the messages from the Wheels Mega. Run them on the board by typing t in the
 * Serial Monitor, or on the laptop with ./test --test.
 *
 * They use a pretend clock, so a full round takes a few seconds; the LEDs and
 * the Wheels Mega aren't touched while they run. They reset the game when
 * they finish. Returns true if every check passed. */
bool run_unit_tests(uint32_t now_ms);

#endif
