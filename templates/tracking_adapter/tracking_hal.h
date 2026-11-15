#ifndef TRACKING_HAL_H
#define TRACKING_HAL_H

#include <stdint.h>
#include "tracking/CharacterModule.h"

/* Board side of Tracking: the hardware callbacks Tracking's code calls.
 * Owners: Tracking writes the sensor reads (encoders, photo-interrupters,
 * limit switches), Wheels writes the homing movement. GL only provides these
 * stubs so the sketch compiles. */

/* The callbacks for CharacterConfig.hardware. Same set for both players;
 * each callback is told which player or encoder channel it's for. */
CharacterHardware tracking_hal_callbacks();

/* Which mux channel each player's encoder is on, and +1 or -1 so both
 * encoders count the same way round. */
uint8_t tracking_hal_encoder_channel(uint8_t player_id);
int8_t tracking_hal_encoder_sign(uint8_t player_id);

/* Pin modes and interrupts. Called by tracking_init() once both modules are
 * set up. */
void tracking_hal_begin();

/* Reads both encoders and keeps a short history with timestamps. Called at
 * the start of every tracking_update(). */
void tracking_hal_service();

#endif
