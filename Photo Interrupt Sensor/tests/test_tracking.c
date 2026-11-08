#include "CharacterController.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t now;
    uint16_t raw[2];
    uint32_t lag[2];
    CharacterEncoderSample reference[2];
    bool encoder_fail[2], reference_fail[2], reference_input_fail[2];
    bool switch_fail[2], active[2], pressed[2], start_fail[2];
    bool direction_fail[2];
    int pillar[2];
    CharacterDirection direction[2];
    unsigned int starts[2], stops[2], reference_reads[2];
    unsigned int critical_depth, applications;
    uint32_t requested_crossing[2];
    CharacterDirection outgoing[2];
} Fake;

typedef struct {
    Fake hw;
    CharacterModule player[2];
    CharacterController controller;
} Fixture;

static unsigned int player_for_channel(uint8_t channel)
{
    assert(channel == 3U || channel == 6U);
    return channel == 3U ? 0U : 1U;
}

static bool read_encoder(void *context, uint8_t channel, CharacterEncoderSample *sample)
{
    Fake *f = context;
    unsigned int p = player_for_channel(channel);
    assert(f->critical_depth == 0U);
    if (f->encoder_fail[p]) return false;
    sample->angle_x10 = f->raw[p];
    sample->sampled_ms = f->now - f->lag[p];
    return true;
}

static bool read_reference(void *context, uint8_t channel, uint32_t crossing,
                           CharacterEncoderSample *sample)
{
    Fake *f = context;
    unsigned int p = player_for_channel(channel);
    assert(f->critical_depth == 0U);
    ++f->reference_reads[p];
    f->requested_crossing[p] = crossing;
    if (f->reference_fail[p]) return false;
    *sample = f->reference[p];
    return true;
}

static bool read_active(void *context, uint8_t p, bool *active)
{
    Fake *f = context;
    assert(p < 2U && f->critical_depth == 0U);
    *active = f->active[p];
    return !f->reference_input_fail[p];
}

static bool read_pressed(void *context, uint8_t p, bool *pressed)
{
    Fake *f = context;
    assert(p < 2U && f->critical_depth == 0U);
    *pressed = f->pressed[p];
    return !f->switch_fail[p];
}

static bool start_homing(void *context, uint8_t p)
{
    Fake *f = context;
    assert(p < 2U && f->critical_depth == 0U);
    ++f->starts[p];
    return !f->start_fail[p];
}

static void stop_homing(void *context, uint8_t p)
{
    Fake *f = context;
    assert(p < 2U && f->critical_depth == 0U);
    ++f->stops[p];
}

static uint8_t enter_critical(void *context)
{
    Fake *f = context;
    assert(f->critical_depth == 0U);
    ++f->critical_depth;
    return 0x80U;
}

static void exit_critical(void *context, uint8_t previous)
{
    Fake *f = context;
    assert(f->critical_depth == 1U && previous == 0x80U);
    --f->critical_depth;
}

static int identify(void *context, uint8_t p)
{
    Fake *f = context;
    return f->pillar[p];
}

static bool read_direction(void *context, uint8_t p, CharacterDirection *direction)
{
    Fake *f = context;
    *direction = f->direction[p];
    return !f->direction_fail[p];
}

static void apply_response(void *context, const uint8_t ids[2],
                           const CharacterDirection outgoing[2])
{
    Fake *f = context;
    assert(ids[0] == 0U && ids[1] == 1U);
    ++f->applications;
    f->outgoing[0] = outgoing[0];
    f->outgoing[1] = outgoing[1];
    f->direction[0] = outgoing[0];
    f->direction[1] = outgoing[1];
}

static CharacterConfig config_for(Fake *f, uint8_t p)
{
    CharacterConfig config = character_default_config();
    config.player_id = p;
    config.encoder_channel = p == 0U ? 3U : 6U;
    config.hardware.context = f;
    config.hardware.read_encoder = read_encoder;
    config.hardware.read_reference_sample = read_reference;
    config.hardware.read_reference_active = read_active;
    config.hardware.read_pillar_pressed = read_pressed;
    config.hardware.start_homing = start_homing;
    config.hardware.stop_homing = stop_homing;
    config.hardware.critical_enter = enter_critical;
    config.hardware.critical_exit = exit_critical;
    config.hardware.identify_pillar = identify;
    return config;
}

static void setup(Fixture *f)
{
    uint8_t p;
    memset(f, 0, sizeof(*f));
    for (p = 0U; p < 2U; ++p) {
        CharacterConfig config = config_for(&f->hw, p);
        f->hw.pillar[p] = CHARACTER_UNKNOWN_PILLAR;
        assert(character_init(&f->player[p], &config));
    }
}

static void tick(Fixture *f, uint8_t p, uint32_t now, uint16_t raw)
{
    f->hw.now = now;
    f->hw.raw[p] = raw;
    character_update(&f->player[p], now);
}

static void home(Fixture *f, uint8_t p, uint16_t reference, uint32_t now)
{
    f->hw.active[p] = false;
    assert(character_start_homing(&f->player[p], now));
    f->hw.reference[p].angle_x10 = reference;
    f->hw.reference[p].sampled_ms = now + 1U;
    f->hw.active[p] = true;
    character_reference_isr(&f->player[p], now + 1U);
    tick(f, p, now + 1U, reference);
    assert(character_is_calibrated(&f->player[p]));
    assert(character_angle_is_valid(&f->player[p]));
    assert(character_get_angle(&f->player[p]) == 0U);
    f->hw.active[p] = false;
}

static void setup_controller(Fixture *f, bool respond, bool initial)
{
    CharacterControllerConfig config = character_controller_default_config();
    config.context = &f->hw;
    config.detect_initial_overlap = initial;
    config.read_direction = read_direction;
    if (respond) {
        config.choose_response = character_response_separate;
        config.apply_response = apply_response;
    }
    assert(character_controller_init(&f->controller, &f->player[0], &f->player[1], &config));
}

static void pair_tick(Fixture *f, uint32_t now, uint16_t first, uint16_t second, bool enabled)
{
    f->hw.now = now;
    f->hw.raw[0] = first;
    f->hw.raw[1] = second;
    character_controller_update(&f->controller, now, enabled);
}

static void setup_pair(Fixture *f, bool respond, bool initial)
{
    setup(f);
    home(f, 0U, 0U, 0U);
    home(f, 1U, 0U, 0U);
    setup_controller(f, respond, initial);
}

static void test_config(void)
{
    Fixture f;
    CharacterConfig config = character_default_config();
    CharacterControllerConfig pair = character_controller_default_config();
    CharacterModule empty = {0};
    setup(&f);
    assert(!character_init(&empty, &config));
    assert(!character_is_calibrated(&empty));
    assert(!character_angle_is_valid(NULL));
    assert(f.hw.starts[0] == 0U && f.hw.stops[0] == 0U);
    config = config_for(&f.hw, 0U);
    config.encoder_sign = 0;
    assert(!character_init(&empty, &config));
    config.encoder_sign = 1;
    config.homing_timeout_ms = 0U;
    assert(!character_init(&empty, &config));
    assert(!character_controller_init(&f.controller, &f.player[0], &f.player[0], &pair));
    pair.release_x10 = pair.enter_x10;
    assert(!character_controller_init(&f.controller, &f.player[0], &f.player[1], &pair));
}

static void test_offsets_and_channels(void)
{
    Fixture f;
    CharacterConfig reverse;
    setup(&f);
    home(&f, 0U, 50U, 0U);
    home(&f, 1U, 1234U, 0U);
    tick(&f, 0U, 2U, 500U);
    tick(&f, 1U, 2U, 1334U);
    assert(character_get_angle(&f.player[0]) == 450U);
    assert(character_get_angle(&f.player[1]) == 100U);
    tick(&f, 0U, 3U, 40U);
    assert(character_get_angle(&f.player[0]) == 3590U);
    assert(f.hw.stops[0] == 1U && f.hw.stops[1] == 1U);
    reverse = config_for(&f.hw, 1U);
    reverse.encoder_sign = -1;
    assert(character_init(&f.player[1], &reverse));
    home(&f, 1U, 100U, 4U);
    tick(&f, 1U, 6U, 50U);
    assert(character_get_angle(&f.player[1]) == 50U);
    assert(character_angular_distance(3580U, 20U) == 40U);
}

static void test_recalibration_buffer(void)
{
    Fixture f;
    setup(&f);
    home(&f, 0U, 50U, 0U);
    tick(&f, 0U, 2U, 40U); /* 359 degrees is inside the buffer. */
    assert(!f.player[0].reference_armed);
    character_reference_isr(&f.player[0], 3U);
    tick(&f, 0U, 3U, 60U);
    assert(f.player[0].calibration_revision == 1U);
    tick(&f, 0U, 4U, 500U); /* Exactly 45 degrees is still disarmed. */
    assert(!f.player[0].reference_armed);
    f.hw.active[0] = true;
    tick(&f, 0U, 5U, 501U);
    assert(!f.player[0].reference_armed);
    f.hw.active[0] = false;
    tick(&f, 0U, 6U, 501U);
    assert(f.player[0].reference_armed);
    f.hw.reference[0] = (CharacterEncoderSample){70U, 10U};
    character_reference_isr(&f.player[0], 10U);
    character_reference_isr(&f.player[0], 11U); /* Bounce cannot replace first edge. */
    assert(f.hw.reference_reads[0] == 1U); /* ISR did no I/O. */
    f.hw.active[0] = true;
    tick(&f, 0U, 12U, 90U); /* Movement since crossing must remain visible. */
    assert(f.hw.requested_crossing[0] == 10U);
    assert(f.player[0].reference_offset_x10 == 70U);
    assert(character_get_angle(&f.player[0]) == 20U);
    assert(f.player[0].calibration_revision == 2U);
    assert(f.hw.stops[0] == 1U); /* Runtime reference refresh does not stop motion. */
}

static void test_sample_failures(void)
{
    Fixture f;
    setup(&f);
    home(&f, 0U, 50U, 0U);
    tick(&f, 0U, 2U, 600U);
    f.hw.reference[0] = (CharacterEncoderSample){99U, 15U};
    character_reference_isr(&f.player[0], 10U);
    tick(&f, 0U, 15U, 100U);
    assert(f.player[0].rejected_reference_events == 1U);
    assert(f.player[0].reference_offset_x10 == 50U);
    f.hw.reference[0] = (CharacterEncoderSample){99U, 20U};
    character_reference_isr(&f.player[0], 20U);
    tick(&f, 0U, 41U, 100U); /* Expired event. */
    assert(f.player[0].rejected_reference_events == 2U);
    f.hw.reference_fail[0] = true;
    character_reference_isr(&f.player[0], 42U);
    tick(&f, 0U, 42U, 100U);
    assert(f.player[0].rejected_reference_events == 3U);
    f.hw.encoder_fail[0] = true;
    tick(&f, 0U, 43U, 700U);
    assert(character_is_calibrated(&f.player[0]));
    assert(!character_angle_is_valid(&f.player[0]));
    assert(character_get_angle(&f.player[0]) == 50U); /* Retain last valid cache. */
    f.hw.encoder_fail[0] = false;
    f.hw.lag[0] = 21U;
    tick(&f, 0U, 44U, 700U);
    assert(!character_angle_is_valid(&f.player[0]));
    f.hw.lag[0] = 0U;
    tick(&f, 0U, 45U, 3600U);
    assert(!character_angle_is_valid(&f.player[0]));
    tick(&f, 0U, 46U, 700U);
    assert(character_angle_is_valid(&f.player[0]));
    assert(character_get_angle(&f.player[0]) == 650U);
}

static void test_homing_states_and_rollover(void)
{
    Fixture f;
    setup(&f);
    f.hw.active[0] = true;
    assert(character_start_homing(&f.player[0], 0U));
    assert(!character_start_homing(&f.player[0], 1U));
    character_reference_isr(&f.player[0], 1U);
    tick(&f, 0U, 1U, 100U);
    assert(!character_is_calibrated(&f.player[0]));
    f.hw.active[0] = false;
    tick(&f, 0U, 2U, 100U);
    f.hw.reference[0] = (CharacterEncoderSample){100U, 3U};
    character_reference_isr(&f.player[0], 3U);
    tick(&f, 0U, 3U, 100U);
    assert(character_is_calibrated(&f.player[0]));
    assert(character_start_homing(&f.player[0], UINT32_MAX - 9U));
    tick(&f, 0U, 9990U, 100U); /* Exactly 10000 elapsed across rollover. */
    assert(f.player[0].calibration == CHARACTER_HOMING_TIMEOUT);
    assert(f.hw.stops[0] == 2U);
    assert(character_start_homing(&f.player[0], 10000U));
    character_cancel_homing(&f.player[0]);
    assert(f.player[0].calibration == CHARACTER_UNCALIBRATED);
    f.hw.start_fail[0] = true;
    assert(!character_start_homing(&f.player[0], 10001U));
    assert(f.player[0].calibration == CHARACTER_HOMING_FAILED);
    f.hw.start_fail[0] = false;
    assert(character_start_homing(&f.player[0], 10002U));
    f.hw.reference_input_fail[0] = true;
    tick(&f, 0U, 10003U, 0U);
    assert(f.player[0].calibration == CHARACTER_HOMING_FAILED);
    assert(f.hw.stops[0] == 5U);
    setup(&f);
    home(&f, 0U, 50U, UINT32_MAX - 1U);
    tick(&f, 0U, 0U, 100U);
    assert(character_get_angle(&f.player[0]) == 50U);
}

static void test_switch_debounce(void)
{
    Fixture f;
    PillarCollisionStatus hit;
    setup(&f);
    f.hw.pressed[0] = true;
    tick(&f, 0U, 0U, 0U);
    assert(!character_read_pillar_collision(&f.player[0]).is_hit);
    f.hw.pressed[0] = false;
    tick(&f, 0U, 1U, 0U);
    tick(&f, 0U, 21U, 0U);
    f.hw.pressed[0] = true;
    tick(&f, 0U, 22U, 0U);
    hit = character_read_pillar_collision(&f.player[0]);
    assert(hit.is_hit && hit.hit_ms == 22U && hit.id == CHARACTER_UNKNOWN_PILLAR);
    tick(&f, 0U, 1000U, 0U); /* Held switch. */
    assert(!character_read_pillar_collision(&f.player[0]).is_hit);
    f.hw.pressed[0] = false;
    tick(&f, 0U, 1001U, 0U);
    f.hw.pressed[0] = true;
    tick(&f, 0U, 1010U, 0U); /* Release bounce, under 20ms. */
    assert(!character_read_pillar_collision(&f.player[0]).is_hit);
    f.hw.pressed[0] = false;
    tick(&f, 0U, UINT32_MAX - 9U, 0U);
    tick(&f, 0U, 10U, 0U);
    f.hw.pressed[0] = true;
    tick(&f, 0U, 11U, 0U);
    assert(character_read_pillar_collision(&f.player[0]).hit_ms == 11U);
}

static void test_switch_fault_and_initial_policy(void)
{
    Fixture f;
    CharacterConfig config;
    setup(&f);
    config = config_for(&f.hw, 0U);
    config.accept_initial_press = true;
    assert(character_init(&f.player[0], &config));
    f.hw.pressed[0] = true;
    tick(&f, 0U, 0U, 0U);
    assert(character_read_pillar_collision(&f.player[0]).is_hit);
    f.hw.switch_fail[0] = true;
    tick(&f, 0U, 1U, 0U);
    f.hw.switch_fail[0] = false;
    tick(&f, 0U, 2U, 0U);
    assert(!character_read_pillar_collision(&f.player[0]).is_hit);
    f.hw.pressed[0] = false;
    tick(&f, 0U, 3U, 0U);
    tick(&f, 0U, 23U, 0U);
    f.hw.pressed[0] = true;
    tick(&f, 0U, 24U, 0U);
    assert(character_read_pillar_collision(&f.player[0]).is_hit);
}

static void test_pillar_attribution_and_queue(void)
{
    Fixture f;
    CharacterConfig config;
    const uint16_t angles[] = {0U, 900U, 1800U};
    const uint16_t ambiguous[] = {0U, 20U};
    unsigned int i;
    setup(&f);
    config = config_for(&f.hw, 0U);
    config.release_debounce_ms = 0U;
    config.pillar_angles_x10 = angles;
    config.pillar_count = 3U;
    config.pillar_tolerance_x10 = 30U;
    assert(character_init(&f.player[0], &config));
    home(&f, 0U, 0U, 0U);
    for (i = 0U; i < 5U; ++i) {
        f.hw.pressed[0] = false;
        tick(&f, 0U, 2U + i * 2U, 3590U);
        f.hw.pressed[0] = true;
        tick(&f, 0U, 3U + i * 2U, 3590U);
    }
    assert(f.player[0].dropped_pillar_events == 1U);
    for (i = 0U; i < 4U; ++i) {
        PillarCollisionStatus hit = character_read_pillar_collision(&f.player[0]);
        assert(hit.is_hit && hit.id == 0 && hit.hit_ms == 3U + i * 2U);
    }
    assert(!character_read_pillar_collision(&f.player[0]).is_hit);
    config.pillar_angles_x10 = ambiguous;
    config.pillar_count = 2U;
    assert(character_init(&f.player[0], &config));
    f.hw.pressed[0] = false;
    home(&f, 0U, 0U, 20U);
    f.hw.pressed[0] = true;
    tick(&f, 0U, 22U, 10U);
    assert(character_read_pillar_collision(&f.player[0]).id == CHARACTER_UNKNOWN_PILLAR);
    f.hw.pressed[0] = false;
    tick(&f, 0U, 23U, 10U);
    f.hw.pillar[0] = 5;
    f.hw.encoder_fail[0] = true;
    f.hw.pressed[0] = true;
    tick(&f, 0U, 24U, 10U);
    assert(character_read_pillar_collision(&f.player[0]).id == 5);
}

static void test_simultaneous_switches(void)
{
    Fixture f;
    setup(&f);
    setup_controller(&f, false, false); /* Deliberately NOT calibrated. */
    pair_tick(&f, 0U, 0U, 0U, false);
    pair_tick(&f, 20U, 0U, 0U, false);
    f.hw.pressed[0] = f.hw.pressed[1] = true;
    pair_tick(&f, 21U, 0U, 0U, false);
    assert(character_read_pillar_collision(&f.player[0]).hit_ms == 21U);
    assert(character_read_pillar_collision(&f.player[1]).hit_ms == 21U);
    assert(!character_controller_read_collision(&f.controller).is_hit);
}

static void test_pair_wrap_latch_and_response(void)
{
    Fixture f;
    CharacterPairCollision hit;
    setup_pair(&f, true, false);
    f.hw.direction[0] = CHARACTER_POSITIVE;
    f.hw.direction[1] = CHARACTER_NEGATIVE;
    pair_tick(&f, 2U, 3500U, 100U, true);
    pair_tick(&f, 3U, 3580U, 20U, true);
    hit = character_controller_read_collision(&f.controller);
    assert(hit.is_hit && hit.separation_x10 == 40U && hit.hit_ms == 3U);
    assert(hit.incoming[0] == CHARACTER_POSITIVE && hit.incoming[1] == CHARACTER_NEGATIVE);
    assert(f.hw.applications == 1U);
    assert(f.hw.outgoing[0] == CHARACTER_NEGATIVE && f.hw.outgoing[1] == CHARACTER_POSITIVE);
    assert(character_read_character_collision(&f.player[0]).is_hit);
    assert(character_read_character_collision(&f.player[1]).is_hit);
    assert(!character_read_character_collision(&f.player[0]).is_hit);
    pair_tick(&f, 4U, 3580U, 20U, true);
    assert(!character_controller_read_collision(&f.controller).is_hit);
    pair_tick(&f, 5U, 3550U, 50U, true); /* Exactly release threshold: still latched. */
    pair_tick(&f, 6U, 3580U, 20U, true);
    assert(!character_controller_read_collision(&f.controller).is_hit);
    pair_tick(&f, 7U, 3549U, 50U, true);
    f.hw.direction[0] = CHARACTER_POSITIVE;
    f.hw.direction[1] = CHARACTER_NEGATIVE;
    pair_tick(&f, 8U, 3575U, 25U, true); /* Exactly entry threshold counts. */
    assert(character_controller_read_collision(&f.controller).is_hit);
    assert(f.hw.applications == 2U);
}

static void test_pair_response_cases(void)
{
    Fixture f;
    CharacterPairCollision event = {0};
    CharacterDirection outgoing[2];
    setup_pair(&f, true, false);
    f.hw.direction[0] = f.hw.direction[1] = CHARACTER_POSITIVE;
    pair_tick(&f, 2U, 1000U, 1200U, true);
    pair_tick(&f, 3U, 1180U, 1220U, true); /* Same-direction catch-up. */
    assert(f.hw.applications == 1U);
    assert(f.hw.outgoing[0] == CHARACTER_NEGATIVE && f.hw.outgoing[1] == CHARACTER_POSITIVE);
    setup_pair(&f, true, true);
    f.hw.direction[0] = CHARACTER_NEGATIVE;
    f.hw.direction[1] = CHARACTER_POSITIVE;
    pair_tick(&f, 2U, 1000U, 1040U, true); /* Already moving apart. */
    assert(character_controller_read_collision(&f.controller).is_hit);
    assert(f.hw.applications == 0U);
    event.is_hit = event.directions_valid = true;
    event.incoming[0] = CHARACTER_POSITIVE;
    event.incoming[1] = CHARACTER_NEGATIVE;
    assert(!character_response_separate(NULL, &event, outgoing)); /* Coincident, no ordering. */
    event.previous_signed_gap_x10 = 20;
    assert(character_response_separate(NULL, &event, outgoing));
    event.signed_gap_x10 = -20;
    event.incoming[0] = CHARACTER_NEGATIVE;
    event.incoming[1] = CHARACTER_STOPPED;
    assert(character_response_separate(NULL, &event, outgoing));
    assert(outgoing[0] == CHARACTER_POSITIVE && outgoing[1] == CHARACTER_NEGATIVE);
}

static void test_pair_validity_and_startup(void)
{
    Fixture f;
    setup_pair(&f, true, false);
    pair_tick(&f, 2U, 0U, 20U, true);
    assert(!character_controller_read_collision(&f.controller).is_hit);
    pair_tick(&f, 3U, 0U, 200U, true);
    f.hw.encoder_fail[1] = true;
    pair_tick(&f, 4U, 0U, 20U, true);
    assert(!f.controller.pair_valid);
    f.hw.encoder_fail[1] = false;
    pair_tick(&f, 5U, 0U, 20U, true);
    assert(!character_controller_read_collision(&f.controller).is_hit);
    pair_tick(&f, 30U, 0U, 200U, true);
    f.hw.lag[1] = 6U; /* Fresh individually, but too far apart in time. */
    pair_tick(&f, 31U, 0U, 20U, true);
    assert(!f.controller.pair_valid);
    f.hw.lag[1] = 0U;
    pair_tick(&f, 32U, 0U, 200U, false);
    pair_tick(&f, 33U, 0U, 20U, false);
    assert(!character_controller_read_collision(&f.controller).is_hit);
    pair_tick(&f, 34U, 0U, 200U, true);
    f.hw.direction_fail[0] = true;
    pair_tick(&f, 35U, 0U, 20U, true);
    assert(character_controller_read_collision(&f.controller).is_hit);
    assert(f.hw.applications == 0U);
}

static void test_recalibration_does_not_fake_collision(void)
{
    Fixture f;
    setup_pair(&f, false, false);
    pair_tick(&f, 2U, 1000U, 20U, true);
    assert(f.player[0].reference_armed);
    f.hw.reference[0] = (CharacterEncoderSample){1000U, 3U};
    character_reference_isr(&f.player[0], 3U);
    pair_tick(&f, 3U, 1000U, 20U, true); /* Offset jump makes angles close. */
    assert(f.player[0].calibration_revision == 2U);
    assert(!character_controller_read_collision(&f.controller).is_hit);
    pair_tick(&f, 4U, 1000U, 20U, true);
    assert(!character_controller_read_collision(&f.controller).is_hit);
    pair_tick(&f, 5U, 1000U, 200U, true);
    pair_tick(&f, 6U, 1000U, 20U, true);
    assert(character_controller_read_collision(&f.controller).is_hit);
}

static void test_pair_event_overflow(void)
{
    Fixture f;
    setup_pair(&f, false, false);
    pair_tick(&f, 2U, 0U, 200U, true);
    pair_tick(&f, 3U, 0U, 20U, true);
    pair_tick(&f, 4U, 0U, 200U, true);
    pair_tick(&f, 5U, 0U, 20U, true);
    assert(f.controller.dropped_events == 1U);
    assert(f.player[0].dropped_character_events == 1U);
    assert(character_controller_read_collision(&f.controller).hit_ms == 3U);
    assert(character_read_character_collision(&f.player[0]).hit_ms == 3U);
    assert(f.hw.applications == 0U); /* Default is detection only. */
}

#define RUN(test) do { test(); puts("PASS " #test); } while (0)

int main(void)
{
    RUN(test_config);
    RUN(test_offsets_and_channels);
    RUN(test_recalibration_buffer);
    RUN(test_sample_failures);
    RUN(test_homing_states_and_rollover);
    RUN(test_switch_debounce);
    RUN(test_switch_fault_and_initial_policy);
    RUN(test_pillar_attribution_and_queue);
    RUN(test_simultaneous_switches);
    RUN(test_pair_wrap_latch_and_response);
    RUN(test_pair_response_cases);
    RUN(test_pair_validity_and_startup);
    RUN(test_recalibration_does_not_fake_collision);
    RUN(test_pair_event_overflow);
    puts("All 14 tracking test groups passed.");
    return 0;
}
