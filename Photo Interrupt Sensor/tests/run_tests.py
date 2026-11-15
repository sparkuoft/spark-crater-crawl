"""Build the tracking sources as C and run host tests plus a C++ link check."""

import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc', help='C compiler executable')
    parser.add_argument('--cxx', default='g++', help='C++ compiler executable')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    sensor = tests.parent
    output = sensor.parent / 'build' / 'tracking-tests'
    output.mkdir(parents=True, exist_ok=True)
    warnings = ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow', '-Werror']
    objects = []
    for name in ('CharacterModule', 'CharacterController'):
        obj = output / (name + '.o')
        subprocess.run([args.cc, '-std=c11', *warnings, '-I', str(sensor),
                        '-c', str(sensor / (name + '.c')), '-o', str(obj)], check=True)
        objects.append(str(obj))
    suffix = '.exe' if os.name == 'nt' else ''
    for source, compiler, standard in (
        ('test_tracking.c', args.cc, '-std=c11'),
        ('cpp_link_smoke.cpp', args.cxx, '-std=c++11'),
    ):
        executable = output / (Path(source).stem + suffix)
        subprocess.run([compiler, standard, *warnings, '-I', str(sensor),
                        str(tests / source), *objects, '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print('PASS C++ headers and linkage against separately compiled C objects')


if __name__ == '__main__':
    main()
