#include "CharacterController.h"

#include <stddef.h>
#include <string.h>

static int16_t signed_gap(uint16_t first, uint16_t second)
{
    int32_t gap = (int32_t)second - (int32_t)first;
    if (gap > 1800) gap -= 3600;
    if (gap < -1800) gap += 3600;
    return (int16_t)gap;
}

static bool valid_direction(CharacterDirection direction)
{
    return direction == CHARACTER_NEGATIVE || direction == CHARACTER_STOPPED
        || direction == CHARACTER_POSITIVE;
}

CharacterControllerConfig character_controller_default_config(void)
{
    CharacterControllerConfig config = {0};
    config.enter_x10 = 50U;
    config.release_x10 = 100U;
    config.max_pair_sample_skew_ms = 5U;
    return config;
}

bool character_controller_init(CharacterController *c,
                               CharacterModule *first, CharacterModule *second,
                               const CharacterControllerConfig *config)
{
    if (c == NULL || first == NULL || second == NULL || config == NULL
        || first == second || !first->initialized || !second->initialized
        || first->config.player_id == second->config.player_id
        || config->enter_x10 >= config->release_x10
        || config->release_x10 >= CHARACTER_TURN_X10 / 2U
        || config->max_pair_sample_skew_ms > INT32_MAX
        || (config->choose_response != NULL
            && (config->apply_response == NULL || config->read_direction == NULL)))
        return false;
    memset(c, 0, sizeof(*c));
    c->players[0] = first;
    c->players[1] = second;
    c->config = *config;
    c->initialized = true;
    return true;
}

static void report_collision(CharacterController *c, uint32_t now_ms,
                              uint16_t separation, int16_t gap)
{
    CharacterPairCollision event = {0};
    CharacterDirection outgoing[2] = {CHARACTER_STOPPED, CHARACTER_STOPPED};
    unsigned int i;
    event.is_hit = true;
    event.hit_ms = now_ms;
    event.separation_x10 = separation;
    event.signed_gap_x10 = gap;
    event.previous_signed_gap_x10 = c->baseline_seen ? c->previous_signed_gap_x10 : 0;
    event.approach_known = c->baseline_seen;
    event.approaching = c->baseline_seen && separation < c->previous_separation_x10;
    event.directions_valid = c->config.read_direction != NULL;
    for (i = 0U; i < 2U; ++i) {
        event.player_id[i] = c->players[i]->config.player_id;
        event.angle_x10[i] = character_get_angle(c->players[i]);
        if (c->config.read_direction != NULL) {
            bool ok = c->config.read_direction(c->config.context, event.player_id[i],
                                               &event.incoming[i]);
            if (!ok || !valid_direction(event.incoming[i])) event.directions_valid = false;
        }
        character_record_character_collision(c->players[i], now_ms);
    }
    if (c->pending_event.is_hit) ++c->dropped_events;
    else c->pending_event = event;
    if (event.directions_valid && c->config.choose_response != NULL
        && c->config.choose_response(c->config.context, &event, outgoing)
        && valid_direction(outgoing[0]) && valid_direction(outgoing[1]))
        c->config.apply_response(c->config.context, event.player_id, outgoing);
}

void character_controller_update(CharacterController *c, uint32_t now_ms,
                                 bool collisions_enabled)
{
    uint16_t separation;
    int16_t gap;
    uint32_t first_time, second_time;
    bool recalibrated;
    unsigned int i;
    if (c == NULL || !c->initialized) return;
    for (i = 0U; i < 2U; ++i) character_update(c->players[i], now_ms);
    first_time = c->players[0]->sampled_ms;
    second_time = c->players[1]->sampled_ms;
    c->pair_valid = character_angle_is_valid(c->players[0])
        && character_angle_is_valid(c->players[1])
        && (first_time - second_time <= c->config.max_pair_sample_skew_ms
            || second_time - first_time <= c->config.max_pair_sample_skew_ms);
    if (!collisions_enabled || !c->pair_valid) {
        c->baseline_seen = false;
        c->armed = false;
        return;
    }
    separation = character_angular_distance(c->players[0]->angle_x10,
                                             c->players[1]->angle_x10);
    gap = signed_gap(c->players[0]->angle_x10, c->players[1]->angle_x10);
    recalibrated = c->baseline_seen
        && (c->calibration_revision[0] != c->players[0]->calibration_revision
            || c->calibration_revision[1] != c->players[1]->calibration_revision);
    if (recalibrated) {
        /* Offset jumps are not physical approaches. Require separation again. */
        c->armed = separation > c->config.release_x10;
    } else {
        if (!c->baseline_seen)
            c->armed = c->config.detect_initial_overlap || separation > c->config.release_x10;
        else if (separation > c->config.release_x10)
            c->armed = true;
        if (c->armed && separation <= c->config.enter_x10) {
            report_collision(c, now_ms, separation, gap);
            c->armed = false;
        }
    }
    c->previous_signed_gap_x10 = gap;
    c->previous_separation_x10 = separation;
    for (i = 0U; i < 2U; ++i)
        c->calibration_revision[i] = c->players[i]->calibration_revision;
    c->baseline_seen = true;
}

CharacterPairCollision character_controller_read_collision(CharacterController *c)
{
    CharacterPairCollision event = {0};
    if (c != NULL && c->initialized) {
        event = c->pending_event;
        c->pending_event.is_hit = false;
    }
    return event;
}

bool character_response_separate(void *context, const CharacterPairCollision *event,
                                 CharacterDirection outgoing[2])
{
    int16_t gap;
    int closing;
    (void)context;
    if (event == NULL || outgoing == NULL || !event->is_hit || !event->directions_valid)
        return false;
    gap = event->signed_gap_x10 != 0 ? event->signed_gap_x10 : event->previous_signed_gap_x10;
    if (gap == 0 || gap == 1800 || gap == -1800) return false;
    /* Direction ordering identifies head-on/separating motion without speed
     * data. Equal directions require measured approach (catch-up). */
    closing = ((int)event->incoming[0] - (int)event->incoming[1]) * (gap > 0 ? 1 : -1);
    if (closing < 0 || (closing == 0 && (!event->approach_known || !event->approaching)))
        return false;
    outgoing[0] = gap > 0 ? CHARACTER_NEGATIVE : CHARACTER_POSITIVE;
    outgoing[1] = gap > 0 ? CHARACTER_POSITIVE : CHARACTER_NEGATIVE;
    return true;
}
