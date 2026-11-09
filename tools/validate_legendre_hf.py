#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compare the C++ Holmes & Featherstone engine with pyshtools, value by value.

pyshtools' PlmBar (SHTOOLS, Wieczorek & Meschede 2018) is an independent
implementation of the same normalization: 4-pi, no Condon-Shortley phase,
index l(l+1)/2 + m. For each colatitude this script runs legendre_hf_dump and
compares the full triangle.

Reported per colatitude: max |C++ - pyshtools| / sqrt(2n+1), i.e. the
absolute difference scaled by the bound |Pbar_nm| <= sqrt(2n+1). Relative
differences are not reported: they blow up near the zeros of these oscillating
functions, and in the far tail (values below ~1e-200) pyshtools 4.14 itself
loses about five digits to a subnormal unscaling factor, which this engine
avoids (see tests/test_legendre_holmes_and_featherstone.cpp, TinyValues...).
Within ~0.1 degree of a pole the two also differ by ~1e-9 because pyshtools
rebuilds sin(theta) as sqrt((1-z)(1+z)), which loses precision there; against
exact values this engine is the closer one.

Usage:
    python3 tools/validate_legendre_hf.py build/legendre_hf_dump [degree]
Needs: pip install pyshtools numpy
"""

import subprocess
import sys

import numpy as np
import pyshtools

ANGLES = [0.0, 0.01, 0.5, 1.0, 5.0, 20.0, 45.0, 60.0, 89.0, 90.0, 120.0, 179.0, 180.0]


def main(dump: str, degree: int) -> int:
    n_idx = np.concatenate([np.full(n + 1, n) for n in range(degree + 1)])
    row_bound = np.sqrt(2.0 * n_idx + 1.0)
    worst_abs = 0.0
    print(f"degree {degree}: C++ legendre_hf vs pyshtools {pyshtools.__version__} PlmBar")
    print(f"{'colat':>7} {'max_abs':>10}  timing")
    for th in ANGLES:
        run = subprocess.run([dump, str(degree), repr(th), "5"], capture_output=True, text=True, check=True)
        ours = np.array([float(line.split()[2]) for line in run.stdout.splitlines()])
        ref = pyshtools.legendre.PlmBar(degree, np.cos(np.radians(th)))
        if ours.shape != ref.shape:
            print(f"shape mismatch: {ours.shape} vs {ref.shape}")
            return 1
        abs_err = np.max(np.abs(ours - ref) / row_bound)
        worst_abs = max(worst_abs, abs_err)
        timing = run.stderr.strip().split(": ", 1)[-1]
        print(f"{th:7.2f} {abs_err:10.2e}  {timing}")
    print(f"worst max_abs {worst_abs:.2e}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) == 3 else 2190))
