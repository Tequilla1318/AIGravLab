# GravLab

## EGM2008 coefficient parser

`gravlab::egm2008` reads the NGA EGM2008 coefficient file
(`externals/egm2008/EGM2008_to2190_TideFree.txt`) and converts every value to
`double` (IEEE-754 binary64), correctly rounded.

Each line of the file is `n m C S sigma_C sigma_S`, with the reals written in
Fortran notation (`-0.484165143790815D-03`). The parser accepts `D`, `d`, `E`
or `e` as the exponent marker.

The library is written to MISRA C++:2023 (manually reviewed; see
[docs/misra.md](docs/misra.md)):

- **no dynamic memory**: you provide the storage, and parsing uses fixed-size
  buffers;
- **no exceptions**: every call returns a `Status` and is `noexcept`;
- C++17, fixed-width integer types.

```cpp
#include "gravlab/egm2008.hpp"

namespace egm = gravlab::egm2008;

// ~79 MB for the full model: give it static storage duration.
static egm::Storage<2190> storage;

egm::CoefficientTable table;
if (table.bind(storage.buffers(), 2190) != egm::Status::ok) { /* ... */ }

egm::LoadOptions options;            // options.max_degree = 360 to truncate
const egm::LoadResult r = egm::load_file(
    "externals/egm2008/EGM2008_to2190_TideFree.txt", table, options);
if (r.status != egm::Status::ok) {
  // egm::describe(r.status), r.line, and r.n / r.m say what went wrong
}

egm::Coefficient c;
if (table.get(2, 0, c) == egm::Status::ok) {
  // c.C == -4.84165143790815e-04
}
const double* C = table.C_data();    // triangular, slot_index(n, m) = n(n+1)/2 + m
```

`load_text()` does the same for text already in memory.

What it checks:

- every line has six fields and `0 <= m <= n`;
- every real is a well-formed number that fits a `double`;
- no `(n, m)` appears twice;
- by default, every `(n, m)` from degree 2 up to the highest degree read is
  present (turn off with `options.require_complete = false`);
- lines are at most 256 characters.

A checkout that only has the Git LFS pointer gets `Status::lfs_pointer`
("run `git lfs pull`").

The file has no degree 0 or 1 rows, so `table.has(0, 0)` is `false`; the parser
does not fill them in.

### Build and test

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

GoogleTest is taken from the system if installed (Ubuntu: `libgtest-dev`),
otherwise fetched from GitHub. The library itself is built with
`-fno-exceptions -fno-rtti`.

The two `FullEgm2008File` tests need the real data. They skip when only the LFS
pointer is present. To run them, either `git lfs pull` or point `EGM2008_FILE`
at a copy of the file.

### Bit-for-bit validation

`tools/validate_parser.py` compares every parsed value with Python's
independent, correctly rounded `float()`:

```sh
./build/egm2008_dump externals/egm2008/EGM2008_to2190_TideFree.txt > dump.txt
python3 tools/validate_parser.py externals/egm2008/EGM2008_to2190_TideFree.txt dump.txt
```
