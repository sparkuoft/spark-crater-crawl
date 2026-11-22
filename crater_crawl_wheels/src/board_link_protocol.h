#ifndef BOARD_LINK_PROTOCOL_H
#define BOARD_LINK_PROTOCOL_H

/* Messages between the Game Mega and the Wheels Mega.
 *
 * Both sketches keep an IDENTICAL copy of this file (Arduino can't include
 * files from outside a sketch folder):
 *   crater_crawl/src/board_link_protocol.h
 *   crater_crawl_wheels/src/board_link_protocol.h
 * Change both together.
 *
 * Wiring: Serial2 on both boards, crossed over, plus a shared ground:
 *   Game pin 16 (TX2) -> Wheels pin 17 (RX2)
 *   Game pin 17 (RX2) <- Wheels pin 16 (TX2)
 *   Game GND          -- Wheels GND
 *
 * Every message is one line of plain text ending in '\n', so either side can
 * be watched in a Serial Monitor. Numbers are decimal, separated by one space.
 *
 * Game -> Wheels
 *   T <player> <fatal> <hold_ms>   player was hit (WheelsTrigger).
 *                                  fatal 0: freeze/resist for hold_ms, then
 *                                  release by itself. fatal 1: hold until X.
 *   X                              release every player (game reset)
 *   M <player>                     start homing that player
 *
 * Wheels -> Game
 *   B                              Wheels board started (sent once at boot)
 *   H <player> <pillar>            that player's hit switch was pressed.
 *                                  pillar: Tracking's pillar index, or -1 if
 *                                  Tracking doesn't know which pillar it was
 *   A <player> <angle_x10> <state> position report, every LINK_ANGLE_PERIOD_MS.
 *                                  angle in tenths of a degree, 0..3599.
 *                                  state: one of LinkTrackState below
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define LINK_BAUD 115200UL
#define LINK_LINE_MAX 32           /* longest line, including the '\0' */
#define LINK_ANGLE_PERIOD_MS 100UL /* how often the Wheels Mega reports angles */
#define LINK_NUM_PLAYERS 2
#define LINK_MAX_HOLD_MS 60000L

/* Tracking state for one player, as sent in an A message. */
enum LinkTrackState : uint8_t
{
    LINK_TRACK_NOT_HOMED = 0, /* never homed, or homing was cancelled */
    LINK_TRACK_HOMING = 1,    /* waiting for the character to pass the photo-interrupter */
    LINK_TRACK_READY = 2,     /* homed, and the angle is valid */
    LINK_TRACK_NO_ANGLE = 3,  /* homed, but the latest encoder read failed */
    LINK_TRACK_FAILED = 4     /* homing timed out or failed; send M to retry */
};

enum LinkMessageType : uint8_t
{
    LINK_MSG_NONE = 0,
    LINK_MSG_TRIGGER, /* T */
    LINK_MSG_RELEASE, /* X */
    LINK_MSG_HOME,    /* M */
    LINK_MSG_BOOT,    /* B */
    LINK_MSG_HIT,     /* H */
    LINK_MSG_ANGLE    /* A */
};

struct LinkMessage
{
    LinkMessageType type;
    uint8_t player;     /* T, M, H, A */
    bool fatal;         /* T */
    uint16_t hold_ms;   /* T */
    int16_t pillar;     /* H: 0..NUM_PILLARS-1, or -1 */
    uint16_t angle_x10; /* A */
    uint8_t state;      /* A: LinkTrackState */
};

/* Reads " <number>" at s, checks it's in lo..hi and moves s past it. */
static inline bool link_read_number(const char *&s, long lo, long hi, long *out)
{
    if (*s != ' ')
        return false;
    s++;
    if (*s != '-' && (*s < '0' || *s > '9'))
        return false;

    char *end = 0;
    long value = strtol(s, &end, 10);
    if (end == s || value < lo || value > hi)
        return false;
    s = end;
    *out = value;
    return true;
}

/* Parses one line (without its '\n'). Returns false for anything malformed or
 * out of range; the caller just ignores those lines. */
static inline bool link_parse(const char *line, LinkMessage *out)
{
    LinkMessage m = {};
    long a = 0, b = 0, c = 0;
    const char *s = line + 1;

    switch (line[0])
    {
    case 'T':
        if (!link_read_number(s, 0, LINK_NUM_PLAYERS - 1, &a) || !link_read_number(s, 0, 1, &b) ||
            !link_read_number(s, 0, LINK_MAX_HOLD_MS, &c))
            return false;
        m.type = LINK_MSG_TRIGGER;
        m.player = (uint8_t)a;
        m.fatal = (b == 1);
        m.hold_ms = (uint16_t)c;
        break;

    case 'X':
        m.type = LINK_MSG_RELEASE;
        break;

    case 'M':
        if (!link_read_number(s, 0, LINK_NUM_PLAYERS - 1, &a))
            return false;
        m.type = LINK_MSG_HOME;
        m.player = (uint8_t)a;
        break;

    case 'B':
        m.type = LINK_MSG_BOOT;
        break;

    case 'H':
        if (!link_read_number(s, 0, LINK_NUM_PLAYERS - 1, &a) || !link_read_number(s, -1, 127, &b))
            return false;
        m.type = LINK_MSG_HIT;
        m.player = (uint8_t)a;
        m.pillar = (int16_t)b;
        break;

    case 'A':
        if (!link_read_number(s, 0, LINK_NUM_PLAYERS - 1, &a) || !link_read_number(s, 0, 3599, &b) ||
            !link_read_number(s, 0, LINK_TRACK_FAILED, &c))
            return false;
        m.type = LINK_MSG_ANGLE;
        m.player = (uint8_t)a;
        m.angle_x10 = (uint16_t)b;
        m.state = (uint8_t)c;
        break;

    default:
        return false;
    }

    if (*s != '\0') // nothing may follow the last field
        return false;
    *out = m;
    return true;
}

/* Builders. buf must hold LINK_LINE_MAX bytes. Each adds the '\n'. */
static inline int link_format_trigger(char *buf, uint8_t player, bool fatal, uint16_t hold_ms)
{
    return snprintf(buf, LINK_LINE_MAX, "T %u %u %u\n", (unsigned)player, fatal ? 1u : 0u, (unsigned)hold_ms);
}

static inline int link_format_release(char *buf)
{
    return snprintf(buf, LINK_LINE_MAX, "X\n");
}

static inline int link_format_home(char *buf, uint8_t player)
{
    return snprintf(buf, LINK_LINE_MAX, "M %u\n", (unsigned)player);
}

static inline int link_format_boot(char *buf)
{
    return snprintf(buf, LINK_LINE_MAX, "B\n");
}

static inline int link_format_hit(char *buf, uint8_t player, int16_t pillar)
{
    return snprintf(buf, LINK_LINE_MAX, "H %u %d\n", (unsigned)player, (int)pillar);
}

static inline int link_format_angle(char *buf, uint8_t player, uint16_t angle_x10, uint8_t state)
{
    return snprintf(buf, LINK_LINE_MAX, "A %u %u %u\n", (unsigned)player, (unsigned)angle_x10, (unsigned)state);
}

/* Collects incoming bytes into whole lines, without ever waiting. Feed it one
 * byte at a time; it returns true when line holds a complete, non-empty line.
 * Lines that are too long are dropped whole. */
struct LinkLineReader
{
    char line[LINK_LINE_MAX];
    uint8_t len;
    bool overflow;
};

static inline bool link_reader_feed(LinkLineReader *r, char c)
{
    if (c == '\r')
        return false;
    if (c == '\n')
    {
        bool ok = !r->overflow && r->len > 0;
        r->line[r->len] = '\0';
        r->len = 0;
        r->overflow = false;
        return ok;
    }
    if (r->len < LINK_LINE_MAX - 1)
        r->line[r->len++] = c;
    else
        r->overflow = true;
    return false;
}

#endif
