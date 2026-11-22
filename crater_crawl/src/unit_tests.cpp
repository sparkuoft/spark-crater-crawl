#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "unit_tests.h"
#include "game_fsm.h"
#include "pillar_fsm.h"
#include "wheels_link.h"
#include "board_link_protocol.h"
#include "debug_log.h"

/* ---- Tiny test framework -------------------------------------------------
 * CHECK(condition) counts a check, and prints the line number if it fails.
 * Only line numbers are printed (not the condition's text) to save the
 * Mega's RAM: look the line up in this file. */

static unsigned s_checks = 0;
static unsigned s_failures = 0;
static uint32_t s_t = 0; // the pretend clock

// Prints even while the game's own logging is switched off.
static void say(const char *fmt, ...) LOG_PRINTF_FMT(1, 2);
static void say(const char *fmt, ...)
{
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    bool was_on = log_is_enabled();
    log_set_enabled(true);
    log_text("%s\n", buf);
    log_set_enabled(was_on);
}

#define CHECK(cond) check((cond), __LINE__)

static void check(bool ok, int line)
{
    s_checks++;
    if (!ok)
    {
        s_failures++;
        say("  FAIL at unit_tests.cpp line %d", line);
    }
}

/* ---- Helpers ------------------------------------------------------------ */

static void new_game()
{
    s_t = 0;
    game_fsm_init(s_t); // also resets the pillars and the sequencer
}

static void run_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        s_t++;
        game_fsm_update(s_t);
    }
}

static void start_round()
{
    new_game();
    game_start(s_t);
    run_ms(COUNTDOWN_MS);
}

static int first_pillar_in(PillarPhase phase)
{
    for (uint8_t p = 0; p < NUM_PILLARS; p++)
    {
        if (g_state.pillars[p].phase == phase)
            return p;
    }
    return -1;
}

// Runs until some pillar is fully up. Returns its number, or -1 if none got
// there within max_ms.
static int run_until_a_pillar_is_up(uint32_t max_ms)
{
    for (uint32_t i = 0; i < max_ms; i++)
    {
        int up = first_pillar_in(PILLAR_EXTENDED);
        if (up >= 0)
            return up;
        run_ms(1);
    }
    return -1;
}

static bool run_until_phase(GamePhase phase, uint32_t max_ms)
{
    for (uint32_t i = 0; i < max_ms && g_state.phase != phase; i++)
        run_ms(1);
    return g_state.phase == phase;
}

// Pretend the Wheels Mega just sent this line.
static void from_wheels(const char *line)
{
    wheels_link_handle_line(line, s_t);
}

static uint8_t count_pillars(bool moving_only)
{
    uint8_t n = 0;
    for (uint8_t p = 0; p < NUM_PILLARS; p++)
    {
        PillarPhase ph = g_state.pillars[p].phase;
        if (moving_only ? (ph == PILLAR_RISING || ph == PILLAR_LOWERING) : (ph != PILLAR_IDLE))
            n++;
    }
    return n;
}

/* ---- Tests --------------------------------------------------------------- */

static void test_round_flow()
{
    say("round flow: IDLE -> COUNTDOWN -> RUNNING, reset");
    new_game();
    CHECK(g_state.phase == PHASE_IDLE);
    game_start(s_t);
    CHECK(g_state.phase == PHASE_COUNTDOWN);
    run_ms(COUNTDOWN_MS - 1);
    CHECK(g_state.phase == PHASE_COUNTDOWN);
    run_ms(1);
    CHECK(g_state.phase == PHASE_RUNNING);
    game_start(s_t); // ignored mid-round
    CHECK(g_state.phase == PHASE_RUNNING);
    game_reset(s_t);
    CHECK(g_state.phase == PHASE_IDLE);
}

static void test_hits_need_a_raised_pillar()
{
    say("hits: only during a round, only from a pillar that's fully up");
    new_game();
    from_wheels("H 0 -1"); // no round yet
    CHECK(g_state.players[0].alive);

    start_round();
    from_wheels("H 0 -1"); // round on, but no pillar is up yet
    CHECK(g_state.players[0].alive);

    int up = run_until_a_pillar_is_up(20000);
    CHECK(up >= 0);
    if (up < 0)
        return;

    // Tracking names a pillar that isn't up: ignored.
    char line[LINK_LINE_MAX];
    snprintf(line, sizeof(line), "H 0 %d", (up + 1) % NUM_PILLARS);
    if (g_state.pillars[(up + 1) % NUM_PILLARS].phase != PILLAR_EXTENDED)
    {
        from_wheels(line);
        CHECK(g_state.players[0].alive);
    }

    // Unknown pillar (-1) while one is up: it counts. One hit is fatal.
    from_wheels("H 1 -1");
    CHECK(!g_state.players[1].alive);
    CHECK(g_state.players[0].alive);
    run_ms(1);
    CHECK(g_state.phase == PHASE_PLAYER_ELIMINATED);

    // Game over only once every pillar is back down.
    CHECK(run_until_phase(PHASE_GAME_OVER, 10000));
    CHECK(count_pillars(false) == 0);

    // A hit after the round is over is ignored.
    from_wheels("H 0 -1");
    CHECK(g_state.players[0].alive);
}

static void test_draw()
{
    say("draw: both players hit in the same loop");
    start_round();
    CHECK(run_until_a_pillar_is_up(20000) >= 0);
    from_wheels("H 0 -1");
    from_wheels("H 1 -1");
    CHECK(!g_state.players[0].alive && !g_state.players[1].alive);
    run_ms(1);
    CHECK(g_state.phase == PHASE_PLAYER_ELIMINATED);
}

static void test_difficulty_and_pillar_limit()
{
    say("difficulty steps at 30 s and 60 s; never more than 3 pillars active (takes a few seconds)");
    start_round();
    CHECK(game_difficulty() == WARN_EASY);

    uint8_t most_active = 0;
    uint8_t most_moving = 0;
    for (uint32_t ms = 1; ms <= DIFFICULTY_HARD_AT_MS + 3000UL; ms++)
    {
        run_ms(1);
        uint8_t active = count_pillars(false);
        uint8_t moving = count_pillars(true);
        if (active > most_active)
            most_active = active;
        if (moving > most_moving)
            most_moving = moving;

        if (ms == DIFFICULTY_NORMAL_AT_MS - 1)
            CHECK(game_difficulty() == WARN_EASY);
        if (ms == DIFFICULTY_NORMAL_AT_MS)
            CHECK(game_difficulty() == WARN_NORMAL);
        if (ms == DIFFICULTY_HARD_AT_MS - 1)
            CHECK(game_difficulty() == WARN_NORMAL);
        if (ms == DIFFICULTY_HARD_AT_MS)
            CHECK(game_difficulty() == WARN_HARD);
    }
    CHECK(most_active > 0); // the sequencer did raise pillars
    CHECK(most_active <= MAX_SIMULTANEOUS_PILLAR_MOVES);
    CHECK(most_moving <= MAX_SIMULTANEOUS_PILLAR_MOVES);
    CHECK(g_state.phase == PHASE_RUNNING);
}

static void test_one_pillar_cycle()
{
    say("one pillar: warning -> rising -> up -> lowering -> down");
    new_game(); // IDLE, so the sequencer stays quiet
    CHECK(pillarStartWarning(2, WARN_EASY, false, s_t));
    CHECK(!pillarStartWarning(2, WARN_EASY, false, s_t)); // already busy
    CHECK(g_state.pillars[2].phase == PILLAR_WARNING);

    // 1500 ms is the EASY warning in pillar_fsm.cpp (and LEDGameLogic.cpp).
    run_ms(1499);
    CHECK(g_state.pillars[2].phase == PILLAR_WARNING);
    run_ms(1);
    CHECK(g_state.pillars[2].phase == PILLAR_RISING);
    run_ms(PILLAR_RISE_MS);
    CHECK(g_state.pillars[2].phase == PILLAR_EXTENDED);

    // Holds at the top (PILLAR_HOLD_MS in pillar_fsm.cpp), then comes down.
    uint32_t held = 0;
    while (g_state.pillars[2].phase == PILLAR_EXTENDED && held < 5000)
    {
        run_ms(1);
        held++;
    }
    CHECK(g_state.pillars[2].phase == PILLAR_LOWERING);
    CHECK(held > 0 && held < 5000);
    run_ms(PILLAR_LOWER_MS);
    CHECK(g_state.pillars[2].phase == PILLAR_IDLE);
}

static void test_link_messages()
{
    say("link messages: parsing and building");
    LinkMessage m;
    CHECK(link_parse("T 1 1 0", &m) && m.type == LINK_MSG_TRIGGER && m.player == 1 && m.fatal && m.hold_ms == 0);
    CHECK(link_parse("T 0 0 1200", &m) && !m.fatal && m.hold_ms == 1200);
    CHECK(link_parse("H 0 -1", &m) && m.type == LINK_MSG_HIT && m.player == 0 && m.pillar == -1);
    CHECK(link_parse("H 1 5", &m) && m.pillar == 5);
    CHECK(link_parse("A 1 3599 2", &m) && m.type == LINK_MSG_ANGLE && m.angle_x10 == 3599 && m.state == LINK_TRACK_READY);
    CHECK(link_parse("X", &m) && m.type == LINK_MSG_RELEASE);
    CHECK(link_parse("B", &m) && m.type == LINK_MSG_BOOT);
    CHECK(link_parse("M 1", &m) && m.type == LINK_MSG_HOME && m.player == 1);

    // Malformed or out of range: all rejected.
    CHECK(!link_parse("A 1 3600 2", &m)); // angle too big
    CHECK(!link_parse("H 2 0", &m));      // no player 2
    CHECK(!link_parse("T 0 1", &m));      // missing hold time
    CHECK(!link_parse("X 1", &m));        // extra field
    CHECK(!link_parse("A 0 12a 2", &m));  // not a number
    CHECK(!link_parse("H 0  1", &m));     // two spaces
    CHECK(!link_parse("", &m));
    CHECK(!link_parse("Q 1", &m));

    // What one side builds, the other side reads back the same.
    char buf[LINK_LINE_MAX];
    link_format_trigger(buf, 1, true, 1200);
    CHECK(strcmp(buf, "T 1 1 1200\n") == 0);
    buf[strlen(buf) - 1] = '\0';
    CHECK(link_parse(buf, &m) && m.player == 1 && m.fatal && m.hold_ms == 1200);
    link_format_angle(buf, 0, 1234, LINK_TRACK_HOMING);
    CHECK(strcmp(buf, "A 0 1234 1\n") == 0);

    // Lines are put back together from single bytes; \r is ignored, and a
    // line that's too long is thrown away whole.
    LinkLineReader r = {};
    const char *bytes = "A 0 5 2\r\n";
    int complete = 0;
    for (const char *c = bytes; *c; c++)
        complete += link_reader_feed(&r, *c) ? 1 : 0;
    CHECK(complete == 1 && strcmp(r.line, "A 0 5 2") == 0);
    for (int i = 0; i < 40; i++)
        CHECK(!link_reader_feed(&r, 'x'));
    CHECK(!link_reader_feed(&r, '\n'));
}

static void test_link_angles()
{
    say("link angles: only a valid angle reaches g_state");
    new_game();
    from_wheels("A 0 1234 2");
    CHECK(g_state.players[0].angle_x10 == 1234);
    CHECK(wheels_link_status().track_state[0] == LINK_TRACK_READY);
    from_wheels("A 0 50 3"); // homed, but no valid angle: keep the old one
    CHECK(g_state.players[0].angle_x10 == 1234);
    CHECK(wheels_link_status().track_state[0] == LINK_TRACK_NO_ANGLE);
}

/* ---- Runner -------------------------------------------------------------- */

bool run_unit_tests(uint32_t now_ms)
{
    bool log_was_on = log_is_enabled();
    log_set_enabled(false);     // keep the game's own prints out of the way
    wheels_link_set_muted(true); // don't move real motors on the Wheels Mega
    s_checks = 0;
    s_failures = 0;

    say("=== unit tests ===");
    test_round_flow();
    test_hits_need_a_raised_pillar();
    test_draw();
    test_difficulty_and_pillar_limit();
    test_one_pillar_cycle();
    test_link_messages();
    test_link_angles();
    say("=== %u checks, %u failed: %s ===", s_checks, s_failures, s_failures == 0 ? "ALL PASSED" : "SEE FAILURES ABOVE");

    // Back to a clean game on the real clock.
    wheels_link_set_muted(false);
    wheels_link_restart();
    log_set_enabled(log_was_on);
    game_fsm_init(now_ms);
    return s_failures == 0;
}
