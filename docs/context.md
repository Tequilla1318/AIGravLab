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

- 33 tests pass with GCC 13 and Clang 18 (libstdc++), Clang 20 (libc++ 20),
  warnings as errors, and under AddressSanitizer + UndefinedBehaviorSanitizer.
- Zero heap allocations during parsing, including a full degree-2190 file
  load, on both libstdc++ and libc++ (counting `operator new`).
- Files are read with POSIX `open`/`read`; `std::filebuf` allocates on libc++.
- Full file: 2,401,333 records, loaded in about 0.65 s (libstdc++) and 1.0 s
  (libc++) on the cloud VM.
- All 9,605,332 reals match Python `float()` bit for bit on both libraries.
- Owner decisions: storage is a global (MISRA deviation D1, Rule 6.7.2);
  libstdc++ + libc++ only, no MSVC; no qualified checker yet; limits kept.
- C22 and S22 agree with the EGM2008 values in IERS Conventions (2010),
  Chapter 6.
- cppcheck: no findings. clang-tidy: only C++ Core Guidelines bounds notes,
  each reviewed under MISRA Rule 8.7.1.

## Known gaps

- MISRA compliance is manually reviewed, not checked by a qualified tool.
- POSIX only (no Windows); libc++ must be 20 or newer.
- `.gitattributes` is empty, so Git does not know the coefficient file is
  tracked by LFS. Left alone at the owner's request.
- `dev/TaskOne` has no common history with `main`.
- CI does not pull LFS, so the full-file tests skip there.
