// LED strip check (Game Mega)
//
// Pillars' WS2812B strip: 5 LEDs per pillar, pillar 0 first, data on pin 6
// (the same settings as LEDGameLogic.cpp). Flash this, open the Serial Monitor
// at 115200, and watch the strip:
//   1. the whole strip goes red, green, blue: checks every LED and that the
//      colours are in the right order (red really looks red)
//   2. each pillar's 5 LEDs light up white in turn, with the pillar number
//      printed: checks the pillars are in the right order
//   3. pillar 0 plays the EASY warning: yellow flashing, fast orange, then
//      solid red, the way the game shows a pillar about to rise
//
// Power: the full strip can draw nearly 2 A at full white. This check runs
// dim so USB power is enough. In the real game, power the strip from a 5 V
// supply rather than the Mega's 5V pin, and connect the supply's ground to
// the Mega's GND.
//
// This is a standalone check, so it uses delay(); the game itself never does.

#include <FastLED.h>

#define LED_PIN 6
#define NUM_PILLARS 6
#define LEDS_PER_PILLAR 5
#define TOTAL_LEDS (NUM_PILLARS * LEDS_PER_PILLAR)
#define BRIGHTNESS 40 // out of 255; the game uses 128

CRGB leds[TOTAL_LEDS];

static void fill_pillar(uint8_t pillar, CRGB colour)
{
    for (uint8_t i = 0; i < LEDS_PER_PILLAR; i++)
        leds[pillar * LEDS_PER_PILLAR + i] = colour;
}

static void whole_strip(CRGB colour, const char *name)
{
    Serial.print(F("whole strip: "));
    Serial.println(name);
    fill_solid(leds, TOTAL_LEDS, colour);
    FastLED.show();
    delay(1000);
}

void setup()
{
    Serial.begin(115200);
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, TOTAL_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear(true);
    Serial.println(F("LED strip check: data on pin 6, 30 LEDs (6 pillars x 5)"));
}

void loop()
{
    whole_strip(CRGB::Red, "red");
    whole_strip(CRGB::Green, "green");
    whole_strip(CRGB::Blue, "blue");

    for (uint8_t p = 0; p < NUM_PILLARS; p++)
    {
        Serial.print(F("pillar "));
        Serial.println(p);
        FastLED.clear();
        fill_pillar(p, CRGB::White);
        FastLED.show();
        delay(700);
    }

    Serial.println(F("pillar 0 warning: yellow flashing, orange, red"));
    FastLED.clear();
    for (uint16_t t = 0; t < 1200; t += 500) // yellow: toggles every 500 ms
    {
        fill_pillar(0, (t / 500) % 2 == 0 ? CRGB::Yellow : CRGB::Black);
        FastLED.show();
        delay(500);
    }
    for (uint8_t i = 0; i < 6; i++) // orange: toggles every 100 ms
    {
        fill_pillar(0, i % 2 == 0 ? CRGB(255, 60, 0) : CRGB::Black);
        FastLED.show();
        delay(100);
    }
    fill_pillar(0, CRGB::Red); // rising / up
    FastLED.show();
    delay(1500);

    FastLED.clear(true);
    Serial.println(F("--- again in 2 s"));
    delay(2000);
}
