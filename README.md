# spark-crater-crawl

Crater Crawl runs on **two Arduino Mega 2560s** joined by a serial cable. How it all
fits together, the wiring, and a step-by-step test plan are in
[docs/INTEGRATION.md](docs/INTEGRATION.md).

| Folder | What it is | Board |
| --- | --- | --- |
| `crater_crawl/` | Game sketch: game FSM, pillars and sequencer, LED strip, Serial console, unit tests | Game Mega |
| `crater_crawl_wheels/` | Wheels sketch (Alice): steering wheels, character steppers, plus Tracking's sensors | Wheels Mega |
| `hardware_tests/` | Small check sketches: LED strip, link cable, Tracking's inputs | either |
| `laptop/` | Runs the game sketch's logic on a computer, for testing without hardware | laptop |
| `Photo Interrupt Sensor/` | Tracking's PCB, tests and docs | |
| `demo/` | Demo script for the laptop build | |

## Arduino IDE setup

Board: **Arduino Mega or Mega 2560**. Install these from the Library Manager:
FastLED, TimerOne, TCA9548A, Servo.

Open `crater_crawl/crater_crawl.ino` and upload it to the Game Mega; open
`crater_crawl_wheels/crater_crawl_wheels.ino` and upload it to the Wheels Mega.
Serial Monitor: 115200 baud, "Newline". On the Game Mega, type `?` for status and
`t` to run the unit tests.

## Laptop build

    clang++ -std=c++11 -Wall -Wextra -pthread -Icrater_crawl/src -o test laptop/main.cpp crater_crawl/src/*.cpp
    ./test --test                              # unit tests
    ./test < demo/fall_design_review.txt       # demo round
    ./test --live                              # play in real time
