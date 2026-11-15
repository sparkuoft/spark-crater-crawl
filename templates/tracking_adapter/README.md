# Tracking adapter template

Connects Tracking's code (`Photo Interrupt Sensor/CharacterModule.*`) to the game.
Nothing in this folder is compiled until you copy it into the sketch.

`tracking_adapter.h/.cpp` is GL's part: it calls Tracking's functions, turns switch
hits into `CollisionEvent`s and writes `angle_x10`.

The hardware side (encoder reads, photo-interrupters, limit switches, pins and
interrupts) is Tracking's own code, and homing movement is Wheels'. The adapter's
TODOs say what it needs from them.

Decisions it follows: a hit is a limit-switch press and only registers at full pillar
extension; there are no player-player collisions, so Tracking's `CharacterController`
isn't used; GL picks the pillar ID for each hit.

## Setup

1. Move Tracking's `CharacterModule.h` and `CharacterModule.c` into
   `crater_crawl/src/tracking/`. The Arduino IDE only builds the sketch folder and `src/`.
   - Leave `Photo Interrupt Sensor/tests/` out of `src/`: `test_tracking.c` has its own
     `main()` and would break the sketch. Tell Tracking: their `run_tests.py` looks for
     the `.c` files one folder above `tests/`.
   - `CharacterController.*` isn't needed.
2. Copy `tracking_adapter.h` and `tracking_adapter.cpp` into `crater_crawl/src/`,
   next to Tracking's hardware file. Their hardware file must be Mega-only
   (`#ifdef ARDUINO`), or the laptop build stops compiling.
3. In `crater_crawl/crater_crawl.ino`:
   - add `#include "src/tracking_adapter.h"`
   - `setup()`, after `console_init(now)`:
     `tracking_init(); tracking_home(0, now); tracking_home(1, now);`
   - `loop()`, after the Serial command and before `game_fsm_update(now)`:
     `tracking_update(now);`
4. Compile. The adapter builds as-is and does nothing until its TODOs are filled in.
5. Fill in the TODOs in `tracking_adapter.cpp` in order, compiling after each.
6. Start check: in `console.cpp`, `case 's':`, only start when Tracking is ready.
   Mega-only, so the laptop build can still start games:

       #ifdef ARDUINO
           if (!tracking_ready())
           {
               log_line(now_ms, "start ignored: players not homed yet");
               break;
           }
       #endif

7. For first tests on the board, before Wheels' homing code exists, Tracking's
   `start_homing` callback can return `true` without moving, and you push the figure
   past the photo-interrupter by hand.

## Laptop build

The laptop build has to compile Tracking's C file as C, then link it in:

    clang -std=c11 -c crater_crawl/src/tracking/CharacterModule.c -o CharacterModule.o
    clang++ -std=c++11 -Wall -Wextra -pthread -Icrater_crawl/src -o test laptop/main.cpp crater_crawl/src/*.cpp CharacterModule.o

`laptop/main.cpp` doesn't call the adapter, so the laptop game works as before.
