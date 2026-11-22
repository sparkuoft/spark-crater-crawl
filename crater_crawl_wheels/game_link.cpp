#include "game_link.h"
#include "tracking_hal.h"
#include "src/board_link_protocol.h"

static LinkLineReader s_reader = {};
static uint32_t s_last_angle_report_ms = 0;

// Per player: a timed freeze after a plain hit, or a hold after a fatal one.
static bool s_freeze_on[NUM_TRACKED];
static uint32_t s_freeze_until_ms[NUM_TRACKED];
static bool s_held[NUM_TRACKED];

// Sends a line only if it fits in the send buffer right now. Returns false
// if it didn't fit; the caller tries again on a later loop.
static bool try_send(const char *buf, int len)
{
    if (len <= 0 || Serial2.availableForWrite() < len)
        return false;
    Serial2.write((const uint8_t *)buf, (size_t)len);
    return true;
}

static void handle_command(const LinkMessage &m, uint32_t now_ms)
{
    switch (m.type)
    {
    case LINK_MSG_TRIGGER:
        if (m.fatal)
        {
            s_held[m.player] = true;
        }
        else
        {
            s_freeze_on[m.player] = true;
            s_freeze_until_ms[m.player] = now_ms + m.hold_ms;
        }
        break;

    case LINK_MSG_RELEASE:
        for (uint8_t p = 0; p < NUM_TRACKED; p++)
        {
            s_freeze_on[p] = false;
            s_held[p] = false;
        }
        break;

    case LINK_MSG_HOME:
        tracking_home(m.player, now_ms);
        break;

    default:
        break; // B, H and A only go the other way
    }
}

void game_link_begin()
{
    Serial2.begin(LINK_BAUD);
    char buf[LINK_LINE_MAX];
    try_send(buf, link_format_boot(buf));
}

void game_link_update(uint32_t now_ms)
{
    // Commands from the Game Mega.
    while (Serial2.available() > 0)
    {
        LinkMessage m;
        if (link_reader_feed(&s_reader, (char)Serial2.read()) && link_parse(s_reader.line, &m))
            handle_command(m, now_ms);
    }

    char buf[LINK_LINE_MAX];

    // Hit-switch presses go out first. Tracking keeps up to four per player,
    // so one that doesn't fit now waits there for the next loop.
    while (Serial2.availableForWrite() >= LINK_LINE_MAX)
    {
        uint8_t player = 0;
        int16_t pillar = -1;
        if (!tracking_take_hit(&player, &pillar))
            break;
        try_send(buf, link_format_hit(buf, player, pillar));
    }

    // Angle reports (about 11 bytes each). One that doesn't fit is skipped;
    // the next period sends a fresh one.
    if (now_ms - s_last_angle_report_ms >= LINK_ANGLE_PERIOD_MS)
    {
        s_last_angle_report_ms = now_ms;
        for (uint8_t p = 0; p < NUM_TRACKED; p++)
            try_send(buf, link_format_angle(buf, p, tracking_angle_x10(p), tracking_link_state(p)));
    }
}

bool game_link_player_frozen(uint8_t player_id, uint32_t now_ms)
{
    if (player_id >= NUM_TRACKED)
        return false;
    if (s_freeze_on[player_id] && (int32_t)(now_ms - s_freeze_until_ms[player_id]) >= 0)
        s_freeze_on[player_id] = false;
    return s_held[player_id] || s_freeze_on[player_id];
}
