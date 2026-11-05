# GravLab

## EGM2008 coefficient parser

`gravlab::egm2008` reads the NGA EGM2008 coefficient file
(`externals/egm2008/EGM2008_to2190_TideFree.txt`) and converts every value to
`double` (IEEE-754 binary64), correctly rounded.

Each line of the file is `n m C S sigma_C sigma_S`, with the reals written in
Fortran notation (`-0.484165143790815D-03`). The parser accepts `D`, `d`, `E`
or `e` as the exponent marker.

```cpp
#include "gravlab/egm2008.hpp"

namespace egm = gravlab::egm2008;

egm::LoadOptions options;
options.max_degree = 360;            // or -1 (default) for the full 2190
egm::CoefficientTable t = egm::load_file(
    "externals/egm2008/EGM2008_to2190_TideFree.txt", options);

double c20 = t.C(2, 0);              // -4.84165143790815e-04
double s22 = t.S(2, 2);
const std::vector<double>& C = t.C_data();   // triangular, index(n, m) = n(n+1)/2 + m
```

What it checks:

- every line has six fields and `0 <= m <= n`;
- every real is a well-formed number that fits a `double`;
- no `(n, m)` appears twice;
- by default, every `(n, m)` from degree 2 up to the highest degree read is
  present (turn off with `options.require_complete = false`).

Errors are thrown as `egm::ParseError` with the line number. A checkout that
only has the Git LFS pointer gets a clear "run `git lfs pull`" error.

The file has no degree 0 or 1 rows, so `t.has(0, 0)` is `false`; the parser does
not fill them in.

### Build and test

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

GoogleTest is taken from the system if installed (Ubuntu: `libgtest-dev`),
otherwise fetched from GitHub.

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
