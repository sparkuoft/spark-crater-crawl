// Link check (both boards)
//
// Checks the serial cable between the Game Mega and the Wheels Mega before
// running the real sketches. Flash this SAME sketch onto both boards and wire
// them like this (as in board_link_protocol.h):
//   Game pin 16 (TX2) -> Wheels pin 17 (RX2)
//   Game pin 17 (RX2) <- Wheels pin 16 (TX2)
//   Game GND          -- Wheels GND
//
// Open each board's Serial Monitor at 115200. Each board sends "ping <n>" to
// the other every half second and prints what it receives, so both Monitors
// should show "got: ping ..." twice a second.
// If one says "nothing received", check the wire going INTO that board
// (its pin 17) and the ground wire.

#define LINK_BAUD 115200UL
#define SEND_PERIOD_MS 500UL
#define SILENT_WARNING_MS 2000UL

static char s_line[32];
static uint8_t s_len = 0;
static uint32_t s_last_send_ms = 0;
static uint32_t s_last_heard_ms = 0;
static uint32_t s_count = 0;

void setup()
{
    Serial.begin(115200);
    Serial2.begin(LINK_BAUD);
    Serial.println(F("Link check: sending pings on Serial2 (pins 16/17)"));
}

void loop()
{
    uint32_t now = millis();

    if (now - s_last_send_ms >= SEND_PERIOD_MS)
    {
        s_last_send_ms = now;
        Serial2.print(F("ping "));
        Serial2.println(s_count++);
    }

    while (Serial2.available() > 0)
    {
        char c = (char)Serial2.read();
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            s_line[s_len] = '\0';
            s_len = 0;
            s_last_heard_ms = now;
            Serial.print(F("got: "));
            Serial.println(s_line);
        }
        else if (s_len < sizeof(s_line) - 1)
        {
            s_line[s_len++] = c;
        }
    }

    if (now - s_last_heard_ms >= SILENT_WARNING_MS)
    {
        s_last_heard_ms = now;
        Serial.println(F("nothing received for 2 s: check the wire into pin 17, and GND"));
    }
}
