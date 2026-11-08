#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/* Test console: typed commands standing in for hardware that doesn't exist
 * yet. The driver (main.cpp) reads whole lines, handles +<ms>, # comments and
 * q itself, and passes everything else here.
 *
 *   s          start a game
 *   r          reset to IDLE
 *   h<p> [n]   fake a Tracking hit on player p by pillar n: "h1 3". With no n
 *              it uses a pillar that's up. Only EXTENDED pillars count
 *   w<n>       warn + raise pillar n now, at the current difficulty. RUNNING
 *              only, and refused once MAX_SIMULTANEOUS_PILLAR_MOVES pillars are
 *              active (the same limit the sequencer keeps)
 *   ?          print current state
 *
 * console_tick prints the demo view, read-only: a 3-2-1 countdown, a one-line
 * arena whenever a pillar or player changes, and a game-over summary. */
void console_init(uint32_t now_ms);
void console_handle_line(const char *line, uint32_t now_ms);
void console_tick(uint32_t now_ms); /* call every loop */

#endif
