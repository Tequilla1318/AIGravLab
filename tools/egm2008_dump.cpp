// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Loads an EGM2008 coefficient file and writes every parsed value back out
// with 17 significant digits, which round-trips a double exactly. Used by
// tools/validate_parser.py to compare the C++ parser against an independent
// parser bit for bit.
//
// Usage: egm2008_dump <coefficient-file> [max-degree] > out.txt
// Load time and record count go to stderr.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "gravlab/egm2008.hpp"

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: " << argv[0] << " <coefficient-file> [max-degree]\n";
    return 2;
  }
  namespace egm = gravlab::egm2008;
  egm::LoadOptions options;
  if (argc == 3) options.max_degree = std::stoi(argv[2]);

  try {
    const auto t0 = std::chrono::steady_clock::now();
    const egm::CoefficientTable table = egm::load_file(argv[1], options);
    const auto t1 = std::chrono::steady_clock::now();

    std::size_t written = 0;
    for (int n = 0; n <= table.max_degree(); ++n) {
      for (int m = 0; m <= n; ++m) {
        if (!table.has(n, m)) continue;
        std::printf("%d %d %.17g %.17g %.17g %.17g\n", n, m, table.C(n, m),
                    table.S(n, m), table.sigma_C(n, m), table.sigma_S(n, m));
        ++written;
      }
    }
    std::cerr << "max degree " << table.max_degree() << ", " << written
              << " records, load "
              << std::chrono::duration<double>(t1 - t0).count() << " s\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
