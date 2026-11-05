# MISRA C++:2023 compliance — EGM2008 parser

## Status: manually reviewed, not tool-certified

No MISRA C++:2023 checker was available (the qualified ones are commercial:
Helix QAC, Parasoft C/C++test, PC-lint Plus, Cppcheck Premium, Polyspace,
Klocwork). This record comes from a rule-by-rule manual review of the code
against the rule headlines, backed by the compiler and free tools listed
below. Treat it as a pre-audit. A qualified checker run is still needed
before claiming compliance formally.

Rule numbers and headlines follow the MISRA C++:2023 list as published in
MathWorks' Polyspace documentation. Rule categories (mandatory, required,
advisory) are given only where the review depended on them.

## Scope

| In scope | Out of scope |
|---|---|
| `include/gravlab/egm2008.hpp`, `src/egm2008.cpp` | `tests/` (GoogleTest is not MISRA compliant), `tools/` (developer utilities) |

## Design decisions behind the compliance

| Decision | Rules served |
|---|---|
| No dynamic memory. The caller provides storage (`Storage<Capacity>` or its own `Buffers`); parsing uses fixed `std::array` buffers; files are read with POSIX `read()` into a fixed 64 KiB buffer. | 21.6.1, 21.6.2, 21.6.3 |
| No exceptions. Every operation returns a `Status` or a `LoadResult`, and every function is `noexcept`. The library is compiled with `-fno-exceptions -fno-rtti`, so the compiler enforces this. | 18.x, 18.4.1, 18.5.1 |
| Numbers are converted with `std::from_chars`, never with `atof`, `strtod`, `<cctype>` or C I/O. | 21.2.1, 21.2.2, 24.5.1, 30.0.1 |
| Fixed-width integer types (`std::int32_t`, `std::uint64_t`, `std::size_t`) throughout. | 6.9.2 |
| Scoped enumeration with an explicit underlying type (`enum class Status : std::uint8_t`). | 10.2.1, 10.2.2 |
| Include guard, no `#pragma`, no function-like macros. | 19.0.2, 19.2.1, 19.6.1 |

## Evidence

| Check | Result |
|---|---|
| GCC 13 and Clang 18 (libstdc++), Clang 20 (libc++ 20); `-std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Wuseless-cast -Wcast-qual -Wswitch-enum -Wundef -Wnull-dereference -Wdouble-promotion -Werror` | clean |
| Library built with `-fno-exceptions -fno-rtti` | builds |
| cppcheck 2.13, `--enable=all --inconclusive` | no findings |
| clang-tidy 18, `bugprone-*`, `cert-*`, `cppcoreguidelines-*`, `hicpp-*`, `misc-*`, selected `readability-*` | C++ Core Guidelines bounds-profile notes (pointer arithmetic, non-constant index), each reviewed under Rule 8.7.1 (N4), plus one vararg note for POSIX `open` (N2) |
| `NoHeap` test: global `operator new` replaced with a counter around `parse_fortran_double`, `parse_line`, `load_text`, `load_file` (success and failure) and a full degree-2190 file load | 0 allocations on libstdc++ (GCC 13, Clang 18) and libc++ 20 (Clang 20) |
| AddressSanitizer + UndefinedBehaviorSanitizer, all 33 tests including the full file | clean |
| All 9,605,332 reals vs Python `float()`, bit for bit, libstdc++ and libc++ builds | 0 mismatches |

## Supported toolchains

| Requirement | Why |
|---|---|
| POSIX (Linux, macOS, BSD) | file reading uses `open`/`read`/`close` (N1) |
| libstdc++ from GCC 12+, or libc++ 20+ | earliest versions with `std::from_chars` for `double`; libc++ 18 does not have it |
| MSVC / Windows | not supported (owner's decision) |

## Deviation records

**D1 — Rule 6.7.2 (required), application storage.** *Decided 2026-11-05.*
The application holds the coefficient table in one namespace-scope
`static gravlab::egm2008::Storage<2190>` object (~79 MB).
- *Why:* the table is too large for the stack, Rule 21.6.1 rules out the heap,
  and its size is fixed when the program is built. A single object with static
  storage duration is the simplest way to meet all three constraints.
- *Risk:* a global is visible to the whole translation unit, and its
  initialization order across translation units is unspecified.
- *Mitigation:* give it internal linkage, in an unnamed namespace in one
  `.cpp`. Reach it only through a `CoefficientTable` bound at start-up, and do
  not touch it during static initialization. `Storage` is zero-initialized
  (`std::array{}`), so it has no dynamic initializer.
- *Scope:* this deviation belongs to the application that uses the library.
  The library code itself declares no globals.

## Notes and potential deviations

These are the points a formal checker or assessor is most likely to raise.

**N1 — Rule 21.6.1 (advisory): why the file is read with POSIX, not
`std::filebuf`.** An earlier version used `std::filebuf`. It made no
allocations on libstdc++, but libc++'s `basic_filebuf` constructor allocates
its buffer before `pubsetbuf()` can replace it (measured: 1 allocation per
`load_file`). There is no other standard C++ file API to fall back on, and
Rule 30.0.1 rules out `<cstdio>`. The reader therefore uses POSIX
`open`/`read`/`close`, which never allocate. Measured: 0 allocations on both
libstdc++ and libc++.

**N2 — POSIX calls (Rules 4.1.1, 5.13.3, 8.2.11, 22.4.1).** The POSIX
functions are `extern "C"` declarations from system headers. The calling code
is still ISO C++17. Points an assessor may raise:
- `open()` is declared variadic. It is called with two arguments and nothing
  in the ellipsis, so Rule 8.2.11 has no argument to apply to. `<cstdarg>` is
  not used (Rule 21.10.1).
- `O_RDONLY` is defined as `00`, an octal literal, in glibc's headers. The
  octal constant comes from the system header's macro, not from this code
  (Rule 5.13.3). It is the only flag passed, so no built-in `|` on `int` is
  needed (Rule 7.0.4).
- `errno` is read only, to retry `read()` after `EINTR`, and never assigned
  (Rule 22.4.1).
- File descriptors are POSIX `int`. They are stored as `std::int32_t`, and a
  `static_assert` checks that the two are the same type (Rule 6.9.2).
- `close()` returns a status that is discarded with a cast to `void`, since a
  failed close on a read-only file needs no action (Rule 0.1.2).

**N3 — Rule 18.5.1, noexcept functions calling the standard library.**
`std::string_view::substr` can throw only when `pos > size()`; every call is
guarded so that it cannot. `std::from_chars` reports failure by return value.
The POSIX calls are C functions and cannot throw. With `-fno-exceptions`, any
throw path left inside the standard library would call `abort()`, which also
touches Rule 18.5.2. None is reachable from validated inputs.

**N4 — Rule 8.7.1, pointer arithmetic.** Every site, and why it stays in bounds:

| Site | Bound |
|---|---|
| `parse_non_negative`: `first + digits.size()` | one past the end of the token |
| `parse_fortran_double`: `first + length` | `length <= token.size() <= kMaxRealLength` |
| `FileSource::next_line`: `buffer_.data() + begin_` | `begin_ <= end_ <= kReadBufferSize` |
| `FileSource::refill`: `buffer_.data() + end_` | after compaction `end_ <= kMaxLineLength < kReadBufferSize` |
| `CoefficientTable::clear`: `buffers_.X + count` | `count = slot_count(capacity) <= length`, checked in `bind()` |
| `CoefficientTable::{has,get,set}`: `buffers_.X[i]` | `i = slot_index(n, m)` only after `in_range(n, m)`, so `i < slot_count(capacity)` |

**N5 — Rules 6.8.1 / 6.8.3, lifetime of caller storage.** `CoefficientTable`
is a non-owning view. The caller must keep the `Storage` or `Buffers`
memory alive as long as the table. `Storage::buffers()` is lvalue
ref-qualified (Rule 6.8.4), so it cannot be called on a temporary.

**N6 — Rule 6.7.2 in the application, not the library.** A full
`Storage<2190>` is about 79 MB and needs static storage duration. The owner
chose a global, recorded as deviation D1 above. Other applications could
instead pass a platform memory region as `Buffers`.

**N7 — Rule 0.1.1 (advisory), defensive initializers.** Some locals are
initialized and then overwritten on every path that reads them, for example
`double parsed{0.0}` before `std::from_chars`. They are kept on purpose to
satisfy Rule 11.6.1.

**N8 — Rule 0.1.2, discarded return values.** The one discarded result,
POSIX `close()` in the reader's destructor, is cast to `void`, which is the
permitted way to discard it. `std::fill` is used instead of `std::fill_n`
because it returns nothing.

**N9 — Rule 7.11.2 at call sites.** Inside the library, string constants are
`""sv` literals, so no array decays. Callers that pass string literals to the
`std::string_view` parameters will see this rule in their own code.

**N10 — Rule 4.1.3, arithmetic overflow.** `slot_count` and `slot_index` stay
exact for degrees up to `kMaxCapacity = 32767`, even with a 32-bit
`std::size_t`. `bind()` rejects anything larger, and `Storage` checks it with
a `static_assert`.

## Rule-by-rule record

C = compliant (reviewed), N/A = the construct does not occur, Note = see above.

| Rule | Status | Remark |
|---|---|---|
| 0.0.1 | C | no unreachable statements |
| 0.0.2 | C | no invariant controlling expressions |
| 0.1.1 | Note N7 | |
| 0.1.2 | C, Note N8 | `[[nodiscard]]` on fallible API functions |
| 0.2.1–0.2.4 | C | no unused variables, parameters, types or functions (`-Wextra` clean) |
| Dir 0.3.1 | C | floating point is only converted, never computed on |
| Dir 0.3.2 | C | `from_chars` ranges valid; `load_file` rejects a null path |
| 4.1.1 | C, Note N2 | ISO C++17, extensions off, `-Wpedantic`; POSIX via `extern "C"` system headers |
| 4.1.2 | C | no deprecated features |
| 4.1.3 | C, Note N10 | sanitizers clean |
| 4.6.1 | C | no unsequenced modifications |
| 5.0.1, 5.7.1, 5.7.3 | C | |
| 5.7.2 | C | no commented-out code |
| 5.10.1 | C | no reserved identifiers |
| 5.13.1–5.13.3, 5.13.5–5.13.7 | C / N/A, Note N2 | no octal in this code (`O_RDONLY` is octal in glibc's own macro), no `l` suffix, no encoding-prefix concatenation |
| 5.13.4 | C | every unsigned literal has a `U` suffix |
| 6.0.1–6.0.4 | C | global namespace holds only `namespace gravlab` |
| 6.2.1–6.2.4 | C | header defines only inline, constexpr and template entities |
| 6.4.1 | C | `-Wshadow` clean |
| 6.4.2, 6.4.3 | N/A | no inheritance |
| 6.5.1 | C | every external function is declared in the header |
| 6.5.2 | C | helpers in an unnamed namespace in the `.cpp` |
| 6.7.1 | C | no local statics |
| 6.7.2 | C (library); deviation D1 (application) | only `constexpr` namespace-scope constants in the library |
| 6.8.1–6.8.3 | C, Note N5 | |
| 6.8.4 | C | `Storage::buffers() &` |
| 6.9.1 | C | |
| 6.9.2 | C, Note N2 | no `int`, `long`, `short` or `unsigned`; descriptors are `std::int32_t` |
| 7.0.1, 7.0.2 | C | no conversions from or to `bool` |
| 7.0.3 | C | characters only compared for equality; digits via `string_view::find` |
| 7.0.4 | C | no bitwise operators |
| 7.0.5, 7.0.6 | C | `-Wconversion -Wsign-conversion` clean; explicit `static_cast` where the type changes |
| 7.11.1 | C | `nullptr` only |
| 7.11.2 | C, Note N9 | `std::array::data()`, no decay |
| 7.11.3 | N/A | |
| 8.0.1 | C | mixed-operator expressions parenthesized |
| 8.1.1, 8.1.2 | N/A | no lambdas |
| 8.2.1–8.2.9 | C / N/A | only `static_cast`; `-Wold-style-cast` clean |
| 8.2.11 | C, Note N2 | no arguments passed through an ellipsis |
| 8.2.10 | C | no recursion |
| 8.3.1, 8.3.2 | C | |
| 8.7.1 | C, Note N4 | |
| 8.7.2, 8.9.1 | C | no pointer subtraction; pointers only compared for equality |
| 8.14.1 | C | right operands of `&&` and `||` have no side effects |
| 8.18.1 | C | the buffer compaction copies element by element |
| 8.18.2, 8.19.1, 8.20.1 | C | |
| 9.2.1 | C | the only expression-statement cast is a cast to `void` (Note N8) |
| 9.3.1 | C | every body is a compound statement |
| 9.4.1 | C | every `else if` chain ends in `else` |
| 9.4.2 | N/A | no `switch` |
| 9.5.1 | C | every `for` declares its counter in the init-statement and does not modify it in the body |
| 9.5.2 | N/A | no range-for |
| 9.6.1–9.6.4 | N/A | no `goto`, no `[[noreturn]]` |
| 9.6.5 | C | |
| 10.0.1 | C | one declarator per declaration |
| 10.1.1 | C | non-const reference parameters are all written to |
| 10.1.2 | N/A | no `volatile` |
| 10.2.1–10.2.3 | C | |
| 10.3.1 | C | no unnamed namespace in the header |
| 10.4.1 | N/A | |
| 11.3.1 | C | no C-style arrays |
| 11.3.2 | C | at most one level of indirection |
| 11.6.1–11.6.3 | C | default member initializers everywhere; only the first enumerator has an explicit value |
| 12.2.x, 12.3.1 | N/A | no bit-fields, no unions |
| 13.1.x, 13.3.x | N/A | no inheritance, no virtual functions |
| 13.3.3 | C | parameter names match between declarations and definitions |
| 14.1.1 | C | each class's data members are either all public or all private |
| 15.0.1 | C | rule of zero, except `FileSource`, which declares all five special members |
| 15.0.2, Dir 15.8.1 | N/A | no user-provided copy or move |
| 15.1.1–15.1.5 | C | single-argument constructor `explicit`; all members initialized |
| 16.x, 17.8.1 | N/A | no operator overloading, no explicit specialization |
| 18.1.x, 18.3.x | N/A | no `throw` or `catch` (`-fno-exceptions`) |
| 18.4.1 | C | every function is `noexcept` |
| 18.5.1, 18.5.2 | C, Note N3 | |
| 19.0.1–19.0.4, 19.1.x | C | |
| 19.2.1–19.2.3 | C | include guard `GRAVLAB_EGM2008_HPP` |
| 19.3.x | N/A | no macros besides the guard |
| 19.6.1 | C | no `#pragma` |
| 21.2.1–21.2.4 | C | none of `<cstdlib>`, `<cstring>`, `offsetof` |
| 21.6.1 | C, Note N1 | |
| 21.6.2–21.6.5 | N/A | no `new`, `delete` or smart pointers |
| 21.10.x | N/A | |
| 22.3.1 | N/A | no `assert` |
| 22.4.1 | C, Note N2 | `errno` read, never assigned |
| 23.11.1 | N/A | |
| 24.5.1, 24.5.2 | C | |
| 25.5.x | N/A | no locale or `getenv` calls |
| 26.3.1 | C | `std::array<bool, N>`, not `std::vector<bool>` |
| 28.3.1, 28.6.x | N/A | |
| 30.0.1 | C, Note N1 | POSIX `read()`; no `<cstdio>` |
| 30.0.2 | C | the file is only read |
