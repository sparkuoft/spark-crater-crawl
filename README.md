# spark-crater-crawl

## Running the game logic on a laptop

The game logic is plain C++ for now (no Arduino yet), so it builds and runs on a computer:

    clang++ -std=c++11 -Wall -Wextra -pthread -o crater_crawl *.cpp

Script mode. Time only moves when you type `+<ms>`, so the same input always gives the same output:

    ./crater_crawl

Live mode. Time runs for real; type `h0` or `h1` while a pillar is up (`#`) to hit that player:

    ./crater_crawl --live

Fall design review demo, either instantly or played back at real speed (about 70 s):

    ./crater_crawl < demo/fall_design_review.txt
    ./crater_crawl --live < demo/fall_design_review.txt

Commands are listed when the program starts. The arena line shows each pillar as
`.` down, `!` warning, `^` rising, `#` up, `v` lowering.
