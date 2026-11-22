#include "tracking_hal.h"
#include "src/board_link_protocol.h"
#include <math.h>

static_assert(NUM_TRACKED == LINK_NUM_PLAYERS, "tracking_hal.h and board_link_protocol.h disagree on the player count");

/* ---- Pins and settings -------------------------------------------------
 * Picked so nothing clashes with Alice's wiring (WheelsPin.h and the sketch:
 * 2, 3, 6, 7, 8, 10, 52, 53, I2C on 20/21), the Game Mega link on Serial2
 * (16/17), or player 2's wheel encoder (18/19, still to come).
 *
 *  - Photo-interrupters on A8 and A9 (port K), using pin-change interrupts:
 *    the Mega's regular interrupt pins all go to the wheel encoders.
 *  - Hit switches on 22 and 23, INPUT_PULLUP, so pressed reads LOW.
 *
 * TODO (Tracking): confirm the photo-interrupter level. The ITR9608 with a
 * pull-up should read HIGH when the beam is blocked; if it reads LOW, flip
 * REFERENCE_BLOCKED_LEVEL. tests/tracking_inputs_check shows what it reads. */
static const uint8_t REFERENCE_PIN[NUM_TRACKED] = {A8, A9};
static const uint8_t REFERENCE_PORTK_BIT[NUM_TRACKED] = {0, 1}; // A8 = PK0, A9 = PK1
static const uint8_t REFERENCE_BLOCKED_LEVEL = HIGH;
static const uint8_t SWITCH_PIN[NUM_TRACKED] = {22, 23};
static const uint8_t SWITCH_PRESSED_LEVEL = LOW;

/* Player 2's wheel, encoder and sensors aren't built yet (Alice's sketch has
 * Motor A only). TODO: set to true once they're wired. */
static const bool PLAYER_WIRED[NUM_TRACKED] = {true, false};

/* Mux channel of each player's AS5600 (player 0 = STEPPER_MUX_CHANNEL in the
 * sketch), and +1/-1 so both angles increase the same way round. */
static const uint8_t ENCODER_CHANNEL[NUM_TRACKED] = {0, 1};
static const int8_t ENCODER_SIGN[NUM_TRACKED] = {1, 1};

/* The sketch reads the AS5600 every 20 ms, so a reading can be up to about
 * 20 ms old when Tracking looks at it. Tracking's default (20 ms) would
 * sometimes reject it, so allow a little more. */
#define MAX_SAMPLE_AGE_MS 40UL

/* Homing is the player turning their wheel past the sensor, which can take a
 * while; Tracking's default gives up after 10 s. */
#define HOMING_TIMEOUT_MS 60000UL

/* ---- State ------------------------------------------------------------- */

static CharacterModule s_players[NUM_TRACKED];
static TrackingAngleReader s_read_angle_now = nullptr;

// Last AS5600 reading per player, from tracking_note_angle().
static uint16_t s_cached_x10[NUM_TRACKED];
static uint32_t s_cached_ms[NUM_TRACKED];
static bool s_cached[NUM_TRACKED];

static volatile uint8_t s_last_portk = 0;

static uint16_t degrees_to_x10(float degrees)
{
    long x10 = lroundf(degrees * 10.0f) % 3600L; // 359.97 rounds to 3600, i.e. 0
    if (x10 < 0)
        x10 += 3600L;
    return (uint16_t)x10;
}

static int8_t player_for_channel(uint8_t channel)
{
    for (uint8_t p = 0; p < NUM_TRACKED; p++)
    {
        if (PLAYER_WIRED[p] && ENCODER_CHANNEL[p] == channel)
            return (int8_t)p;
    }
    return -1;
}

/* ---- Hardware callbacks Tracking's code calls -------------------------- */

static bool read_encoder(void *, uint8_t channel, CharacterEncoderSample *sample)
{
    int8_t p = player_for_channel(channel);
    if (p < 0 || !s_cached[p])
        return false;
    sample->angle_x10 = s_cached_x10[p];
    sample->sampled_ms = s_cached_ms[p];
    return true;
}

// Called once, right after a photo-interrupter fires. Reads the AS5600 fresh
// and stamps it with when it was really read; Tracking only accepts it if
// that's within 2 ms of the crossing.
static bool read_reference_sample(void *, uint8_t channel, uint32_t crossing_ms, CharacterEncoderSample *sample)
{
    (void)crossing_ms;
    int8_t p = player_for_channel(channel);
    float degrees = 0.0f;
    if (p < 0 || s_read_angle_now == nullptr || !s_read_angle_now((uint8_t)p, &degrees))
        return false;

    uint32_t read_ms = millis();
    tracking_note_angle((uint8_t)p, degrees, read_ms);
    sample->angle_x10 = s_cached_x10[p];
    sample->sampled_ms = read_ms;
    return true;
}

static bool read_reference_active(void *, uint8_t player_id, bool *active)
{
    if (player_id >= NUM_TRACKED || !PLAYER_WIRED[player_id])
        return false;
    *active = digitalRead(REFERENCE_PIN[player_id]) == REFERENCE_BLOCKED_LEVEL;
    return true;
}

static bool read_pillar_pressed(void *, uint8_t player_id, bool *pressed)
{
    if (player_id >= NUM_TRACKED || !PLAYER_WIRED[player_id])
        return false;
    *pressed = digitalRead(SWITCH_PIN[player_id]) == SWITCH_PRESSED_LEVEL;
    return true;
}

// Homing is done by hand: the player turns their wheel until the character
// passes the photo-interrupter, so there's no motor to start or stop.
static bool start_homing(void *, uint8_t player_id)
{
    return player_id < NUM_TRACKED && PLAYER_WIRED[player_id];
}

static void stop_homing(void *, uint8_t player_id)
{
    (void)player_id;
}

// Protects the bytes the photo-interrupter interrupt shares with the main loop.
static uint8_t critical_enter(void *)
{
    uint8_t sreg = SREG;
    cli();
    return sreg;
}

static void critical_exit(void *, uint8_t previous_state)
{
    SREG = previous_state;
}

/* ---- Photo-interrupter interrupt ---------------------------------------
 * Runs whenever an enabled pin on port K (A8..A15) changes. Only records
 * the time; Tracking does the rest in the main loop. */
ISR(PCINT2_vect)
{
    uint8_t bits = PINK;
    uint8_t changed = bits ^ s_last_portk;
    s_last_portk = bits;
    uint32_t now = millis();

    for (uint8_t p = 0; p < NUM_TRACKED; p++)
    {
        uint8_t mask = (uint8_t)(1u << REFERENCE_PORTK_BIT[p]);
        if (!PLAYER_WIRED[p] || !(changed & mask))
            continue;
        uint8_t level = (bits & mask) ? HIGH : LOW;
        if (level == REFERENCE_BLOCKED_LEVEL)
            character_reference_isr(&s_players[p], now);
    }
}

/* ---- Public ------------------------------------------------------------ */

bool tracking_begin(TrackingAngleReader read_angle_now, uint32_t now_ms)
{
    s_read_angle_now = read_angle_now;

    CharacterHardware hw = {};
    hw.read_encoder = read_encoder;
    hw.read_reference_sample = read_reference_sample;
    hw.read_reference_active = read_reference_active;
    hw.read_pillar_pressed = read_pillar_pressed;
    hw.start_homing = start_homing;
    hw.stop_homing = stop_homing;
    hw.critical_enter = critical_enter;
    hw.critical_exit = critical_exit;
    hw.identify_pillar = nullptr; // the Game Mega picks the pillar that's up

    bool ok = true;
    uint8_t pcint_mask = 0;
    for (uint8_t p = 0; p < NUM_TRACKED; p++)
    {
        if (!PLAYER_WIRED[p])
            continue;

        CharacterConfig cfg = character_default_config();
        cfg.player_id = p;
        cfg.encoder_channel = ENCODER_CHANNEL[p];
        cfg.encoder_sign = ENCODER_SIGN[p];
        cfg.max_sample_age_ms = MAX_SAMPLE_AGE_MS;
        cfg.homing_timeout_ms = HOMING_TIMEOUT_MS;
        cfg.hardware = hw;
        if (!character_init(&s_players[p], &cfg))
        {
            ok = false;
            continue;
        }

        pinMode(REFERENCE_PIN[p], INPUT);
        pinMode(SWITCH_PIN[p], INPUT_PULLUP);
        pcint_mask |= (uint8_t)(1u << REFERENCE_PORTK_BIT[p]);
    }

    // Only after every module is set up: switch on the pin-change interrupt.
    s_last_portk = PINK;
    PCMSK2 |= pcint_mask;
    PCICR |= (uint8_t)(1u << PCIE2);

    for (uint8_t p = 0; p < NUM_TRACKED; p++)
    {
        if (PLAYER_WIRED[p] && s_players[p].initialized)
            character_start_homing(&s_players[p], now_ms);
    }
    return ok;
}

void tracking_note_angle(uint8_t player_id, float degrees, uint32_t read_ms)
{
    if (player_id >= NUM_TRACKED || !isfinite(degrees))
        return;
    s_cached_x10[player_id] = degrees_to_x10(degrees);
    s_cached_ms[player_id] = read_ms;
    s_cached[player_id] = true;
}

void tracking_update(uint32_t now_ms)
{
    for (uint8_t p = 0; p < NUM_TRACKED; p++)
    {
        if (PLAYER_WIRED[p])
            character_update(&s_players[p], now_ms);
    }
}

bool tracking_home(uint8_t player_id, uint32_t now_ms)
{
    if (player_id >= NUM_TRACKED || !PLAYER_WIRED[player_id] || !s_players[player_id].initialized)
        return false;
    if (s_players[player_id].calibration == CHARACTER_HOMING)
        return true; // already homing
    return character_start_homing(&s_players[player_id], now_ms);
}

bool tracking_take_hit(uint8_t *player_id, int16_t *pillar)
{
    for (uint8_t p = 0; p < NUM_TRACKED; p++)
    {
        if (!PLAYER_WIRED[p])
            continue;
        PillarCollisionStatus hit = character_read_pillar_collision(&s_players[p]);
        if (hit.is_hit)
        {
            *player_id = p;
            *pillar = (int16_t)hit.id;
            return true;
        }
    }
    return false;
}

uint16_t tracking_angle_x10(uint8_t player_id)
{
    if (player_id >= NUM_TRACKED)
        return 0;
    return character_get_angle(&s_players[player_id]);
}

uint8_t tracking_link_state(uint8_t player_id)
{
    if (player_id >= NUM_TRACKED || !PLAYER_WIRED[player_id] || !s_players[player_id].initialized)
        return LINK_TRACK_NOT_HOMED;

    switch (s_players[player_id].calibration)
    {
    case CHARACTER_HOMING:
        return LINK_TRACK_HOMING;
    case CHARACTER_READY:
        return character_angle_is_valid(&s_players[player_id]) ? LINK_TRACK_READY : LINK_TRACK_NO_ANGLE;
    case CHARACTER_HOMING_TIMEOUT:
    case CHARACTER_HOMING_FAILED:
        return LINK_TRACK_FAILED;
    case CHARACTER_UNCALIBRATED:
    default:
        return LINK_TRACK_NOT_HOMED;
    }
}
