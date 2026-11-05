#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Cross-check the C++ EGM2008 parser against Python, value by value.

Python's float() is an independent, correctly rounded decimal-to-binary64
conversion (CPython uses David Gay's dtoa). For every record in the source
file, this script parses the four reals with float() and compares them bit for
bit with what egm2008_dump printed (17 significant digits round-trip a double
exactly, so the dump itself loses nothing).

Usage:
    egm2008_dump EGM2008_to2190_TideFree.txt > dump.txt
    python3 tools/validate_parser.py EGM2008_to2190_TideFree.txt dump.txt
"""

import struct
import sys


def bits(x: float) -> bytes:
    return struct.pack("<d", x)


def main(source_path: str, dump_path: str) -> int:
    expected = {}
    with open(source_path) as f:
        for line in f:
            fields = line.split()
            if not fields:
                continue
            n, m = int(fields[0]), int(fields[1])
            values = tuple(float(t.replace("D", "E").replace("d", "e")) for t in fields[2:6])
            if (n, m) in expected:
                print(f"duplicate in source: n={n} m={m}")
                return 1
            expected[(n, m)] = values

    seen = 0
    mismatches = 0
    with open(dump_path) as f:
        for line in f:
            fields = line.split()
            n, m = int(fields[0]), int(fields[1])
            got = tuple(float(t) for t in fields[2:6])
            want = expected.get((n, m))
            if want is None:
                print(f"dump has n={n} m={m}, source does not")
                mismatches += 1
                continue
            for name, g, w in zip(("C", "S", "sigma_C", "sigma_S"), got, want):
                if bits(g) != bits(w):
                    mismatches += 1
                    if mismatches <= 10:
                        print(f"mismatch n={n} m={m} {name}: C++ {g!r}  Python {w!r}")
            seen += 1

    missing = len(expected) - seen
    print(f"records in source: {len(expected)}")
    print(f"records compared:  {seen}")
    print(f"reals compared:    {4 * seen}")
    print(f"missing from dump: {missing}")
    print(f"bit mismatches:    {mismatches}")
    return 0 if mismatches == 0 and missing == 0 else 1


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
