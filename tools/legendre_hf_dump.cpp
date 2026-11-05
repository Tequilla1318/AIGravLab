// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Developer tool, outside the MISRA scope (see docs/misra.md).
//
// Evaluates the Holmes & Featherstone Legendre engine at one colatitude and
// prints every Pbar_nm with 17 significant digits, which round-trips a double
// exactly. Used by tools/validate_legendre_hf.py to compare against pyshtools.
//
// Usage: legendre_hf_dump <degree> <colatitude-degrees> [repeats] > out.txt
// The fastest time of `repeats` evaluations (default 20) goes to stderr.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#include "gravlab/legendre_holmes_and_featherstone.hpp"

namespace hf = gravlab::legendre_hf;

namespace {
hf::RecursionStorage<hf::kMaxDegree> g_recursion;  // ~58 MB
hf::ValueStorage<hf::kMaxDegree> g_values;         // ~29 MB
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "usage: " << argv[0] << " <degree> <colatitude-degrees> [repeats]\n";
    return 2;
  }
  const int degree = std::stoi(argv[1]);
  const double theta = std::stod(argv[2]) * 3.141592653589793 / 180.0;
  const int repeats = (argc == 4) ? std::stoi(argv[3]) : 20;

  hf::Recursion recursion;
  const auto i0 = std::chrono::steady_clock::now();
  hf::Status s = recursion.init(g_recursion.buffers(), degree);
  const auto i1 = std::chrono::steady_clock::now();
  if (s != hf::Status::ok) {
    std::cerr << "init: " << hf::describe(s) << '\n';
    return 1;
  }

  double best = 1e300;
  for (int r = 0; r < std::max(1, repeats); ++r) {
    const auto t0 = std::chrono::steady_clock::now();
    s = hf::evaluate(recursion, theta, degree, g_values.data(), g_values.size());
    const auto t1 = std::chrono::steady_clock::now();
    if (s != hf::Status::ok) {
      std::cerr << "evaluate: " << hf::describe(s) << '\n';
      return 1;
    }
    best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
  }

  for (int n = 0; n <= degree; ++n) {
    for (int m = 0; m <= n; ++m) {
      std::printf("%d %d %.17g\n", n, m, g_values.data()[gravlab::slot_index(n, m)]);
    }
  }
  std::cerr << "degree " << degree << ": init "
            << std::chrono::duration<double>(i1 - i0).count() * 1e3 << " ms, evaluate "
            << best * 1e3 << " ms (best of " << repeats << ")\n";
  return 0;
}
