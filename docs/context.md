# Context

## Current state (2026-11-05)

- `dev/TaskOne` holds the NGA EGM2008 tide-free coefficient file
  (`externals/egm2008/EGM2008_to2190_TideFree.txt`, 242 MB, Git LFS).
- Task one: a C++ parser that converts those coefficients to `double`.
  - PR #1 (`dev/TaskOne-egm2008-parser`): first version, C++17 with exceptions
    and `std::vector` storage.
  - Stacked PR (`dev/TaskOne-misra`): reworked to MISRA C++:2023. No dynamic
    memory (caller-provided `Storage<Capacity>`/`Buffers`), status codes
    instead of exceptions, library built with `-fno-exceptions -fno-rtti`.
    Scope: `include/` and `src/` only. Review record in `docs/misra.md`.

## Validation results (MISRA version)

- 33 tests pass with GCC 13 and Clang 18 (warnings as errors), and under
  AddressSanitizer + UndefinedBehaviorSanitizer.
- Zero heap allocations during parsing, including a full degree-2190 file
  load, measured with a counting `operator new` (libstdc++).
- Full file: 2,401,333 records, loaded in about 0.65 s on the cloud VM.
- All 9,605,332 reals match Python `float()` bit for bit.
- C22 and S22 agree with the EGM2008 values in IERS Conventions (2010),
  Chapter 6.
- cppcheck: no findings. clang-tidy: only C++ Core Guidelines bounds notes,
  each reviewed under MISRA Rule 8.7.1.

## Known gaps

- MISRA compliance is manually reviewed, not checked by a qualified tool.
- The zero-allocation property of `std::filebuf` is verified only on
  libstdc++ (docs/misra.md, N1).
- `.gitattributes` is empty, so Git does not know the coefficient file is
  tracked by LFS. Left alone at the owner's request.
- `dev/TaskOne` has no common history with `main`.
- CI does not pull LFS, so the full-file tests skip there.
