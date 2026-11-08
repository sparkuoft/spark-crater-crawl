#include "CharacterModule.h"

#include <stddef.h>
#include <string.h>

static uint16_t wrap_angle(int32_t angle)
{
    angle %= (int32_t)CHARACTER_TURN_X10;
    if (angle < 0) angle += (int32_t)CHARACTER_TURN_X10;
    return (uint16_t)angle;
}

uint16_t character_angular_distance(uint16_t first_x10, uint16_t second_x10)
{
    uint16_t difference = first_x10 > second_x10
        ? (uint16_t)(first_x10 - second_x10)
        : (uint16_t)(second_x10 - first_x10);
    return difference > CHARACTER_TURN_X10 / 2U
        ? (uint16_t)(CHARACTER_TURN_X10 - difference) : difference;
}

static uint16_t relative_angle(const CharacterModule *c, uint16_t raw)
{
    return wrap_angle(((int32_t)raw - (int32_t)c->reference_offset_x10)
                      * c->config.encoder_sign);
}

CharacterConfig character_default_config(void)
{
    CharacterConfig config = {0};
    config.encoder_sign = 1;
    config.reference_rearm_x10 = 450U;
    config.release_debounce_ms = 20U;
    config.homing_timeout_ms = 10000U;
    config.max_sample_age_ms = 20U;
    config.reference_sample_tolerance_ms = 2U;
    config.max_reference_event_age_ms = 20U;
    return config;
}

bool character_init(CharacterModule *c, const CharacterConfig *config)
{
    uint8_t i;
    if (c == NULL || config == NULL) return false;
    if ((config->encoder_sign != 1 && config->encoder_sign != -1)
        || config->reference_rearm_x10 >= CHARACTER_TURN_X10 / 2U
        || config->pillar_tolerance_x10 >= CHARACTER_TURN_X10 / 2U
        || config->homing_timeout_ms == 0U
        || config->homing_timeout_ms > INT32_MAX
        || config->release_debounce_ms > INT32_MAX
        || config->max_sample_age_ms > INT32_MAX
        || config->reference_sample_tolerance_ms > INT32_MAX
        || config->max_reference_event_age_ms > INT32_MAX
        || (config->pillar_count > 0U && config->pillar_angles_x10 == NULL)
        || config->hardware.read_encoder == NULL
        || config->hardware.read_reference_sample == NULL
        || config->hardware.read_reference_active == NULL
        || config->hardware.read_pillar_pressed == NULL
        || config->hardware.start_homing == NULL
        || config->hardware.stop_homing == NULL
        || config->hardware.critical_enter == NULL
        || config->hardware.critical_exit == NULL) return false;
    for (i = 0U; i < config->pillar_count; ++i) {
        if (config->pillar_angles_x10[i] >= CHARACTER_TURN_X10) return false;
    }
    memset(c, 0, sizeof(*c));
    c->config = *config;
    c->initialized = true;
    c->calibration = CHARACTER_UNCALIBRATED;
    return true;
}

static bool take_reference(CharacterModule *c, uint32_t *crossing_ms)
{
    CharacterHardware *hw = &c->config.hardware;
    uint8_t previous = hw->critical_enter(hw->context);
    bool pending = c->reference_pending;
    *crossing_ms = c->reference_crossing_ms;
    c->reference_pending = false;
    hw->critical_exit(hw->context, previous);
    return pending;
}

void character_reference_isr(CharacterModule *c, uint32_t crossing_ms)
{
    /* First edge wins while pending, so bounce cannot replace its timestamp. */
    if (c != NULL && c->initialized && !c->reference_pending) {
        c->reference_crossing_ms = crossing_ms;
        c->reference_pending = true;
    }
}

bool character_start_homing(CharacterModule *c, uint32_t now_ms)
{
    bool active;
    uint32_t discarded;
    CharacterHardware *hw;
    if (c == NULL || !c->initialized || c->calibration == CHARACTER_HOMING)
        return false;
    hw = &c->config.hardware;
    c->angle_valid = false;
    c->reference_armed = false;
    c->calibration = CHARACTER_UNCALIBRATED;
    (void)take_reference(c, &discarded);
    if (!hw->read_reference_active(hw->context, c->config.player_id, &active)) {
        c->reference_input_error = true;
        c->calibration = CHARACTER_HOMING_FAILED;
        return false;
    }
    c->reference_input_error = false;
    c->reference_armed = !active;
    c->homing_started_ms = now_ms;
    c->calibration = CHARACTER_HOMING;
    if (!hw->start_homing(hw->context, c->config.player_id)) {
        hw->stop_homing(hw->context, c->config.player_id);
        c->calibration = CHARACTER_HOMING_FAILED;
        c->reference_armed = false;
        return false;
    }
    return true;
}

void character_cancel_homing(CharacterModule *c)
{
    if (c != NULL && c->initialized && c->calibration == CHARACTER_HOMING) {
        c->config.hardware.stop_homing(c->config.hardware.context, c->config.player_id);
        c->calibration = CHARACTER_UNCALIBRATED;
        c->reference_armed = false;
        c->angle_valid = false;
    }
}

static bool near_time(uint32_t first, uint32_t second, uint32_t tolerance)
{
    return first - second <= tolerance || second - first <= tolerance;
}

static void update_reference(CharacterModule *c, uint32_t now_ms,
                             bool reference_ok, bool active)
{
    CharacterHardware *hw = &c->config.hardware;
    CharacterEncoderSample sample;
    uint32_t crossing_ms;
    bool pending = take_reference(c, &crossing_ms);
    if (c->calibration == CHARACTER_HOMING
        && now_ms - c->homing_started_ms >= c->config.homing_timeout_ms) {
        hw->stop_homing(hw->context, c->config.player_id);
        c->calibration = CHARACTER_HOMING_TIMEOUT;
        c->reference_armed = false;
        return;
    }
    if (c->calibration == CHARACTER_HOMING && !reference_ok) {
        hw->stop_homing(hw->context, c->config.player_id);
        c->calibration = CHARACTER_HOMING_FAILED;
        c->reference_armed = false;
        return;
    }
    /* Process before rearming, so an edge captured while disarmed is ignored. */
    if (pending && c->reference_armed && reference_ok
        && (c->calibration == CHARACTER_HOMING || c->calibration == CHARACTER_READY)) {
        if (now_ms - crossing_ms <= c->config.max_reference_event_age_ms
            && hw->read_reference_sample(hw->context, c->config.encoder_channel,
                                         crossing_ms, &sample)
            && sample.angle_x10 < CHARACTER_TURN_X10
            && now_ms - sample.sampled_ms <= c->config.max_reference_event_age_ms
            && near_time(sample.sampled_ms, crossing_ms,
                         c->config.reference_sample_tolerance_ms)) {
            bool homing = c->calibration == CHARACTER_HOMING;
            c->reference_offset_x10 = sample.angle_x10;
            c->calibration = CHARACTER_READY;
            ++c->calibration_revision;
            c->reference_armed = false;
            if (homing) hw->stop_homing(hw->context, c->config.player_id);
            return;
        }
        ++c->rejected_reference_events;
    }
    if (c->calibration == CHARACTER_HOMING && reference_ok && !active)
        c->reference_armed = true;
}

static int identify_pillar(CharacterModule *c)
{
    CharacterConfig *config = &c->config;
    int match = CHARACTER_UNKNOWN_PILLAR;
    uint8_t i;
    if (config->hardware.identify_pillar != NULL) {
        int direct = config->hardware.identify_pillar(config->hardware.context,
                                                       config->player_id);
        if (direct >= 0) return direct;
    }
    if (!c->angle_valid) return match;
    for (i = 0U; i < config->pillar_count; ++i) {
        if (character_angular_distance(c->angle_x10, config->pillar_angles_x10[i])
            <= config->pillar_tolerance_x10) {
            if (match != CHARACTER_UNKNOWN_PILLAR) return CHARACTER_UNKNOWN_PILLAR;
            match = (int)i;
        }
    }
    return match;
}

static void record_pillar(CharacterModule *c, uint32_t now_ms)
{
    uint8_t index;
    if (c->pillar_count == CHARACTER_EVENT_CAPACITY) {
        ++c->dropped_pillar_events;
        return; /* Preserve oldest events and expose overflow. */
    }
    index = (uint8_t)((c->pillar_head + c->pillar_count) % CHARACTER_EVENT_CAPACITY);
    c->pillar_events[index].id = identify_pillar(c);
    c->pillar_events[index].is_hit = true;
    c->pillar_events[index].hit_ms = now_ms;
    ++c->pillar_count;
}

static void update_switch(CharacterModule *c, uint32_t now_ms)
{
    bool pressed;
    CharacterHardware *hw = &c->config.hardware;
    c->pillar_input_error = !hw->read_pillar_pressed(hw->context,
                                                   c->config.player_id, &pressed);
    if (c->pillar_input_error) {
        c->switch_armed = false;
        c->release_timing = false;
        c->switch_seen = true; /* Require stable release after a read failure. */
        return;
    }
    if (!c->switch_seen) {
        c->switch_seen = true;
        c->switch_armed = c->config.accept_initial_press;
    }
    if (pressed) {
        c->release_timing = false;
        if (c->switch_armed) record_pillar(c, now_ms);
        c->switch_armed = false;
    } else if (!c->switch_armed) {
        if (!c->release_timing) {
            c->release_timing = true;
            c->release_started_ms = now_ms;
        }
        if (now_ms - c->release_started_ms >= c->config.release_debounce_ms)
            c->switch_armed = true;
    }
}

void character_update(CharacterModule *c, uint32_t now_ms)
{
    CharacterEncoderSample sample;
    CharacterHardware *hw;
    bool active = false;
    bool reference_ok;
    if (c == NULL || !c->initialized) return;
    hw = &c->config.hardware;
    reference_ok = hw->read_reference_active(hw->context, c->config.player_id, &active);
    c->reference_input_error = !reference_ok;
    update_reference(c, now_ms, reference_ok, active);
    c->encoder_error = !hw->read_encoder(hw->context, c->config.encoder_channel, &sample);
    if (!c->encoder_error) {
        c->encoder_error = sample.angle_x10 >= CHARACTER_TURN_X10
            || now_ms - sample.sampled_ms > c->config.max_sample_age_ms;
    }
    c->angle_valid = !c->encoder_error && c->calibration == CHARACTER_READY;
    if (c->angle_valid) {
        c->angle_x10 = relative_angle(c, sample.angle_x10);
        c->sampled_ms = sample.sampled_ms;
        if (!c->reference_armed && reference_ok && !active
            && character_angular_distance(c->angle_x10, 0U) > c->config.reference_rearm_x10)
            c->reference_armed = true;
    }
    update_switch(c, now_ms); /* Contact never depends on calibration. */
}

uint16_t character_get_angle(const CharacterModule *c)
{
    return c != NULL && c->initialized ? c->angle_x10 : 0U;
}

bool character_is_calibrated(const CharacterModule *c)
{
    return c != NULL && c->initialized && c->calibration == CHARACTER_READY;
}

bool character_angle_is_valid(const CharacterModule *c)
{
    return character_is_calibrated(c) && c->angle_valid;
}

PillarCollisionStatus character_read_pillar_collision(CharacterModule *c)
{
    PillarCollisionStatus event = {CHARACTER_UNKNOWN_PILLAR, false, 0U};
    if (c != NULL && c->initialized && c->pillar_count > 0U) {
        event = c->pillar_events[c->pillar_head];
        c->pillar_head = (uint8_t)((c->pillar_head + 1U) % CHARACTER_EVENT_CAPACITY);
        --c->pillar_count;
    }
    return event;
}

CharacterCollisionStatus character_read_character_collision(CharacterModule *c)
{
    CharacterCollisionStatus event = {false, 0U};
    if (c != NULL && c->initialized) {
        event = c->character_event;
        c->character_event.is_hit = false;
    }
    return event;
}

void character_record_character_collision(CharacterModule *c, uint32_t now_ms)
{
    if (c == NULL || !c->initialized) return;
    if (c->character_event.is_hit) {
        ++c->dropped_character_events;
    } else {
        c->character_event.is_hit = true;
        c->character_event.hit_ms = now_ms;
    }
}
