# Context

## Current state (2026-11-05)

- `dev/TaskOne` holds the NGA EGM2008 tide-free coefficient file
  (`externals/egm2008/EGM2008_to2190_TideFree.txt`, 242 MB, Git LFS).
- Task one: a C++ parser that converts those coefficients to `double`.
  Implemented in `include/gravlab/egm2008.hpp` and `src/egm2008.cpp`, with
  tests in `tests/test_egm2008.cpp` and validation tools in `tools/`.

## Validation results

- 22 unit tests plus 2 full-file tests pass (GCC 13, warnings as errors), and
  also pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- Full file: 2,401,333 records (every n = 2..2190, m = 0..n), loaded in about
  0.7–0.9 s on the cloud VM.
- All 9,605,332 reals match Python's `float()` bit for bit.
- C22 and S22 agree with the EGM2008 values quoted in IERS Conventions (2010),
  Chapter 6, to all eight published digits.

## Known gaps

- `.gitattributes` is empty, so Git does not know the coefficient file is
  tracked by LFS. Left alone for now at the owner's request.
- `dev/TaskOne` has no common history with `main`.
- CI does not pull LFS, so the full-file tests skip there.
