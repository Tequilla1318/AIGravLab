// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gravlab/egm2008.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>

namespace egm = gravlab::egm2008;

namespace {

// glibc strtod is correctly rounded, so it is an independent reference for
// the value a decimal string should become.
double reference(const char* s) { return std::strtod(s, nullptr); }

// Bit-for-bit equality, so +0.0 and -0.0 are told apart.
bool same_bits(double a, double b) {
  return std::memcmp(&a, &b, sizeof(double)) == 0;
}

// One record with distinct, easy-to-check values per (n, m):
// C = n.mmm e-3, S = -m.nnn e-6.
std::string record_line(int n, int m) {
  char buf[160];
  std::snprintf(buf, sizeof buf, "%5d%5d   %d.%03dD-03   -%d.%03dD-06   1.0D-11   2.0D-11",
                n, m, n, m, m, n);
  return buf;
}

std::string complete_file(int from, int to) {
  std::string out;
  for (int n = from; n <= to; ++n)
    for (int m = 0; m <= n; ++m) out += record_line(n, m) + "\n";
  return out;
}

egm::CoefficientTable load_string(const std::string& text,
                                  const egm::LoadOptions& options = {}) {
  std::istringstream in(text);
  return egm::load(in, options);
}

}  // namespace

// ---------------------------------------------------------------------------
// parse_fortran_double

TEST(ParseFortranDouble, DExponentMatchesEExponent) {
  EXPECT_TRUE(same_bits(egm::parse_fortran_double("-0.484165143790815D-03"),
                        reference("-0.484165143790815E-03")));
  EXPECT_TRUE(same_bits(egm::parse_fortran_double("0.243938357328313D-05"),
                        reference("0.243938357328313E-05")));
}

TEST(ParseFortranDouble, AcceptsAllExponentMarkers) {
  for (const char* s : {"1.5D+02", "1.5d+02", "1.5E+02", "1.5e+02", "1.5D2"}) {
    EXPECT_EQ(egm::parse_fortran_double(s), 150.0) << s;
  }
}

TEST(ParseFortranDouble, SignsAndPlainForms) {
  EXPECT_EQ(egm::parse_fortran_double("+2.5"), 2.5);
  EXPECT_EQ(egm::parse_fortran_double("-2.5"), -2.5);
  EXPECT_EQ(egm::parse_fortran_double("42"), 42.0);
  EXPECT_EQ(egm::parse_fortran_double(".5"), 0.5);
  EXPECT_EQ(egm::parse_fortran_double("5."), 5.0);
  EXPECT_TRUE(same_bits(egm::parse_fortran_double("0.000000000000000D+00"), 0.0));
  EXPECT_TRUE(same_bits(egm::parse_fortran_double("-0.0D+00"), -0.0));
}

TEST(ParseFortranDouble, CorrectlyRoundedLikeStrtod) {
  // 0.1 is not representable; both must pick the same nearest double.
  EXPECT_TRUE(same_bits(egm::parse_fortran_double("0.1D+00"), reference("0.1")));

  // Random 15- and 17-significant-digit values over the exponent range the
  // NGA file uses, written the way the file writes them.
  std::mt19937_64 rng(20081);
  std::uniform_real_distribution<double> mant(-1.0, 1.0);
  std::uniform_int_distribution<int> expo(-40, 5);
  char e_form[64];
  for (int i = 0; i < 100000; ++i) {
    const double x = mant(rng) * std::pow(10.0, expo(rng));
    std::snprintf(e_form, sizeof e_form, (i % 2) ? "%.14E" : "%.16E", x);
    std::string d_form = e_form;
    d_form[d_form.find('E')] = 'D';
    ASSERT_TRUE(same_bits(egm::parse_fortran_double(d_form), reference(e_form)))
        << d_form;
  }
}

TEST(ParseFortranDouble, RejectsMalformedTokens) {
  for (const char* s : {"", "abc", "D-03", "1.0D", "1.0D+", "1.0DX", "1.0 ",
                        "1..0", "--1.0", "+-1.0", "+", "-", "inf", "nan",
                        "0x1p3", "1.0D-03junk", "1,5"}) {
    EXPECT_THROW(egm::parse_fortran_double(s), std::invalid_argument) << '"' << s << '"';
  }
  EXPECT_THROW(egm::parse_fortran_double(std::string(65, '1')),
               std::invalid_argument);
}

TEST(ParseFortranDouble, RejectsOverflow) {
  EXPECT_THROW(egm::parse_fortran_double("1.0D+400"), std::out_of_range);
}

// ---------------------------------------------------------------------------
// parse_line

TEST(ParseLine, ParsesNgaRecord) {
  // First data line of EGM2008_to2190_TideFree.txt.
  const auto c = egm::parse_line(
      "    2    0   -0.484165143790815D-03    0.000000000000000D+00    "
      "0.7481239490D-11    0.0000000000D+00");
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->n, 2);
  EXPECT_EQ(c->m, 0);
  EXPECT_TRUE(same_bits(c->C, reference("-0.484165143790815E-03")));
  EXPECT_TRUE(same_bits(c->S, 0.0));
  EXPECT_TRUE(same_bits(c->sigma_C, reference("0.7481239490E-11")));
  EXPECT_TRUE(same_bits(c->sigma_S, 0.0));
}

TEST(ParseLine, ToleratesTabsAndCrlf) {
  const auto c = egm::parse_line("3\t1\t1D0\t2D0\t3D0\t4D0\r");
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->n, 3);
  EXPECT_EQ(c->m, 1);
  EXPECT_EQ(c->sigma_S, 4.0);
}

TEST(ParseLine, BlankLinesAreSkipped) {
  EXPECT_FALSE(egm::parse_line("").has_value());
  EXPECT_FALSE(egm::parse_line("   \t \r").has_value());
}

TEST(ParseLine, RejectsBadRecordsWithLineNumber) {
  const char* bad[] = {
      "2 0 1D0 2D0 3D0",              // too few fields
      "2 0 1D0 2D0 3D0 4D0 5D0",      // too many
      "2 3 1D0 2D0 3D0 4D0",          // m > n
      "-2 0 1D0 2D0 3D0 4D0",         // negative degree
      "2 x 1D0 2D0 3D0 4D0",          // non-integer order
      "2.0 0 1D0 2D0 3D0 4D0",        // non-integer degree
      "2 0 1D0 oops 3D0 4D0",         // bad real
  };
  for (const char* line : bad) {
    try {
      egm::parse_line(line, 17);
      ADD_FAILURE() << "accepted: " << line;
    } catch (const egm::ParseError& e) {
      EXPECT_EQ(e.line(), 17u) << line;
      EXPECT_NE(std::string(e.what()).find("line 17"), std::string::npos) << e.what();
    }
  }
}

// ---------------------------------------------------------------------------
// CoefficientTable

TEST(CoefficientTable, TriangularIndex) {
  EXPECT_EQ(egm::CoefficientTable::index(0, 0), 0u);
  EXPECT_EQ(egm::CoefficientTable::index(1, 0), 1u);
  EXPECT_EQ(egm::CoefficientTable::index(1, 1), 2u);
  EXPECT_EQ(egm::CoefficientTable::index(2, 0), 3u);
  EXPECT_EQ(egm::CoefficientTable::index(2190, 2190), 2401335u);
  EXPECT_EQ(egm::CoefficientTable(2190).size(), 2401336u);
}

TEST(CoefficientTable, SetGetAndBounds) {
  egm::CoefficientTable t(3);
  EXPECT_FALSE(t.has(2, 1));
  EXPECT_EQ(t.C(2, 1), 0.0);
  EXPECT_TRUE(t.set({2, 1, 1.0, 2.0, 3.0, 4.0}));
  EXPECT_FALSE(t.set({2, 1, 9.0, 9.0, 9.0, 9.0}));  // duplicate ignored
  EXPECT_TRUE(t.has(2, 1));
  EXPECT_EQ(t.C(2, 1), 1.0);
  EXPECT_EQ(t.S(2, 1), 2.0);
  EXPECT_EQ(t.sigma_C(2, 1), 3.0);
  EXPECT_EQ(t.sigma_S(2, 1), 4.0);
  EXPECT_EQ(t.C_data()[egm::CoefficientTable::index(2, 1)], 1.0);

  EXPECT_FALSE(t.has(4, 0));
  EXPECT_FALSE(t.has(2, 3));
  EXPECT_FALSE(t.has(-1, 0));
  EXPECT_THROW(t.C(4, 0), std::out_of_range);
  EXPECT_THROW(t.S(2, 3), std::out_of_range);
  EXPECT_THROW(t.set({4, 0, 0, 0, 0, 0}), std::out_of_range);
}

TEST(CoefficientTable, ExtendKeepsEntries) {
  egm::CoefficientTable t(2);
  t.set({2, 2, 5.0, 6.0, 0.0, 0.0});
  t.extend_to(10);
  EXPECT_EQ(t.max_degree(), 10);
  EXPECT_EQ(t.C(2, 2), 5.0);
  EXPECT_FALSE(t.has(10, 10));
}

// ---------------------------------------------------------------------------
// load

TEST(Load, CompleteFileRoundTrips) {
  const auto t = load_string(complete_file(2, 6));
  EXPECT_EQ(t.max_degree(), 6);
  for (int n = 2; n <= 6; ++n) {
    for (int m = 0; m <= n; ++m) {
      ASSERT_TRUE(t.has(n, m));
      char c[32], s[32];
      std::snprintf(c, sizeof c, "%d.%03de-03", n, m);
      std::snprintf(s, sizeof s, "-%d.%03de-06", m, n);
      EXPECT_TRUE(same_bits(t.C(n, m), reference(c))) << n << ' ' << m;
      EXPECT_TRUE(same_bits(t.S(n, m), reference(s))) << n << ' ' << m;
    }
  }
  EXPECT_FALSE(t.has(0, 0));
  EXPECT_FALSE(t.has(1, 1));
}

TEST(Load, MaxDegreeTruncates) {
  egm::LoadOptions o;
  o.max_degree = 4;
  const auto t = load_string(complete_file(2, 6), o);
  EXPECT_EQ(t.max_degree(), 4);
  EXPECT_TRUE(t.has(4, 4));
  EXPECT_THROW(t.C(5, 0), std::out_of_range);
}

TEST(Load, MaxDegreeAboveFileIsIncomplete) {
  egm::LoadOptions o;
  o.max_degree = 8;
  EXPECT_THROW(load_string(complete_file(2, 6), o), egm::ParseError);
  o.require_complete = false;
  const auto t = load_string(complete_file(2, 6), o);
  EXPECT_EQ(t.max_degree(), 8);
  EXPECT_FALSE(t.has(7, 0));
}

TEST(Load, MissingRecordIsAnError) {
  std::string text = complete_file(2, 4);
  const std::string gone = record_line(3, 2) + "\n";
  text.erase(text.find(gone), gone.size());
  try {
    load_string(text);
    FAIL() << "missing record accepted";
  } catch (const egm::ParseError& e) {
    EXPECT_NE(std::string(e.what()).find("n=3 m=2"), std::string::npos) << e.what();
  }
  egm::LoadOptions o;
  o.require_complete = false;
  EXPECT_FALSE(load_string(text, o).has(3, 2));
}

TEST(Load, DuplicateRecordIsAnError) {
  const std::string text = complete_file(2, 3) + record_line(2, 1) + "\n";
  try {
    load_string(text);
    FAIL() << "duplicate accepted";
  } catch (const egm::ParseError& e) {
    EXPECT_EQ(e.line(), 8u);  // 3 + 4 records, then the duplicate
  }
}

TEST(Load, BadLineReportsItsNumber) {
  const std::string text = complete_file(2, 2) + "\n3 0 1D0 2D0 3D0\n";
  try {
    load_string(text);
    FAIL();
  } catch (const egm::ParseError& e) {
    EXPECT_EQ(e.line(), 5u);  // 3 records, a blank line, then the bad one
  }
}

TEST(Load, EmptyInputIsAnError) {
  EXPECT_THROW(load_string(""), egm::ParseError);
  EXPECT_THROW(load_string("\n\n  \n"), egm::ParseError);
}

TEST(LoadFile, MissingFile) {
  EXPECT_THROW(egm::load_file("/nonexistent/egm2008.txt"), std::runtime_error);
}

TEST(LoadFile, LfsPointerIsReportedClearly) {
  const std::string path = ::testing::TempDir() + "lfs_pointer.txt";
  {
    std::ofstream out(path);
    out << "version https://git-lfs.github.com/spec/v1\n"
           "oid sha256:7e448aac4e1b8e63955890cbca08286018ecc6d203e074a64ba5bde21851438a\n"
           "size 242534633\n";
  }
  try {
    egm::load_file(path);
    FAIL();
  } catch (const egm::ParseError& e) {
    EXPECT_NE(std::string(e.what()).find("git lfs pull"), std::string::npos);
  }
  std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Full NGA file. Skips when only the LFS pointer is checked out.

class FullEgm2008File : public ::testing::Test {
 protected:
  static std::string path() {
    if (const char* env = std::getenv("EGM2008_FILE")) return env;
    return GRAVLAB_EGM2008_FILE;
  }
  void SetUp() override {
    std::ifstream in(path());
    std::string first;
    if (!in || !std::getline(in, first) ||
        first.rfind("version https://git-lfs", 0) == 0) {
      GTEST_SKIP() << "EGM2008 data not available at " << path()
                   << " (run `git lfs pull` or set EGM2008_FILE)";
    }
  }
};

TEST_F(FullEgm2008File, LoadsEveryCoefficientToDegree2190) {
  const auto t = egm::load_file(path());
  EXPECT_EQ(t.max_degree(), 2190);
  // load() already checked that every (n, m), 2 <= n <= 2190, appears exactly
  // once: sum_{n=2}^{2190} (n + 1) = 2,401,333 records.
  std::size_t present = 0;
  for (int n = 0; n <= 2190; ++n)
    for (int m = 0; m <= n; ++m) present += t.has(n, m);
  EXPECT_EQ(present, 2401333u);

  // Independent check against published values: IERS Conventions (2010),
  // Chapter 6, quotes EGM2008 C22 = 2.4393836e-6 and S22 = -1.4002737e-6 to
  // eight significant figures. Agreement to half a unit in the last place.
  EXPECT_NEAR(t.C(2, 2), 2.4393836e-6, 0.5e-13);
  EXPECT_NEAR(t.S(2, 2), -1.4002737e-6, 0.5e-13);

  // Exact values, as decimal strings in the file, against strtod.
  EXPECT_TRUE(same_bits(t.C(2, 0), reference("-0.484165143790815E-03")));
  EXPECT_TRUE(same_bits(t.S(2, 1), reference("0.138441389137979E-08")));
  EXPECT_TRUE(same_bits(t.sigma_C(2, 2), reference("0.7230231722E-11")));

  // Zonal terms have no sine part.
  for (int n = 2; n <= 2190; ++n) ASSERT_EQ(t.S(n, 0), 0.0) << n;

  // Every value is finite, and every standard deviation is non-negative.
  for (std::size_t i = 0; i < t.size(); ++i) {
    ASSERT_TRUE(std::isfinite(t.C_data()[i]));
    ASSERT_TRUE(std::isfinite(t.S_data()[i]));
    ASSERT_GE(t.sigma_C_data()[i], 0.0);
    ASSERT_GE(t.sigma_S_data()[i], 0.0);
  }
}

TEST_F(FullEgm2008File, TruncatedLoadMatchesFullLoad) {
  egm::LoadOptions o;
  o.max_degree = 360;
  const auto small = egm::load_file(path(), o);
  const auto full = egm::load_file(path());
  ASSERT_EQ(small.max_degree(), 360);
  for (int n = 2; n <= 360; ++n) {
    for (int m = 0; m <= n; ++m) {
      ASSERT_TRUE(same_bits(small.C(n, m), full.C(n, m)));
      ASSERT_TRUE(same_bits(small.S(n, m), full.S(n, m)));
    }
  }
}
