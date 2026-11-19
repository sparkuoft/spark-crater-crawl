"""Compile and link the root sketch for Uno/Mega using installed Arduino AVR tools.

Run: python tests/compile_resistance.py
This performs no upload and does not run motors.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(os.environ["LOCALAPPDATA"]) / "Arduino15"
TOOLS = DATA / "packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin"
CORE = DATA / "packages/arduino/hardware/avr/1.8.8"
LIBRARY_ROOTS = [DATA / "libraries", Path.home() / "Documents/Arduino/libraries",
                 Path.home() / "OneDrive/Documents/Arduino/libraries"]
timer_headers = [p for directory in LIBRARY_ROOTS for p in directory.glob("**/TimerOne.h")]
if not timer_headers:
    raise SystemExit("TimerOne library not found. Install it through Arduino Library Manager.")
TIMER = timer_headers[0].parent
WIRE = CORE / "libraries/Wire/src"
BUILD = Path(tempfile.mkdtemp(prefix="crater-resistance-build-"))


def run(command):
    result = subprocess.run([str(arg) for arg in command], capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    return result.stdout


for board, mcu, variant, board_define in [
    ("uno", "atmega328p", "standard", "ARDUINO_AVR_UNO"),
    ("mega", "atmega2560", "mega", "ARDUINO_AVR_MEGA2560"),
]:
    directory = BUILD / board
    directory.mkdir()
    flags = ["-mmcu=" + mcu, "-DF_CPU=16000000L", "-DARDUINO=10819",
             "-DARDUINO_ARCH_AVR", "-D" + board_define, "-Os",
             "-ffunction-sections", "-fdata-sections"]
    for include in [ROOT, CORE / "cores/arduino", CORE / "variants" / variant, WIRE, TIMER]:
        flags.extend(["-I", str(include)])
    sources = list(ROOT.glob("*.cpp")) + [ROOT / "arduino_ffb_tb6614.ino"]
    for library in [WIRE, TIMER]:
        sources.extend(library.rglob("*.cpp"))
        sources.extend(library.rglob("*.c"))
    core_sources = [p for p in (CORE / "cores/arduino").iterdir()
                    if p.suffix in (".cpp", ".c", ".S")]
    application_objects, core_objects = [], []
    for index, source in enumerate(sources + core_sources):
        cpp = source.suffix in (".cpp", ".ino")
        compiler = TOOLS / ("avr-g++.exe" if cpp else "avr-gcc.exe")
        language = (["-x", "c++", "-std=gnu++11", "-fno-exceptions", "-fno-threadsafe-statics"]
                    if cpp else (["-std=gnu11"] if source.suffix == ".c" else []))
        obj = directory / (str(index) + ".o")
        run([compiler, *flags, *language, "-c", source, "-o", obj])
        (core_objects if source in core_sources else application_objects).append(obj)
    archive = directory / "core.a"
    run([TOOLS / "avr-ar.exe", "rcs", archive, *core_objects])
    elf = directory / "sketch.elf"
    run([TOOLS / "avr-g++.exe", "-mmcu=" + mcu, "-Wl,--gc-sections",
         "-o", elf, *application_objects, archive, "-lm"])
    print(board + ": compiled and linked successfully")
    print(run([TOOLS / "avr-size.exe", "--format=avr", "--mcu=" + mcu, elf]))
print("Build files: " + str(BUILD))
