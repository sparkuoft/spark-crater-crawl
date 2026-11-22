#include "wheels_link.h"
#include "board_link_protocol.h"
#include "game_fsm.h"
#include "debug_log.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

static_assert(NUM_PLAYERS == LINK_NUM_PLAYERS, "board_link_protocol.h and shared_state.h disagree on the player count");

/* If nothing arrives for this long, the console says the Wheels Mega is gone.
 * It sends an A message every LINK_ANGLE_PERIOD_MS, so this is ten missed. */
#define LINK_SILENT_MS 1000UL

#define NO_PILLAR 0xFF

static WheelsLinkStatus s_status = {};
static bool s_muted = false;
static bool s_reported_silent = false;

#ifdef ARDUINO
static LinkLineReader s_reader = {};

static void send_line(const char *buf)
{
    if (!s_muted)
        Serial2.print(buf);
}
#else
static void send_line(const char *buf)
{
    (void)buf;
}
#endif

void wheels_link_init()
{
#ifdef ARDUINO
    Serial2.begin(LINK_BAUD);
#endif
}

void wheels_link_update(uint32_t now_ms)
{
#ifdef ARDUINO
    // Only reads what's already arrived, so this never waits.
    while (Serial2.available() > 0)
    {
        if (link_reader_feed(&s_reader, (char)Serial2.read()))
            wheels_link_handle_line(s_reader.line, now_ms);
    }
#endif

    if (s_status.heard && !s_reported_silent && now_ms - s_status.last_heard_ms >= LINK_SILENT_MS)
    {
        s_reported_silent = true;
        log_line(now_ms, "LINK nothing from the Wheels Mega for %lu ms: check the Serial2 cable", LINK_SILENT_MS);
    }
}

void wheels_link_send_trigger(const WheelsTrigger &t, uint16_t hold_ms)
{
    char buf[LINK_LINE_MAX];
    link_format_trigger(buf, t.player_id, t.fatal, hold_ms);
    send_line(buf);
}

void wheels_link_release_all()
{
    char buf[LINK_LINE_MAX];
    link_format_release(buf);
    send_line(buf);
}

void wheels_link_home(uint8_t player_id)
{
    char buf[LINK_LINE_MAX];
    link_format_home(buf, player_id);
    send_line(buf);
}

// Which pillar hit the player. Tracking's number if it has one; otherwise the
// switch only registers at full extension (Tracking's design), so it was a
// pillar that's EXTENDED right now. Several up: the first one, which is fine
// while the game only uses the pillar for the EXTENDED check and the log.
static uint8_t choose_pillar(int16_t tracking_pillar)
{
    if (tracking_pillar >= 0 && tracking_pillar < NUM_PILLARS)
        return (uint8_t)tracking_pillar;

    for (uint8_t p = 0; p < NUM_PILLARS; p++)
    {
        if (g_state.pillars[p].phase == PILLAR_EXTENDED)
            return p;
    }
    return NO_PILLAR;
}

static void handle_hit(const LinkMessage &m, uint32_t now_ms)
{
    uint8_t pillar = choose_pillar(m.pillar);
    if (pillar == NO_PILLAR)
    {
        log_line(now_ms, "LINK P%u switch pressed but no pillar is up: ignored", (unsigned)m.player);
        return;
    }

    // The Wheels Mega has its own clock, so the hit is timed on ours, when it
    // arrives. game_on_collision decides whether it counts.
    CollisionEvent ev;
    ev.player_id = m.player;
    ev.pillar_id = pillar;
    ev.hit = true;
    ev.timestamp_ms = now_ms;
    game_on_collision(ev, now_ms);
}

static void handle_angle(const LinkMessage &m, uint32_t now_ms)
{
    uint8_t old_state = s_status.track_state[m.player];
    s_status.track_state[m.player] = m.state;

    // Only a valid angle replaces the one in g_state; otherwise the old one stays.
    if (m.state == LINK_TRACK_READY)
        g_state.players[m.player].angle_x10 = m.angle_x10;

    if (m.state != old_state)
    {
        static const char *const names[] = {"not homed", "homing", "ready", "homed, no angle", "homing failed"};
        log_line(now_ms, "LINK P%u tracking: %s", (unsigned)m.player, names[m.state]);
    }
}

void wheels_link_handle_line(const char *line, uint32_t now_ms)
{
    LinkMessage m;
    if (!link_parse(line, &m))
    {
        s_status.bad_lines++;
        log_line(now_ms, "LINK ignored a bad line from the Wheels Mega: \"%s\"", line);
        return;
    }

    if (s_reported_silent)
        log_line(now_ms, "LINK Wheels Mega is back");
    s_reported_silent = false;
    s_status.heard = true;
    s_status.last_heard_ms = now_ms;

    switch (m.type)
    {
    case LINK_MSG_BOOT:
        log_line(now_ms, "LINK Wheels Mega started");
        break;
    case LINK_MSG_HIT:
        handle_hit(m, now_ms);
        break;
    case LINK_MSG_ANGLE:
        handle_angle(m, now_ms);
        break;
    default:
        // T, X and M only go the other way.
        s_status.bad_lines++;
        log_line(now_ms, "LINK ignored a Game-to-Wheels message from the Wheels Mega: \"%s\"", line);
        break;
    }
}

void wheels_link_restart()
{
    s_status = WheelsLinkStatus();
    s_reported_silent = false;
#ifdef ARDUINO
    s_reader = LinkLineReader();
    while (Serial2.available() > 0)
        Serial2.read();
#endif
}

void wheels_link_set_muted(bool muted)
{
    s_muted = muted;
}

const WheelsLinkStatus &wheels_link_status()
{
    return s_status;
}
