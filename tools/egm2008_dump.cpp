// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Developer tool, outside the MISRA scope (see docs/misra.md).
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
#include <iostream>
#include <string>

#include "gravlab/egm2008.hpp"

namespace egm = gravlab::egm2008;

namespace {
egm::Storage<2190> g_storage;  // ~79 MB, static duration
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: " << argv[0] << " <coefficient-file> [max-degree]\n";
    return 2;
  }
  egm::LoadOptions options;
  if (argc == 3) options.max_degree = std::stoi(argv[2]);

  egm::CoefficientTable table;
  if (table.bind(g_storage.buffers(), 2190) != egm::Status::ok) return 1;

  const auto t0 = std::chrono::steady_clock::now();
  const egm::LoadResult r = egm::load_file(argv[1], table, options);
  const auto t1 = std::chrono::steady_clock::now();
  if (r.status != egm::Status::ok) {
    std::cerr << "error: " << egm::describe(r.status);
    if (r.line != 0) std::cerr << " (line " << r.line << ")";
    if (r.n >= 0) std::cerr << " (n=" << r.n << " m=" << r.m << ")";
    std::cerr << '\n';
    return 1;
  }

  std::size_t written = 0;
  egm::Coefficient c;
  for (int n = 0; n <= table.max_degree(); ++n) {
    for (int m = 0; m <= n; ++m) {
      if (table.get(n, m, c) != egm::Status::ok) continue;
      std::printf("%d %d %.17g %.17g %.17g %.17g\n", n, m, c.C, c.S, c.sigma_C, c.sigma_S);
      ++written;
    }
  }
  std::cerr << "max degree " << table.max_degree() << ", " << written << " records, load "
            << std::chrono::duration<double>(t1 - t0).count() << " s\n";
  return 0;
}
