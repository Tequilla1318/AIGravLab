// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests are outside the MISRA scope (GoogleTest itself is not MISRA
// compliant); they use whatever is clearest.
#include "gravlab/egm2008.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <random>
#include <string>

namespace egm = gravlab::egm2008;
using egm::Status;

// ---------------------------------------------------------------------------
// Count heap allocations, to prove the parser makes none.

namespace {
std::atomic<long> g_allocations{0};
}

// Every replaceable form is replaced, so all allocations go through malloc and
// every deallocation through free (mixing with the default ones upsets ASan).
namespace {
void* counted_alloc(std::size_t size) noexcept {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size == 0 ? 1 : size);
}
void* counted_aligned_alloc(std::size_t size, std::align_val_t align) noexcept {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  const std::size_t a = static_cast<std::size_t>(align);
  const std::size_t rounded = ((size == 0 ? 1 : size) + a - 1) / a * a;
  return std::aligned_alloc(a, rounded);
}
}  // namespace

void* operator new(std::size_t size) {
  if (void* p = counted_alloc(size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
  if (void* p = counted_alloc(size)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted_alloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return counted_alloc(size);
}
void* operator new(std::size_t size, std::align_val_t a) {
  if (void* p = counted_aligned_alloc(size, a)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t a) {
  if (void* p = counted_aligned_alloc(size, a)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept {
  return counted_aligned_alloc(size, a);
}
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept {
  return counted_aligned_alloc(size, a);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(p);
}

namespace {

// Storage has static duration: the full table is ~79 MB.
egm::Storage<2190> g_full_storage;
egm::Storage<360> g_small_storage;
egm::Storage<10> g_tiny_storage;

egm::CoefficientTable bind(egm::Buffers buffers, std::int32_t capacity) {
  egm::CoefficientTable t;
  EXPECT_EQ(t.bind(buffers, capacity), Status::ok);
  return t;
}

egm::CoefficientTable tiny_table() { return bind(g_tiny_storage.buffers(), 10); }

// glibc strtod is correctly rounded, so it is an independent reference for
// the value a decimal string should become.
double reference(const char* s) { return std::strtod(s, nullptr); }

// Bit-for-bit equality, so +0.0 and -0.0 are told apart.
bool same_bits(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }

double parsed(const std::string& token) {
  double v = std::nan("");
  EXPECT_EQ(egm::parse_fortran_double(token, v), Status::ok) << token;
  return v;
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

egm::Coefficient get(const egm::CoefficientTable& t, int n, int m) {
  egm::Coefficient c;
  EXPECT_EQ(t.get(n, m, c), Status::ok) << n << ' ' << m;
  return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// parse_fortran_double

TEST(ParseFortranDouble, DExponentMatchesEExponent) {
  EXPECT_TRUE(same_bits(parsed("-0.484165143790815D-03"), reference("-0.484165143790815E-03")));
  EXPECT_TRUE(same_bits(parsed("0.243938357328313D-05"), reference("0.243938357328313E-05")));
}

TEST(ParseFortranDouble, AcceptsAllExponentMarkers) {
  for (const char* s : {"1.5D+02", "1.5d+02", "1.5E+02", "1.5e+02", "1.5D2"}) {
    EXPECT_EQ(parsed(s), 150.0) << s;
  }
}

TEST(ParseFortranDouble, SignsAndPlainForms) {
  EXPECT_EQ(parsed("+2.5"), 2.5);
  EXPECT_EQ(parsed("-2.5"), -2.5);
  EXPECT_EQ(parsed("42"), 42.0);
  EXPECT_EQ(parsed(".5"), 0.5);
  EXPECT_EQ(parsed("5."), 5.0);
  EXPECT_TRUE(same_bits(parsed("0.000000000000000D+00"), 0.0));
  EXPECT_TRUE(same_bits(parsed("-0.0D+00"), -0.0));
}

TEST(ParseFortranDouble, CorrectlyRoundedLikeStrtod) {
  EXPECT_TRUE(same_bits(parsed("0.1D+00"), reference("0.1")));

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
    double v = 0.0;
    ASSERT_EQ(egm::parse_fortran_double(d_form, v), Status::ok) << d_form;
    ASSERT_TRUE(same_bits(v, reference(e_form))) << d_form;
  }
}

TEST(ParseFortranDouble, RejectsMalformedTokensAndLeavesValue) {
  for (const char* s : {"", "abc", "D-03", "1.0D", "1.0D+", "1.0DX", "1.0 ", "1..0", "--1.0",
                        "+-1.0", "+", "-", "inf", "nan", "0x1p3", "1.0D-03junk", "1,5"}) {
    double v = 7.0;
    EXPECT_EQ(egm::parse_fortran_double(s, v), Status::invalid_real) << '"' << s << '"';
    EXPECT_EQ(v, 7.0) << s;
  }
  double v = 7.0;
  EXPECT_EQ(egm::parse_fortran_double(std::string(65, '1'), v), Status::invalid_real);
  EXPECT_EQ(egm::parse_fortran_double(std::string(64, '1'), v), Status::ok);
}

TEST(ParseFortranDouble, RejectsOverflow) {
  double v = 7.0;
  EXPECT_EQ(egm::parse_fortran_double("1.0D+400", v), Status::real_out_of_range);
  EXPECT_EQ(v, 7.0);
}

// ---------------------------------------------------------------------------
// parse_line

TEST(ParseLine, ParsesNgaRecord) {
  // First data line of EGM2008_to2190_TideFree.txt.
  egm::Coefficient c;
  ASSERT_EQ(egm::parse_line("    2    0   -0.484165143790815D-03    0.000000000000000D+00    "
                            "0.7481239490D-11    0.0000000000D+00",
                            c),
            Status::ok);
  EXPECT_EQ(c.n, 2);
  EXPECT_EQ(c.m, 0);
  EXPECT_TRUE(same_bits(c.C, reference("-0.484165143790815E-03")));
  EXPECT_TRUE(same_bits(c.S, 0.0));
  EXPECT_TRUE(same_bits(c.sigma_C, reference("0.7481239490E-11")));
  EXPECT_TRUE(same_bits(c.sigma_S, 0.0));
}

TEST(ParseLine, ToleratesTabsAndCrlf) {
  egm::Coefficient c;
  ASSERT_EQ(egm::parse_line("3\t1\t1D0\t2D0\t3D0\t4D0\r", c), Status::ok);
  EXPECT_EQ(c.n, 3);
  EXPECT_EQ(c.m, 1);
  EXPECT_EQ(c.sigma_S, 4.0);
  ASSERT_EQ(egm::parse_line("+3 +1 1D0 2D0 3D0 4D0", c), Status::ok);
  EXPECT_EQ(c.n, 3);
}

TEST(ParseLine, BlankLines) {
  egm::Coefficient c;
  EXPECT_EQ(egm::parse_line("", c), Status::empty_line);
  EXPECT_EQ(egm::parse_line("   \t \r", c), Status::empty_line);
}

TEST(ParseLine, RejectsBadRecordsWithSpecificStatus) {
  const std::pair<const char*, Status> cases[] = {
      {"2 0 1D0 2D0 3D0", Status::wrong_field_count},
      {"2 0 1D0 2D0 3D0 4D0 5D0", Status::wrong_field_count},
      {"2 3 1D0 2D0 3D0 4D0", Status::order_exceeds_degree},
      {"-2 0 1D0 2D0 3D0 4D0", Status::invalid_degree},
      {"-0 0 1D0 2D0 3D0 4D0", Status::invalid_degree},
      {"2.0 0 1D0 2D0 3D0 4D0", Status::invalid_degree},
      {"99999999999 0 1D0 2D0 3D0 4D0", Status::invalid_degree},
      {"2 x 1D0 2D0 3D0 4D0", Status::invalid_order},
      {"2 0 1D0 oops 3D0 4D0", Status::invalid_real},
      {"2 0 1D0 2D0 3D0 1D999", Status::real_out_of_range},
  };
  for (const auto& [line, expected] : cases) {
    egm::Coefficient c{9, 9, 9.0, 9.0, 9.0, 9.0};
    EXPECT_EQ(egm::parse_line(line, c), expected) << line;
    EXPECT_EQ(c.n, 9) << "record changed on failure: " << line;
  }
}

TEST(Describe, EveryStatusHasText) {
  for (int s = 0; s <= static_cast<int>(Status::io_error); ++s) {
    const char* text = egm::describe(static_cast<Status>(s));
    ASSERT_NE(text, nullptr);
    EXPECT_GT(std::strlen(text), 0u);
    EXPECT_STRNE(text, "unknown status") << s;
  }
  EXPECT_STREQ(egm::describe(static_cast<Status>(200)), "unknown status");
}

// ---------------------------------------------------------------------------
// CoefficientTable

TEST(CoefficientTable, TriangularIndex) {
  EXPECT_EQ(egm::slot_index(0, 0), 0u);
  EXPECT_EQ(egm::slot_index(1, 0), 1u);
  EXPECT_EQ(egm::slot_index(1, 1), 2u);
  EXPECT_EQ(egm::slot_index(2, 0), 3u);
  EXPECT_EQ(egm::slot_index(2190, 2190), 2401335u);
  EXPECT_EQ(egm::slot_count(2190), 2401336u);
  EXPECT_EQ(egm::slot_count(0), 1u);
  EXPECT_EQ(egm::slot_count(-1), 0u);
  static_assert(egm::Storage<2190>::kSlots == 2401336u, "");
}

TEST(CoefficientTable, UnboundTableFailsCleanly) {
  egm::CoefficientTable t;
  egm::Coefficient c;
  EXPECT_FALSE(t.is_bound());
  EXPECT_EQ(t.capacity(), -1);
  EXPECT_FALSE(t.has(2, 0));
  EXPECT_EQ(t.get(2, 0, c), Status::table_not_bound);
  EXPECT_EQ(t.set({2, 0, 1, 1, 1, 1}), Status::table_not_bound);
  EXPECT_EQ(t.C_data(), nullptr);
  EXPECT_EQ(egm::load_text("2 0 1 0 0 0\n", t, {}).status, Status::table_not_bound);
}

TEST(CoefficientTable, BindValidatesStorage) {
  egm::CoefficientTable t;
  egm::Buffers b = g_tiny_storage.buffers();
  EXPECT_EQ(t.bind(b, 11), Status::invalid_argument);  // too small
  EXPECT_FALSE(t.is_bound());
  EXPECT_EQ(t.bind(b, -1), Status::invalid_argument);
  egm::Buffers huge = b;
  huge.length = static_cast<std::size_t>(-1);  // claims to be big enough
  EXPECT_EQ(t.bind(huge, egm::kMaxCapacity + 1), Status::invalid_argument);
  EXPECT_EQ(egm::slot_count(egm::kMaxCapacity + 1), 0u);
  EXPECT_EQ(egm::slot_count(egm::kMaxCapacity), 536887296u);
  egm::Buffers no_present = b;
  no_present.present = nullptr;
  EXPECT_EQ(t.bind(no_present, 3), Status::invalid_argument);
  EXPECT_EQ(t.bind(b, 3), Status::ok);  // smaller capacity than storage is fine
  EXPECT_EQ(t.capacity(), 3);
}

TEST(CoefficientTable, SetGetAndBounds) {
  egm::CoefficientTable t = bind(g_tiny_storage.buffers(), 3);
  egm::Coefficient c;
  EXPECT_FALSE(t.has(2, 1));
  EXPECT_EQ(t.get(2, 1, c), Status::not_present);
  EXPECT_EQ(t.set({2, 1, 1.0, 2.0, 3.0, 4.0}), Status::ok);
  EXPECT_EQ(t.set({2, 1, 9.0, 9.0, 9.0, 9.0}), Status::duplicate_coefficient);
  EXPECT_TRUE(t.has(2, 1));
  c = get(t, 2, 1);
  EXPECT_EQ(c.C, 1.0);
  EXPECT_EQ(c.S, 2.0);
  EXPECT_EQ(c.sigma_C, 3.0);
  EXPECT_EQ(c.sigma_S, 4.0);
  EXPECT_EQ(t.C_data()[egm::slot_index(2, 1)], 1.0);
  EXPECT_EQ(t.max_degree(), 2);

  EXPECT_FALSE(t.has(4, 0));
  EXPECT_FALSE(t.has(2, 3));
  EXPECT_FALSE(t.has(-1, 0));
  EXPECT_EQ(t.get(4, 0, c), Status::out_of_range);
  EXPECT_EQ(t.get(2, -1, c), Status::out_of_range);
  EXPECT_EQ(t.set({4, 0, 0, 0, 0, 0}), Status::out_of_range);

  t.clear();
  EXPECT_FALSE(t.has(2, 1));
  EXPECT_EQ(t.max_degree(), -1);
  EXPECT_EQ(t.C_data()[egm::slot_index(2, 1)], 0.0);
}

// ---------------------------------------------------------------------------
// load_text

TEST(LoadText, CompleteFileRoundTrips) {
  egm::CoefficientTable t = tiny_table();
  const egm::LoadResult r = egm::load_text(complete_file(2, 6), t, {});
  ASSERT_EQ(r.status, Status::ok) << egm::describe(r.status);
  EXPECT_EQ(r.records, 25u);
  EXPECT_EQ(t.max_degree(), 6);
  for (int n = 2; n <= 6; ++n) {
    for (int m = 0; m <= n; ++m) {
      char c[32], s[32];
      std::snprintf(c, sizeof c, "%d.%03de-03", n, m);
      std::snprintf(s, sizeof s, "-%d.%03de-06", m, n);
      const egm::Coefficient got = get(t, n, m);
      EXPECT_TRUE(same_bits(got.C, reference(c))) << n << ' ' << m;
      EXPECT_TRUE(same_bits(got.S, reference(s))) << n << ' ' << m;
    }
  }
  EXPECT_FALSE(t.has(0, 0));
  EXPECT_FALSE(t.has(1, 1));
}

TEST(LoadText, LastLineWithoutNewline) {
  egm::CoefficientTable t = tiny_table();
  std::string text = complete_file(2, 2);
  text.pop_back();
  EXPECT_EQ(egm::load_text(text, t, {}).status, Status::ok);
  EXPECT_TRUE(t.has(2, 2));
}

TEST(LoadText, ReloadClearsPreviousContents) {
  egm::CoefficientTable t = tiny_table();
  ASSERT_EQ(egm::load_text(complete_file(2, 6), t, {}).status, Status::ok);
  ASSERT_EQ(egm::load_text(complete_file(2, 3), t, {}).status, Status::ok);
  EXPECT_EQ(t.max_degree(), 3);
  EXPECT_FALSE(t.has(4, 0));
}

TEST(LoadText, MaxDegreeTruncates) {
  egm::CoefficientTable t = tiny_table();
  egm::LoadOptions o;
  o.max_degree = 4;
  const egm::LoadResult r = egm::load_text(complete_file(2, 6), t, o);
  ASSERT_EQ(r.status, Status::ok);
  EXPECT_EQ(r.records, 12u);
  EXPECT_EQ(t.max_degree(), 4);
  EXPECT_TRUE(t.has(4, 4));
  EXPECT_FALSE(t.has(5, 0));
}

TEST(LoadText, MaxDegreeAboveFileIsIncomplete) {
  egm::CoefficientTable t = tiny_table();
  egm::LoadOptions o;
  o.max_degree = 8;
  const egm::LoadResult r = egm::load_text(complete_file(2, 6), t, o);
  EXPECT_EQ(r.status, Status::missing_coefficient);
  EXPECT_EQ(r.n, 7);
  EXPECT_EQ(r.m, 0);
  o.require_complete = false;
  EXPECT_EQ(egm::load_text(complete_file(2, 6), t, o).status, Status::ok);
  EXPECT_EQ(t.max_degree(), 6);
}

TEST(LoadText, OptionsAndCapacityChecks) {
  egm::CoefficientTable t = tiny_table();
  egm::LoadOptions o;
  o.max_degree = 11;
  EXPECT_EQ(egm::load_text(complete_file(2, 3), t, o).status, Status::degree_exceeds_capacity);
  o.max_degree = -2;
  EXPECT_EQ(egm::load_text(complete_file(2, 3), t, o).status, Status::invalid_argument);
  o = {};
  o.min_degree = -1;
  EXPECT_EQ(egm::load_text(complete_file(2, 3), t, o).status, Status::invalid_argument);

  // No limit, but the file goes past capacity 10.
  const egm::LoadResult r = egm::load_text(complete_file(2, 11), t, {});
  EXPECT_EQ(r.status, Status::degree_exceeds_capacity);
  EXPECT_EQ(r.n, 11);
  EXPECT_EQ(r.line, 64u);  // 63 records for n = 2..10, then the first n = 11 line
}

TEST(LoadText, MissingRecordIsAnError) {
  egm::CoefficientTable t = tiny_table();
  std::string text = complete_file(2, 4);
  const std::string gone = record_line(3, 2) + "\n";
  text.erase(text.find(gone), gone.size());
  const egm::LoadResult r = egm::load_text(text, t, {});
  EXPECT_EQ(r.status, Status::missing_coefficient);
  EXPECT_EQ(r.n, 3);
  EXPECT_EQ(r.m, 2);
  egm::LoadOptions o;
  o.require_complete = false;
  EXPECT_EQ(egm::load_text(text, t, o).status, Status::ok);
  EXPECT_FALSE(t.has(3, 2));
}

TEST(LoadText, DuplicateRecordIsAnError) {
  egm::CoefficientTable t = tiny_table();
  const egm::LoadResult r =
      egm::load_text(complete_file(2, 3) + record_line(2, 1) + "\n", t, {});
  EXPECT_EQ(r.status, Status::duplicate_coefficient);
  EXPECT_EQ(r.line, 8u);  // 3 + 4 records, then the duplicate
  EXPECT_EQ(r.n, 2);
  EXPECT_EQ(r.m, 1);
}

TEST(LoadText, BadLineReportsItsNumber) {
  egm::CoefficientTable t = tiny_table();
  const egm::LoadResult r = egm::load_text(complete_file(2, 2) + "\n3 0 1D0 2D0 3D0\n", t, {});
  EXPECT_EQ(r.status, Status::wrong_field_count);
  EXPECT_EQ(r.line, 5u);  // 3 records, a blank line, then the bad one
}

TEST(LoadText, OverlongLine) {
  egm::CoefficientTable t = tiny_table();
  const std::string text = "2 0 1D0 0D0 0D0 0D0" + std::string(300, ' ') + "\n";
  const egm::LoadResult r = egm::load_text(text, t, {});
  EXPECT_EQ(r.status, Status::line_too_long);
  EXPECT_EQ(r.line, 1u);
}

TEST(LoadText, EmptyInputIsAnError) {
  egm::CoefficientTable t = tiny_table();
  EXPECT_EQ(egm::load_text("", t, {}).status, Status::no_records);
  EXPECT_EQ(egm::load_text("\n\n  \n", t, {}).status, Status::no_records);
}

TEST(LoadText, LfsPointer) {
  egm::CoefficientTable t = tiny_table();
  EXPECT_EQ(egm::load_text("version https://git-lfs.github.com/spec/v1\noid sha256:7e44\n", t, {})
                .status,
            Status::lfs_pointer);
}

// ---------------------------------------------------------------------------
// load_file

namespace {
std::string write_temp(const std::string& name, const std::string& text) {
  const std::string path = ::testing::TempDir() + name;
  std::ofstream(path, std::ios::binary) << text;
  return path;
}
}  // namespace

TEST(LoadFile, MissingFileAndNullPath) {
  egm::CoefficientTable t = tiny_table();
  EXPECT_EQ(egm::load_file("/nonexistent/egm2008.txt", t, {}).status, Status::file_open_failed);
  EXPECT_EQ(egm::load_file(nullptr, t, {}).status, Status::invalid_argument);
}

TEST(LoadFile, MatchesLoadText) {
  // Long enough to need several 64 KiB buffer refills, with CRLF endings and
  // no final newline, so the chunked reader's edge cases are exercised.
  // Whitespace-only lines of varying length between records push about 120 KB
  // through the reader, so records straddle refill boundaries.
  std::string text;
  int k = 0;
  for (int n = 2; n <= 10; ++n) {
    for (int m = 0; m <= n; ++m) {
      for (int pad = 0; pad < 15; ++pad, ++k) {
        text += std::string(static_cast<std::size_t>((k * 13) % 250), ' ') + "\r\n";
      }
      text += record_line(n, m) + "\r\n";
    }
  }
  ASSERT_GT(text.size(), 65536u);
  text.resize(text.size() - 2);
  const std::string path = write_temp("egm_small.txt", text);

  egm::CoefficientTable from_file = bind(g_tiny_storage.buffers(), 10);
  const egm::LoadResult rf = egm::load_file(path.c_str(), from_file, {});
  ASSERT_EQ(rf.status, Status::ok) << egm::describe(rf.status) << " line " << rf.line;
  EXPECT_EQ(rf.records, 63u);

  egm::CoefficientTable from_text = bind(g_small_storage.buffers(), 10);
  ASSERT_EQ(egm::load_text(text, from_text, {}).status, Status::ok);
  for (int n = 2; n <= 10; ++n) {
    for (int m = 0; m <= n; ++m) {
      ASSERT_TRUE(same_bits(get(from_file, n, m).C, get(from_text, n, m).C));
      ASSERT_TRUE(same_bits(get(from_file, n, m).S, get(from_text, n, m).S));
    }
  }
  std::remove(path.c_str());
}

TEST(LoadFile, LfsPointerIsReportedClearly) {
  const std::string path = write_temp(
      "lfs_pointer.txt",
      "version https://git-lfs.github.com/spec/v1\n"
      "oid sha256:7e448aac4e1b8e63955890cbca08286018ecc6d203e074a64ba5bde21851438a\n"
      "size 242534633\n");
  egm::CoefficientTable t = tiny_table();
  const egm::LoadResult r = egm::load_file(path.c_str(), t, {});
  EXPECT_EQ(r.status, Status::lfs_pointer);
  EXPECT_NE(std::string(egm::describe(r.status)).find("git lfs pull"), std::string::npos);
  std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// No dynamic memory during parsing (MISRA C++:2023 Rule 21.6.1).

TEST(NoHeap, ParsingDoesNotAllocate) {
  const std::string text = complete_file(2, 10);
  const std::string path = write_temp("egm_noheap.txt", text);
  egm::CoefficientTable t = tiny_table();
  double v = 0.0;
  egm::Coefficient c;

  const long before = g_allocations.load();
  const Status s1 = egm::parse_fortran_double("-0.484165143790815D-03", v);
  const Status s2 = egm::parse_line("2 0 1D0 2D0 3D0 4D0", c);
  const egm::LoadResult r1 = egm::load_text(text, t, {});
  const egm::LoadResult r2 = egm::load_file(path.c_str(), t, {});
  const egm::LoadResult r3 = egm::load_file("/nonexistent/file", t, {});
  const long after = g_allocations.load();

  EXPECT_EQ(s1, Status::ok);
  EXPECT_EQ(s2, Status::ok);
  EXPECT_EQ(r1.status, Status::ok);
  EXPECT_EQ(r2.status, Status::ok);
  EXPECT_EQ(r3.status, Status::file_open_failed);
  EXPECT_EQ(after - before, 0) << "parser allocated from the heap";
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
    if (!in || !std::getline(in, first) || first.rfind("version https://git-lfs", 0) == 0) {
      GTEST_SKIP() << "EGM2008 data not available at " << path()
                   << " (run `git lfs pull` or set EGM2008_FILE)";
    }
  }
};

TEST_F(FullEgm2008File, LoadsEveryCoefficientToDegree2190) {
  egm::CoefficientTable t = bind(g_full_storage.buffers(), 2190);
  const std::string file = path();  // allocate before measuring
  const long before = g_allocations.load();
  const egm::LoadResult r = egm::load_file(file.c_str(), t, {});
  EXPECT_EQ(g_allocations.load() - before, 0) << "full load allocated from the heap";
  ASSERT_EQ(r.status, Status::ok) << egm::describe(r.status) << " line " << r.line;

  // Every (n, m), 2 <= n <= 2190, exactly once: sum_{n=2}^{2190} (n + 1).
  EXPECT_EQ(r.records, 2401333u);
  EXPECT_EQ(t.max_degree(), 2190);

  // Independent check against published values: IERS Conventions (2010),
  // Chapter 6, quotes EGM2008 C22 = 2.4393836e-6 and S22 = -1.4002737e-6 to
  // eight significant figures. Agreement to half a unit in the last place.
  EXPECT_NEAR(get(t, 2, 2).C, 2.4393836e-6, 0.5e-13);
  EXPECT_NEAR(get(t, 2, 2).S, -1.4002737e-6, 0.5e-13);

  // Exact values, as decimal strings in the file, against strtod.
  EXPECT_TRUE(same_bits(get(t, 2, 0).C, reference("-0.484165143790815E-03")));
  EXPECT_TRUE(same_bits(get(t, 2, 1).S, reference("0.138441389137979E-08")));
  EXPECT_TRUE(same_bits(get(t, 2, 2).sigma_C, reference("0.7230231722E-11")));

  // Zonal terms have no sine part.
  for (int n = 2; n <= 2190; ++n) ASSERT_EQ(get(t, n, 0).S, 0.0) << n;

  // Every value is finite, and every standard deviation is non-negative.
  for (std::size_t i = 0; i < egm::slot_count(2190); ++i) {
    ASSERT_TRUE(std::isfinite(t.C_data()[i]));
    ASSERT_TRUE(std::isfinite(t.S_data()[i]));
    ASSERT_GE(t.sigma_C_data()[i], 0.0);
    ASSERT_GE(t.sigma_S_data()[i], 0.0);
  }
}

TEST_F(FullEgm2008File, TruncatedLoadMatchesFullLoad) {
  egm::CoefficientTable full = bind(g_full_storage.buffers(), 2190);
  ASSERT_EQ(egm::load_file(path().c_str(), full, {}).status, Status::ok);
  egm::CoefficientTable small = bind(g_small_storage.buffers(), 360);
  egm::LoadOptions o;
  o.max_degree = 360;
  const egm::LoadResult r = egm::load_file(path().c_str(), small, o);
  ASSERT_EQ(r.status, Status::ok);
  EXPECT_EQ(small.max_degree(), 360);
  for (int n = 2; n <= 360; ++n) {
    for (int m = 0; m <= n; ++m) {
      ASSERT_TRUE(same_bits(get(small, n, m).C, get(full, n, m).C));
      ASSERT_TRUE(same_bits(get(small, n, m).S, get(full, n, m).S));
    }
  }
}
