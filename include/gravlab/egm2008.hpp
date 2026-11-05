// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parser for the NGA EGM2008 spherical-harmonic coefficient file
// (EGM2008_to2190_TideFree.txt and files in the same format).
//
// File format: one record per line, whitespace separated, six fields:
//
//     n  m  C_nm  S_nm  sigma_C_nm  sigma_S_nm
//
// n, m are integers (degree, order, 0 <= m <= n). The four reals are fully
// normalized coefficients and their standard deviations, written in Fortran
// notation with a 'D' exponent marker, e.g. -0.484165143790815D-03.
// Every real is converted to an IEEE-754 binary64 value (C++ double), rounded
// correctly to nearest.
//
// The NGA file starts at degree 2; degrees 0 and 1 are not stored in it. This
// parser does not invent them: has(0, 0), has(1, 0) and has(1, 1) are false.
#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gravlab::egm2008 {

// Thrown for malformed input. line() is the 1-based line number, or 0 when the
// error is not tied to a single line (e.g. a missing coefficient).
class ParseError : public std::runtime_error {
 public:
  ParseError(const std::string& message, std::size_t line);
  std::size_t line() const noexcept { return line_; }

 private:
  std::size_t line_;
};

// One record of the coefficient file.
struct Coefficient {
  int n = 0;
  int m = 0;
  double C = 0.0;
  double S = 0.0;
  double sigma_C = 0.0;
  double sigma_S = 0.0;
};

// Converts one Fortran-style real token to double. Accepts 'D', 'd', 'E' or
// 'e' as the exponent marker, an optional leading sign, and an optional
// exponent. The whole token must be consumed. Throws std::invalid_argument on
// malformed input and std::out_of_range if the value does not fit a double.
double parse_fortran_double(std::string_view token);

// Parses one line. Returns std::nullopt for an empty or whitespace-only line.
// Throws ParseError (with line_number) for anything else that is not a valid
// six-field record with 0 <= m <= n.
std::optional<Coefficient> parse_line(std::string_view line,
                                      std::size_t line_number = 0);

// Dense lower-triangular storage of coefficients for degrees 0..max_degree.
// Entry (n, m) lives at index(n, m) = n(n+1)/2 + m in each array, so the raw
// arrays can be handed straight to a gravity-field evaluator.
class CoefficientTable {
 public:
  CoefficientTable() = default;
  explicit CoefficientTable(int max_degree);

  int max_degree() const noexcept { return max_degree_; }

  static constexpr std::size_t index(int n, int m) noexcept {
    return static_cast<std::size_t>(n) * (static_cast<std::size_t>(n) + 1) / 2 +
           static_cast<std::size_t>(m);
  }

  // Number of (n, m) slots, i.e. index(max_degree, max_degree) + 1.
  std::size_t size() const noexcept { return C_.size(); }

  // True if (n, m) is in range and was set from the file.
  bool has(int n, int m) const noexcept;

  // Accessors throw std::out_of_range if (n, m) is outside 0 <= m <= n <=
  // max_degree. A slot that was never set reads as 0.0; use has() to tell.
  double C(int n, int m) const;
  double S(int n, int m) const;
  double sigma_C(int n, int m) const;
  double sigma_S(int n, int m) const;

  // Stores a record. Throws std::out_of_range if c.n > max_degree.
  // Returns false (and changes nothing) if the slot was already set.
  bool set(const Coefficient& c);

  // Grows the table to max_degree, keeping existing entries. No-op if the
  // table is already that large.
  void extend_to(int max_degree);

  // Raw triangular arrays, length size().
  const std::vector<double>& C_data() const noexcept { return C_; }
  const std::vector<double>& S_data() const noexcept { return S_; }
  const std::vector<double>& sigma_C_data() const noexcept { return sigma_C_; }
  const std::vector<double>& sigma_S_data() const noexcept { return sigma_S_; }

 private:
  std::size_t checked_index(int n, int m) const;

  int max_degree_ = -1;
  std::vector<double> C_;
  std::vector<double> S_;
  std::vector<double> sigma_C_;
  std::vector<double> sigma_S_;
  std::vector<unsigned char> present_;
};

struct LoadOptions {
  // Keep degrees up to this value. -1 keeps every degree in the input.
  // When set, reading stops at the first record with n > max_degree, which is
  // correct for the NGA file because it is sorted by degree. If an unsorted
  // file drops rows because of this, the completeness check below reports it.
  int max_degree = -1;

  // Require every (n, m) with min_degree <= n <= max degree read and
  // 0 <= m <= n to appear exactly once. Duplicates are always an error.
  bool require_complete = true;

  // Lowest degree the completeness check expects. The NGA file starts at 2.
  int min_degree = 2;
};

// Reads a coefficient file. Throws ParseError on malformed or incomplete
// input, and if no records are found.
CoefficientTable load(std::istream& in, const LoadOptions& options = {});

// Opens and reads a file. Throws std::runtime_error if it cannot be opened,
// and ParseError if it is a Git LFS pointer instead of the real data.
CoefficientTable load_file(const std::filesystem::path& path,
                           const LoadOptions& options = {});

}  // namespace gravlab::egm2008
