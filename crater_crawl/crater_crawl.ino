#include "src/shared_state.h"
#include "src/game_fsm.h"
#include "src/console.h"
#include "src/pillar_led.h"
#include <Arduino.h>

#define CMD_MAX_LEN 32

// Each FastLED.show() turns interrupts off for about 1 ms, so the strip is
// refreshed at 50 Hz rather than every loop. The fastest flash is 100 ms.
#define LED_UPDATE_INTERVAL_MS 20

static uint32_t s_last_led_update_ms = 0;

static char s_line[CMD_MAX_LEN];
static uint8_t s_len = 0;
static bool s_overflow = false;

// Collects Serial bytes into s_line. Returns true when a complete, non-empty
// line is ready. Lines that are too long are dropped.
static bool read_line()
{
    while (Serial.available() > 0)
    {
        char c = (char)Serial.read();

        if (c == '\r')
            continue;

        if (c == '\n')
        {
            bool ok = !s_overflow;
            s_line[s_len] = '\0';
            s_len = 0;
            s_overflow = false;

            if (ok && s_line[0] != '\0')
                return true;
            continue;
        }

        if (s_len < CMD_MAX_LEN - 1)
            s_line[s_len++] = c;
        else
            s_overflow = true;
    }
    return false;
}

void setup()
{
    Serial.begin(115200);

    uint32_t now = millis();
    game_fsm_init(now);
    pillarLedsInit();
    console_init(now);
}

void loop()
{
    uint32_t now = millis();

    if (read_line())
        console_handle_line(s_line, now);

    // Same order as run_loop_once() on the laptop: game first, then console.
    game_fsm_update(now);
    console_tick(now);

    if (now - s_last_led_update_ms >= LED_UPDATE_INTERVAL_MS)
    {
        s_last_led_update_ms = now;
        pillarLedsUpdate(now);
    }
}