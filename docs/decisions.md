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
