#ifndef TRACKING_HAL_H
#define TRACKING_HAL_H

#include <Arduino.h>
#include "src/tracking/CharacterModule.h"

/* Players tracked on this board (NUM_PLAYERS on the Game Mega). */
#define NUM_TRACKED 2

/* Tracking on the Wheels Mega. One CharacterModule per player (Tracking's
 * code, in src/tracking/), plus the hardware it reads on this board:
 *   - angle: the AS5600 on each character, through the TCA9548A mux
 *   - photo-interrupter: one per player, marks the zero angle (homing)
 *   - hit switch: one per player, pressed by a fully raised pillar
 * Pins and settings are at the top of tracking_hal.cpp.
 *
 * Homing needs no motor code: the player turns their wheel until the
 * character passes its photo-interrupter. */

/* Reads one player's character angle in degrees (0..360) right now. Returns
 * false if that player has no encoder wired. Supplied by the sketch. */
typedef bool (*TrackingAngleReader)(uint8_t player_id, float *degrees);

/* Sets up both players and their pins, then starts homing. Call once in
 * setup(), after Wire and the mux are set up. */
bool tracking_begin(TrackingAngleReader read_angle_now, uint32_t now_ms);

/* Hands Tracking an angle the sketch already read, so the AS5600 isn't read
 * twice. */
void tracking_note_angle(uint8_t player_id, float degrees, uint32_t read_ms);

/* One tracking pass. Call every loop. */
void tracking_update(uint32_t now_ms);

/* Starts homing one player again (the Game Mega's M message). */
bool tracking_home(uint8_t player_id, uint32_t now_ms);

/* Takes the next hit-switch press to report. Returns false when there's
 * none. pillar is Tracking's pillar index, or -1 if it doesn't know. */
bool tracking_take_hit(uint8_t *player_id, int16_t *pillar);

/* For the Game Mega's A message: angle in tenths of a degree, and the
 * tracking state as a LinkTrackState. */
uint16_t tracking_angle_x10(uint8_t player_id);
uint8_t tracking_link_state(uint8_t player_id);

#endif
