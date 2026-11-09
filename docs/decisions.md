# Decisions

## 2026-11-05 — EGM2008 parser

**Number conversion: `std::from_chars` after rewriting `D` to `e`.**
`from_chars` is locale-independent, correctly rounded, and fast (GCC 12+ and
recent Clang/MSVC). `strtod` would depend on the C locale's decimal point.
Requires C++17.

**Storage: dense triangular arrays, `index(n, m) = n(n+1)/2 + m`.**
The layout does not depend on the maximum degree, so the table can grow while
reading without moving entries, and the raw arrays can go straight into a
gravity evaluator. Full degree 2190 is 2,401,336 slots, about 77 MB for the
four arrays.

**Degrees 0 and 1 are not synthesized.** The file does not contain them. A
future gravity evaluator decides whether C00 = 1 and degree-1 terms are zero.

**Truncated loads stop reading at the first row with n > max_degree.**
Correct because the NGA file is sorted by degree. The completeness check
catches it if a differently ordered file ever loses rows this way.

**Strict by default.** Malformed lines, duplicates and missing (n, m) are
errors, so a corrupted or partial file cannot load silently.

**GoogleTest from the system first, fetched otherwise.** The cloud sessions
cannot download from GitHub outside this repository, but can install
`libgtest-dev`; CI does the same.

**Validation reference: correctly rounded parsers.** Every value is compared
bit for bit with Python `float()` (full file) and glibc `strtod` (unit tests).
The IERS-published C22 and S22 values give an independent check that the right
file and columns are being read.

## 2026-11-05 — MISRA C++:2023 (owner's choices)

The owner chose all four of these:

- **Verification: manual review.** No qualified checker is available, so
  compliance is a documented rule-by-rule review (`docs/misra.md`) backed by
  strict compiler warnings, cppcheck, clang-tidy, sanitizers, and a
  zero-allocation test. It is a pre-audit, not a certification.
- **Scope: library only** (`include/`, `src/`). Tests and tools are excluded;
  GoogleTest is not MISRA compliant.
- **No heap in parsing.** Storage is provided by the caller, either
  `Storage<Capacity>` with static duration or its own `Buffers`.
  `CoefficientTable` became a non-owning view with a fixed capacity, which
  replaces the grow-while-reading design above.
- **Error codes, not exceptions.** `Status` / `LoadResult` returns; every
  function `noexcept`; the library is built with `-fno-exceptions -fno-rtti`
  so the compiler enforces it.

Follow-on decisions:

- **`max_degree()` is the highest degree stored**, not the requested limit.
  This avoids a public setter that only the loaders would use.
- **`kMaxCapacity = 32767`.** It keeps the slot arithmetic exact even with a
  32-bit `std::size_t`, and is far above any published gravity model.
- **Files are read into a fixed 64 KiB buffer** and lines are split in
  place. Lines are limited to 256 characters. (The reader was first built on
  `std::filebuf` and later changed to POSIX; see below.)
- **No `#pragma once`.** It is replaced by an include guard (Rule 19.6.1).

## 2026-11-05 — MISRA follow-up decisions (owner)

- **Application storage: a global, deviating from Rule 6.7.2.** Recorded as
  D1 in `docs/misra.md`. Use one `static Storage<2190>` with internal
  linkage.
- **Toolchains: libstdc++ and libc++; no MSVC.** CI gains a Clang 20 + libc++
  20 job that runs the whole suite, including the zero-allocation test.
- **No qualified MISRA checker for now.** Revisit before any formal audit.
- **Limits stay** at 256-character lines and `kMaxCapacity = 32767`.

Consequences found while adding libc++:

- **The file reader moved from `std::filebuf` to POSIX `open`/`read`/`close`.**
  libc++'s `basic_filebuf` allocates its buffer in its constructor (measured:
  1 allocation per load), so no standard C++ file API is heap-free on both
  libraries, and `<cstdio>` is ruled out by Rule 30.0.1. POSIX is fine
  because Windows is out of scope.
- **The minimum libc++ is 20.** libc++ 18 has no `std::from_chars` for
  `double`.
- **CMake option `GRAVLAB_GTEST_SOURCE_DIR`** builds GoogleTest from source,
  which a libc++ build needs.

## 2026-11-05 — Legendre engine (Holmes & Featherstone)

- **Scope of the first version: P̄ₙₘ values only.** The whole triangle is
  computed for one θ and stored, matching the owner's plan to precompute
  aₙₘ and bₙₘ once and then compute and store P̄ₙₘ per θ. Derivatives and the
  gravity summation come next.
- **Files and names follow the owner's branch:**
  `legendre_holmes_and_featherstone.{hpp,cpp}`, namespace
  `gravlab::legendre_hf`.
- **Its own small `Status` enum** rather than the parser's, so the module
  doesn't depend on the parser.
- **Shared `triangular.hpp`** holds `slot_index`, `slot_count` and
  `kMaxCapacity`, re-exported by `egm2008.hpp` (MISRA Rule 6.2.3).
- **Power-of-two scale, 2⁻⁹³⁰**, instead of the paper's 10⁻²⁸⁰, so scaling is
  exact.
- **Coefficients rounded the SHTOOLS way** (a square root per integer factor)
  after measuring a 15× accuracy difference at the pole against
  single-square-root ratios.
- **The unscaling factor is renormalized** to stay normal, so values down to
  the double range keep full precision. This is an improvement over the paper
  and over pyshtools 4.14, which lose about five digits near 10⁻²⁰⁰.
- **`kMaxDegree = 2700`**, the paper's stated range for the scaled recursion.
- **Test tolerances come from measurements.** Near the poles they allow for
  the conditioning of rounding cos θ to double (about n²·ε), which was
  verified against exact values.
- **Test support:** the counting `operator new` moved to
  `tests/support/allocation_counter.cpp` and is shared by both test suites.
- **The repository was renamed** from GravLab to AIGravLab by the owner;
  work continues on the renamed repo.
