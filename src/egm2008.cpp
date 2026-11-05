// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gravlab/egm2008.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <istream>
#include <string>
#include <system_error>

namespace gravlab::egm2008 {
namespace {

constexpr bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
         c == '\v';
}

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

std::string at_line(std::size_t line) {
  return line == 0 ? std::string{} : " (line " + std::to_string(line) + ")";
}

// Splits on whitespace into at most N tokens; returns how many were found,
// or N + 1 if there were more than N.
template <std::size_t N>
std::size_t split(std::string_view s, std::array<std::string_view, N>& out) {
  std::size_t count = 0;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && is_space(s[i])) ++i;
    if (i == s.size()) break;
    const std::size_t start = i;
    while (i < s.size() && !is_space(s[i])) ++i;
    if (count == N) return N + 1;
    out[count++] = s.substr(start, i - start);
  }
  return count;
}

int parse_int(std::string_view token, const char* what, std::size_t line) {
  int value = 0;
  const char* first = token.data();
  const char* last = token.data() + token.size();
  if (first != last && *first == '+') ++first;  // from_chars rejects '+'
  const auto [ptr, ec] = std::from_chars(first, last, value);
  if (ec != std::errc{} || ptr != last || first == last) {
    throw ParseError("invalid " + std::string(what) + " '" +
                         std::string(token) + "'" + at_line(line),
                     line);
  }
  return value;
}

}  // namespace

ParseError::ParseError(const std::string& message, std::size_t line)
    : std::runtime_error(message), line_(line) {}

double parse_fortran_double(std::string_view token) {
  // Longest real in the NGA file is 23 characters; leave generous room.
  constexpr std::size_t kMaxLen = 64;
  if (token.empty() || token.size() > kMaxLen) {
    throw std::invalid_argument("invalid real '" + std::string(token) + "'");
  }

  std::array<char, kMaxLen> buf{};
  std::size_t len = 0;
  std::size_t i = 0;

  // Optional sign. from_chars accepts '-' but not '+'.
  if (token[0] == '+' || token[0] == '-') {
    if (token[0] == '-') buf[len++] = '-';
    i = 1;
  }
  // Mantissa must start with a digit or '.', which rules out "inf", "nan",
  // a second sign, and an empty mantissa.
  if (i == token.size() || !(is_digit(token[i]) || token[i] == '.')) {
    throw std::invalid_argument("invalid real '" + std::string(token) + "'");
  }
  for (; i < token.size(); ++i) {
    const char c = token[i];
    buf[len++] = (c == 'D' || c == 'd') ? 'e' : c;
  }

  double value = 0.0;
  const char* first = buf.data();
  const char* last = buf.data() + len;
  const auto [ptr, ec] =
      std::from_chars(first, last, value, std::chars_format::general);
  if (ec == std::errc::result_out_of_range) {
    throw std::out_of_range("real out of double range '" + std::string(token) +
                            "'");
  }
  if (ec != std::errc{} || ptr != last) {
    throw std::invalid_argument("invalid real '" + std::string(token) + "'");
  }
  return value;
}

std::optional<Coefficient> parse_line(std::string_view line,
                                      std::size_t line_number) {
  std::array<std::string_view, 6> f{};
  const std::size_t count = split(line, f);
  if (count == 0) return std::nullopt;
  if (count != 6) {
    throw ParseError("expected 6 fields (n m C S sigma_C sigma_S), found " +
                         (count > 6 ? std::string("more than 6")
                                    : std::to_string(count)) +
                         at_line(line_number),
                     line_number);
  }

  Coefficient c;
  c.n = parse_int(f[0], "degree", line_number);
  c.m = parse_int(f[1], "order", line_number);
  if (c.n < 0 || c.m < 0 || c.m > c.n) {
    throw ParseError("need 0 <= m <= n, got n=" + std::to_string(c.n) +
                         " m=" + std::to_string(c.m) + at_line(line_number),
                     line_number);
  }

  double* targets[4] = {&c.C, &c.S, &c.sigma_C, &c.sigma_S};
  static constexpr const char* names[4] = {"C", "S", "sigma_C", "sigma_S"};
  for (std::size_t k = 0; k < 4; ++k) {
    try {
      *targets[k] = parse_fortran_double(f[2 + k]);
    } catch (const std::exception& e) {
      throw ParseError(std::string(names[k]) + ": " + e.what() +
                           at_line(line_number),
                       line_number);
    }
  }
  return c;
}

// ---------------------------------------------------------------------------
// CoefficientTable

CoefficientTable::CoefficientTable(int max_degree) {
  if (max_degree < 0) {
    throw std::invalid_argument("max_degree must be >= 0");
  }
  max_degree_ = max_degree;
  const std::size_t n = index(max_degree, max_degree) + 1;
  C_.assign(n, 0.0);
  S_.assign(n, 0.0);
  sigma_C_.assign(n, 0.0);
  sigma_S_.assign(n, 0.0);
  present_.assign(n, 0);
}

bool CoefficientTable::has(int n, int m) const noexcept {
  if (n < 0 || m < 0 || m > n || n > max_degree_) return false;
  return present_[index(n, m)] != 0;
}

std::size_t CoefficientTable::checked_index(int n, int m) const {
  if (n < 0 || m < 0 || m > n || n > max_degree_) {
    throw std::out_of_range("coefficient (" + std::to_string(n) + ", " +
                            std::to_string(m) + ") outside table of degree " +
                            std::to_string(max_degree_));
  }
  return index(n, m);
}

double CoefficientTable::C(int n, int m) const { return C_[checked_index(n, m)]; }
double CoefficientTable::S(int n, int m) const { return S_[checked_index(n, m)]; }
double CoefficientTable::sigma_C(int n, int m) const {
  return sigma_C_[checked_index(n, m)];
}
double CoefficientTable::sigma_S(int n, int m) const {
  return sigma_S_[checked_index(n, m)];
}

bool CoefficientTable::set(const Coefficient& c) {
  const std::size_t i = checked_index(c.n, c.m);
  if (present_[i]) return false;
  C_[i] = c.C;
  S_[i] = c.S;
  sigma_C_[i] = c.sigma_C;
  sigma_S_[i] = c.sigma_S;
  present_[i] = 1;
  return true;
}

void CoefficientTable::extend_to(int max_degree) {
  if (max_degree <= max_degree_) return;
  max_degree_ = max_degree;
  // The triangular layout does not depend on max_degree, so growing is a
  // plain resize: existing entries keep their index.
  const std::size_t n = index(max_degree, max_degree) + 1;
  C_.resize(n, 0.0);
  S_.resize(n, 0.0);
  sigma_C_.resize(n, 0.0);
  sigma_S_.resize(n, 0.0);
  present_.resize(n, 0);
}

// ---------------------------------------------------------------------------
// Loading

CoefficientTable load(std::istream& in, const LoadOptions& options) {
  if (options.max_degree < -1) {
    throw std::invalid_argument("LoadOptions::max_degree must be >= -1");
  }

  CoefficientTable table;
  if (options.max_degree >= 0) table = CoefficientTable(options.max_degree);

  std::string line;
  std::size_t line_number = 0;
  std::size_t records = 0;
  int highest = -1;

  while (std::getline(in, line)) {
    ++line_number;
    const std::optional<Coefficient> c = parse_line(line, line_number);
    if (!c) continue;
    if (options.max_degree >= 0 && c->n > options.max_degree) break;
    if (c->n > table.max_degree()) table.extend_to(c->n);
    if (!table.set(*c)) {
      throw ParseError("duplicate coefficient n=" + std::to_string(c->n) +
                           " m=" + std::to_string(c->m) + at_line(line_number),
                       line_number);
    }
    if (c->n > highest) highest = c->n;
    ++records;
  }
  if (in.bad()) throw std::runtime_error("I/O error while reading coefficients");
  if (records == 0) throw ParseError("no coefficient records found", 0);

  // Trim to the highest degree actually read when no limit was requested.
  // With an explicit limit, keep the requested size and let the completeness
  // check report anything missing.
  const int last = options.max_degree >= 0 ? options.max_degree : highest;

  if (options.require_complete) {
    for (int n = options.min_degree; n <= last; ++n) {
      for (int m = 0; m <= n; ++m) {
        if (!table.has(n, m)) {
          throw ParseError("missing coefficient n=" + std::to_string(n) +
                               " m=" + std::to_string(m),
                           0);
        }
      }
    }
  }
  return table;
}

CoefficientTable load_file(const std::filesystem::path& path,
                           const LoadOptions& options) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open coefficient file: " + path.string());
  }
  // A checkout without `git lfs pull` leaves a small text pointer in place of
  // the data. Say so plainly instead of reporting a confusing parse error.
  std::string first;
  if (std::getline(in, first) &&
      first.rfind("version https://git-lfs.github.com/spec/", 0) == 0) {
    throw ParseError(path.string() +
                         " is a Git LFS pointer, not coefficient data; run "
                         "`git lfs pull` to fetch the file",
                     1);
  }
  in.clear();
  in.seekg(0);
  return load(in, options);
}

}  // namespace gravlab::egm2008
