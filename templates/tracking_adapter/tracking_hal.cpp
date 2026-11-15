#include "tracking_hal.h"
#include "tracking_adapter.h"
#include "shared_state.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

/* TEMPLATE: every read below reports "failed" until it's written, so the
 * players simply never become homed and no hits are reported. That's safe to
 * run. Owners are marked on each TODO. */

/* ---- Pins -----------------------------------------------------------------
 * TODO (Tracking, with the team): set real pins, then set
 * TRACKING_PINS_CONFIGURED to 1. Nothing is set up or read until then.
 *  - Photo-interrupters need interrupt pins: on the Mega only 2, 3, 18 or 19.
 *    20 and 21 are the I2C pins the encoders need.
 *  - Every subteam shares one Mega, so check pins aren't already taken
 *    (Pillars' LED strip uses pin 6 in LEDGameLogic.cpp). */
#define TRACKING_PINS_CONFIGURED 0
#ifdef ARDUINO
#define PIN_UNSET 0xFF
static const uint8_t REFERENCE_PIN[NUM_PLAYERS] = {PIN_UNSET, PIN_UNSET}; // photo-interrupter
static const uint8_t SWITCH_PIN[NUM_PLAYERS] = {PIN_UNSET, PIN_UNSET};    // limit switch
#endif

/* ---- Callbacks --------------------------------------------------------- */

static bool read_encoder(void *, uint8_t channel, CharacterEncoderSample *sample)
{
    // TODO (Tracking): Select this mux channel, read that player's AS5600 and
    //   convert to tenths of a degree, 0..3599. The AS5600 gives 0..4095, so
    //   angle_x10 = raw * 3600 / 4096 (use 32-bit maths), plus any gear
    //   ratio. Set sample->sampled_ms to when it was read. Return false if
    //   the read fails. Must not block for long.
    (void)channel;
    (void)sample;
    return false;
}

static bool read_reference_sample(void *, uint8_t channel, uint32_t crossing_ms,
                                  CharacterEncoderSample *sample)
{
    // TODO (Tracking): Return the reading from tracking_hal_service()'s
    //   history that was taken closest to crossing_ms (within 2 ms). Never
    //   copy a later reading and label it with crossing_ms; return false
    //   instead.
    (void)channel;
    (void)crossing_ms;
    (void)sample;
    return false;
}

static bool read_reference_active(void *, uint8_t player_id, bool *active)
{
    // TODO (Tracking): *active = true when that player's photo-interrupter
    //   beam is blocked, e.g. digitalRead(REFERENCE_PIN[player_id]) == HIGH
    //   (check the real level). Return true when the read worked.
    (void)player_id;
    (void)active;
    return false;
}

static bool read_pillar_pressed(void *, uint8_t player_id, bool *pressed)
{
    // TODO (Tracking): *pressed = true when that player's limit switch is
    //   pressed, e.g. digitalRead(SWITCH_PIN[player_id]) == LOW with
    //   INPUT_PULLUP (check the real wiring). Return true when the read worked.
    (void)player_id;
    (void)pressed;
    return false;
}

static bool start_homing(void *, uint8_t player_id)
{
    // TODO (Wheels): Start moving this player's figure toward its
    //   photo-interrupter. Return true if the move started.
    //   First tests, before Wheels code exists: return true without moving
    //   and push the figure past the sensor by hand.
    (void)player_id;
    return false;
}

static void stop_homing(void *, uint8_t player_id)
{
    // TODO (Wheels): Stop the homing move for this player.
    (void)player_id;
}

// Protects the few bytes the photo-interrupter interrupt shares with the main
// loop (a 32-bit time can't be read in one step on the Mega). Done: these
// are standard AVR and needn't change.
static uint8_t critical_enter(void *)
{
#ifdef ARDUINO
    uint8_t sreg = SREG;
    cli();
    return sreg;
#else
    return 0;
#endif
}

static void critical_exit(void *, uint8_t previous_state)
{
#ifdef ARDUINO
    SREG = previous_state;
#else
    (void)previous_state;
#endif
}

CharacterHardware tracking_hal_callbacks()
{
    CharacterHardware hw = {};
    hw.context = nullptr;
    hw.read_encoder = read_encoder;
    hw.read_reference_sample = read_reference_sample;
    hw.read_reference_active = read_reference_active;
    hw.read_pillar_pressed = read_pillar_pressed;
    hw.start_homing = start_homing;
    hw.stop_homing = stop_homing;
    hw.critical_enter = critical_enter;
    hw.critical_exit = critical_exit;
    hw.identify_pillar = nullptr; // optional: only if wiring can tell which pillar pressed the switch
    return hw;
}

uint8_t tracking_hal_encoder_channel(uint8_t player_id)
{
    // TODO (Tracking / Wheels): the real mux channel for each player's encoder.
    return player_id;
}

int8_t tracking_hal_encoder_sign(uint8_t player_id)
{
    // TODO (Tracking): -1 for an encoder that counts the opposite way, so
    //   both players' angles increase in the same direction.
    (void)player_id;
    return 1;
}

/* ---- Setup and per-loop work ------------------------------------------- */

#if defined(ARDUINO) && TRACKING_PINS_CONFIGURED
// One small interrupt handler per player. Only records the time; Tracking
// does the rest in the main loop.
static void reference_isr_p0() { tracking_reference_edge(0, millis()); }
static void reference_isr_p1() { tracking_reference_edge(1, millis()); }
#endif

void tracking_hal_begin()
{
#if defined(ARDUINO) && TRACKING_PINS_CONFIGURED
    // TODO (Tracking): match the pin modes and the interrupt edge to the
    //   wiring. The edge should be the one where the beam becomes blocked.
    for (uint8_t i = 0; i < NUM_PLAYERS; i++)
    {
        pinMode(SWITCH_PIN[i], INPUT_PULLUP);
        pinMode(REFERENCE_PIN[i], INPUT);
    }
    attachInterrupt(digitalPinToInterrupt(REFERENCE_PIN[0]), reference_isr_p0, RISING);
    attachInterrupt(digitalPinToInterrupt(REFERENCE_PIN[1]), reference_isr_p1, RISING);
#endif
}

void tracking_hal_service()
{
    // TODO (Tracking): Read both encoders here, every loop, and keep the last
    //   few readings with their millis() times so read_reference_sample can
    //   find the one taken when the beam was crossed. Each encoder needs a
    //   fresh reading at least every couple of milliseconds.
}
