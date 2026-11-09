// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests for the Holmes & Featherstone Legendre engine. Outside the MISRA
// scope (GoogleTest is not MISRA compliant).
//
// Independent references used:
//   - closed-form Pbar_nm for n <= 3;
//   - the identity  sum_{m=0..n} Pbar_nm(cos t)^2 = 2n + 1  for every n, t;
//   - 24 values from mpmath's hypergeometric legenp at 50 digits
//     (tools/legendre_reference.py), up to degree 2190;
//   - parity: Pbar_nm(cos(pi - t)) = (-1)^(n+m) Pbar_nm(cos t);
//   - pole values: Pbar_n0(+-1) = (+-1)^n sqrt(2n+1), Pbar_nm(+-1) = 0, m > 0.
#include "gravlab/legendre_holmes_and_featherstone.hpp"
#include "support/allocation_counter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace hf = gravlab::legendre_hf;
using gravlab::slot_count;
using gravlab::slot_index;
using hf::Status;

namespace {

constexpr double kPi = 3.141592653589793;
double deg(double d) { return d * kPi / 180.0; }

// Static storage: the full tables are tens of MB.
hf::RecursionStorage<2700> g_recursion_storage;
hf::ValueStorage<2700> g_values;
hf::ValueStorage<2700> g_values2;
hf::RecursionStorage<10> g_small_storage;

// One recursion to degree 2700, shared by all tests (initialized once).
const hf::Recursion& recursion() {
  static hf::Recursion r = [] {
    hf::Recursion rec;
    const Status s = rec.init(g_recursion_storage.buffers(), hf::kMaxDegree);
    EXPECT_EQ(s, Status::ok);
    return rec;
  }();
  return r;
}

// Evaluates into g_values and returns a pointer to them.
const double* eval(double theta, int degree, double* out = g_values.data()) {
  const Status s = hf::evaluate(recursion(), theta, degree, out, g_values.size());
  EXPECT_EQ(s, Status::ok) << hf::describe(s) << " theta=" << theta;
  return out;
}

double at(const double* p, int n, int m) { return p[slot_index(n, m)]; }

// Largest relative error of sum_m Pbar_nm^2 against 2n + 1, over 0 <= n <= N.
double identity_error(const double* p, int degree) {
  double worst = 0.0;
  for (int n = 0; n <= degree; ++n) {
    double sum = 0.0;
    for (int m = 0; m <= n; ++m) sum += at(p, n, m) * at(p, n, m);
    worst = std::max(worst, std::abs(sum - (2.0 * n + 1.0)) / (2.0 * n + 1.0));
  }
  return worst;
}

// Colatitudes covering both poles, near-pole bands, mid-latitudes, equator.
const std::vector<double>& test_angles() {
  static const std::vector<double> angles = {
      0.0,       1e-300,    1e-12,      deg(0.01), deg(0.1),  deg(0.5),
      deg(1.0),  deg(5.0),  deg(10.0),  deg(30.0), deg(45.0), deg(60.0),
      deg(80.0), deg(89.0), deg(90.0),  deg(91.0), deg(120.0), deg(150.0),
      deg(179.0), deg(179.9), kPi - 1e-12, kPi};
  return angles;
}

}  // namespace

// ---------------------------------------------------------------------------
// Statuses and argument checks

TEST(LegendreHF, DescribeCoversEveryStatus) {
  for (int s = 0; s <= static_cast<int>(Status::buffer_too_small); ++s) {
    EXPECT_STRNE(hf::describe(static_cast<Status>(s)), "unknown status") << s;
  }
  EXPECT_STREQ(hf::describe(static_cast<Status>(200)), "unknown status");
}

TEST(LegendreHF, InitRejectsBadArguments) {
  hf::Recursion r;
  hf::RecursionBuffers b = g_small_storage.buffers();
  EXPECT_EQ(r.init(b, 11), Status::invalid_argument);  // storage holds 10
  EXPECT_EQ(r.init(b, -1), Status::invalid_argument);
  hf::RecursionBuffers no_seeds = b;
  no_seeds.seeds = nullptr;
  EXPECT_EQ(r.init(no_seeds, 5), Status::invalid_argument);
  hf::RecursionBuffers short_seeds = b;
  short_seeds.seed_length = 5;
  EXPECT_EQ(r.init(short_seeds, 5), Status::invalid_argument);
  EXPECT_FALSE(r.is_initialized());
  EXPECT_EQ(r.a_data(), nullptr);

  hf::RecursionBuffers huge = b;
  huge.length = slot_count(hf::kMaxDegree + 1);
  huge.seed_length = 5000;
  EXPECT_EQ(r.init(huge, hf::kMaxDegree + 1), Status::invalid_argument);

  EXPECT_EQ(r.init(b, 10), Status::ok);
  EXPECT_TRUE(r.is_initialized());
  EXPECT_EQ(r.max_degree(), 10);
}

TEST(LegendreHF, EvaluateRejectsBadArgumentsAndWritesNothing) {
  double out[66];
  std::fill(std::begin(out), std::end(out), 7.0);
  hf::Recursion uninitialized;
  EXPECT_EQ(hf::evaluate(uninitialized, 0.5, 10, out, 66), Status::not_initialized);

  const hf::Recursion& r = recursion();
  EXPECT_EQ(hf::evaluate(r, 0.5, -1, out, 66), Status::degree_exceeds_capacity);
  EXPECT_EQ(hf::evaluate(r, 0.5, hf::kMaxDegree + 1, out, 66),
            Status::degree_exceeds_capacity);
  EXPECT_EQ(hf::evaluate(r, 0.5, 10, nullptr, 66), Status::invalid_argument);
  for (double bad : {-1e-12, -1.0, kPi + 1e-12, 4.0, std::nan(""),
                     std::numeric_limits<double>::infinity()}) {
    EXPECT_EQ(hf::evaluate(r, bad, 10, out, 66), Status::invalid_argument) << bad;
  }
  EXPECT_EQ(hf::evaluate(r, 0.5, 10, out, 65), Status::buffer_too_small);
  for (double v : out) ASSERT_EQ(v, 7.0) << "evaluate wrote despite failing";

  EXPECT_EQ(hf::evaluate(r, 0.5, 10, out, 66), Status::ok);
  EXPECT_EQ(hf::evaluate(r, 0.0, 0, out, 1), Status::ok);
  EXPECT_EQ(out[0], 1.0);
}

// ---------------------------------------------------------------------------
// Precomputed tables

TEST(LegendreHF, RecursionCoefficients) {
  const hf::Recursion& r = recursion();
  const double* a = r.a_data();
  const double* b = r.b_data();
  const double* seeds = r.seeds_data();
  EXPECT_DOUBLE_EQ(a[slot_index(1, 0)], std::sqrt(3.0));
  EXPECT_DOUBLE_EQ(a[slot_index(2, 0)], std::sqrt(15.0 / 4.0));
  EXPECT_DOUBLE_EQ(b[slot_index(2, 0)], std::sqrt(5.0 / 4.0));
  EXPECT_DOUBLE_EQ(a[slot_index(3, 1)], std::sqrt(35.0 / 8.0));
  EXPECT_DOUBLE_EQ(b[slot_index(3, 1)], std::sqrt(21.0 / 24.0));
  for (int m = 0; m < hf::kMaxDegree; m += 7) {
    // One below the diagonal: a = sqrt(2m+3), b = 0.
    EXPECT_DOUBLE_EQ(a[slot_index(m + 1, m)], std::sqrt(2.0 * m + 3.0)) << m;
    EXPECT_EQ(b[slot_index(m + 1, m)], 0.0) << m;
    // Diagonal slots are unused and zero.
    EXPECT_EQ(a[slot_index(m, m)], 0.0) << m;
  }
  EXPECT_EQ(seeds[0], hf::kScale);
  EXPECT_DOUBLE_EQ(seeds[1], hf::kScale * std::sqrt(3.0));
  EXPECT_DOUBLE_EQ(seeds[2], seeds[1] * std::sqrt(5.0 / 4.0));
  // Seeds grow only like m^(1/4): no overflow risk anywhere.
  EXPECT_LT(seeds[hf::kMaxDegree], 20.0 * hf::kScale);
}

TEST(LegendreHF, ScaleIsAnExactPowerOfTwo) {
  EXPECT_EQ(hf::kScale * hf::kInverseScale, 1.0);
  EXPECT_NEAR(std::log10(hf::kScale), -280.0, 0.1);
}

// ---------------------------------------------------------------------------
// Values

TEST(LegendreHF, ClosedFormsToDegree3) {
  for (double th : test_angles()) {
    const double t = std::cos(th), u = std::sin(th);
    const double* p = eval(th, 3);
    const double tol = 1e-14;
    EXPECT_NEAR(at(p, 0, 0), 1.0, tol);
    EXPECT_NEAR(at(p, 1, 0), std::sqrt(3.0) * t, tol);
    EXPECT_NEAR(at(p, 1, 1), std::sqrt(3.0) * u, tol);
    EXPECT_NEAR(at(p, 2, 0), std::sqrt(5.0) / 2.0 * (3.0 * t * t - 1.0), tol);
    EXPECT_NEAR(at(p, 2, 1), std::sqrt(15.0) * t * u, tol);
    EXPECT_NEAR(at(p, 2, 2), std::sqrt(15.0) / 2.0 * u * u, tol);
    EXPECT_NEAR(at(p, 3, 0), std::sqrt(7.0) / 2.0 * (5.0 * t * t * t - 3.0 * t), tol);
    EXPECT_NEAR(at(p, 3, 1), std::sqrt(21.0 / 8.0) * (5.0 * t * t - 1.0) * u, tol);
    EXPECT_NEAR(at(p, 3, 2), std::sqrt(105.0) / 2.0 * t * u * u, tol);
    EXPECT_NEAR(at(p, 3, 3), std::sqrt(35.0 / 8.0) * u * u * u, tol) << th;
  }
}

TEST(LegendreHF, MatchesMpmathReference) {
  struct Ref {
    int n, m;
    double theta_deg, value;
  };
  // Generated by tools/legendre_reference.py (mpmath legenp, 50 digits,
  // checked at 90). {n, m, colatitude_deg, Pbar_nm}
  const Ref refs[] = {
      {0, 0, 37, 1.0},
      {1, 0, 37, 1.3832772801305957},
      {1, 1, 37, 1.0423741968575877},
      {3, 1, 60, 0.350780380010057},
      {3, 2, 60, 1.9213032686174247},
      {3, 3, 60, 1.3585665699552598},
      {10, 5, 37.3, 2.505882003754521},
      {50, 25, 12.5, 3.424797422313503e-07},
      {100, 0, 1, 5.198646911164675},
      {100, 50, 45, 1.6713565587079473},
      {100, 100, 89.9, 4.758696185753562},
      {360, 1, 0.1, 11.364847874249874},
      {360, 180, 89.5, -1.5682892406239608},
      {1000, 1, 0.5, 16.969600939523378},
      {1000, 500, 30, 3.767524188769812},
      {1000, 999, 85, 0.7303259431295063},
      {2190, 0, 30, -1.3818976572328696},
      {2190, 1, 0.25, 13.932189477617735},
      {2190, 100, 2, 2.4368993114802574e-05},
      {2190, 1000, 60, -0.7233753320095806},
      {2190, 1500, 120, 0.5369371309306281},
      {2190, 2000, 89, 2.4824175100684642},
      {2190, 2188, 90, -7.268174039286318},
      {2190, 2190, 80, 2.828693401950697e-14},
  };
  double worst = 0.0;
  for (const Ref& r : refs) {
    const double* p = eval(deg(r.theta_deg), r.n);
    const double got = at(p, r.n, r.m);
    const double rel = std::abs(got - r.value) / std::abs(r.value);
    worst = std::max(worst, rel);
    // Measured worst is 8e-12 (degree 2190 near the poles, where rounding
    // cos(theta) dominates; see identity_tolerance()). 2e-11 leaves room for
    // a 1-ulp difference in another libm's cos/sin.
    EXPECT_LT(rel, 2e-11) << "n=" << r.n << " m=" << r.m << " theta=" << r.theta_deg
                          << " got " << got << " want " << r.value;
  }
  std::printf("[ info ] worst relative error vs mpmath: %.3g\n", worst);
}

TEST(LegendreHF, TinyValuesKeepFullPrecision) {
  // Deep in the decaying part of a column the unscaling factor u^m / s would
  // be subnormal without renormalization; values near 1e-200 then lost about
  // five digits (pyshtools 4.14 shows the same loss). Values below the double
  // range must flush to (near) zero, values inside it must keep full precision.
  // Strings, because literals below the double range would warn as overflow.
  struct Ref {
    int n, m;
    double theta_deg;
    const char* value;
  };
  // Generated by tools/legendre_reference.py: column recursion at 60 digits,
  // checked at 90.
  const Ref refs[] = {
      {1285, 1285, 20, "1.6234780914743017e-598"},
      {1700, 1285, 20, "3.9829395021889106e-351"},
      {2190, 1285, 20, "1.0655086297945042e-200"},
      {600, 600, 5, "1.1194919624111799e-635"},
      {1500, 600, 5, "1.4620814967681988e-327"},
      {2190, 600, 5, "1.7107030927027694e-228"},
      {2600, 2600, 45, "4.9149920150198565e-391"},
      {2700, 2600, 45, "1.7844496447036732e-299"},
  };
  for (const Ref& r : refs) {
    const double want = std::strtod(r.value, nullptr);  // 0 if below range
    const double got = at(eval(deg(r.theta_deg), r.n), r.n, r.m);
    if (want < std::numeric_limits<double>::min()) {
      EXPECT_LT(std::abs(got), 1e-300) << "n=" << r.n << " m=" << r.m << " got " << got;
    } else {
      EXPECT_LT(std::abs(got - want) / want, 1e-12)
          << "n=" << r.n << " m=" << r.m << " got " << got << " want " << want;
    }
  }
}

// Tolerance for the identity at colatitude th up to degree N.
//
// Away from the poles the recursion itself sets the error: about 1e-11 at
// degree 2190 (pyshtools: 8e-12 at the pole, 8e-14 at 45 degrees).
//
// Within 0.1 degree of a pole the limit is the input, not the method: cos(th)
// is stored with an absolute error of ~1e-16, which near t = 1 is a relative
// error of up to ~1e-8 in 1 - t, and Pbar_n0 amplifies that by ~n^2. Measured
// against exact values at the same double th (mpmath), Pbar_2190,0 at 0.01
// degrees is off by 1.3e-10 here and in pyshtools alike, so the band gets a
// tolerance of N^2 * epsilon.
double identity_tolerance(double th, int N) {
  const double band = deg(0.1);
  const bool near_pole = (th < band) || (th > (kPi - band));
  return near_pole ? static_cast<double>(N) * N * std::numeric_limits<double>::epsilon() : 2e-11;
}

TEST(LegendreHF, SumOfSquaresIdentityToDegree2190) {
  double worst = 0.0, worst_away = 0.0;
  for (double th : test_angles()) {
    const double e = identity_error(eval(th, 2190), 2190);
    worst = std::max(worst, e);
    if (identity_tolerance(th, 2190) == 2e-11) worst_away = std::max(worst_away, e);
    EXPECT_LT(e, identity_tolerance(th, 2190)) << "theta=" << th;
  }
  std::printf("[ info ] identity error to degree 2190: worst %.3g, worst away from poles %.3g\n",
              worst, worst_away);
}

TEST(LegendreHF, SumOfSquaresIdentityToMaxDegree2700) {
  double worst = 0.0;
  for (double th : test_angles()) {
    const double e = identity_error(eval(th, hf::kMaxDegree), hf::kMaxDegree);
    worst = std::max(worst, e);
    EXPECT_LT(e, identity_tolerance(th, hf::kMaxDegree)) << "theta=" << th;
  }
  std::printf("[ info ] worst identity error to degree 2700: %.3g\n", worst);
}

TEST(LegendreHF, AllValuesFinite) {
  for (double th : test_angles()) {
    const double* p = eval(th, hf::kMaxDegree);
    for (std::size_t i = 0; i < slot_count(hf::kMaxDegree); ++i) {
      ASSERT_TRUE(std::isfinite(p[i])) << "theta=" << th << " slot " << i;
    }
  }
}

TEST(LegendreHF, ParityAcrossTheEquator) {
  for (double th : {deg(0.3), deg(10.0), deg(37.0), deg(70.0)}) {
    const double* north = eval(th, 2190, g_values.data());
    const double* south = eval(kPi - th, 2190, g_values2.data());
    for (int n = 0; n <= 2190; ++n) {
      const double row = std::sqrt(2.0 * n + 1.0);  // bound on |Pbar_nm| for this n
      for (int m = 0; m <= n; ++m) {
        const double sign = ((n + m) % 2 == 0) ? 1.0 : -1.0;
        ASSERT_NEAR(at(south, n, m), sign * at(north, n, m), 1e-11 * row)
            << "n=" << n << " m=" << m << " theta=" << th;
      }
    }
  }
}

TEST(LegendreHF, PoleValues) {
  // Pbar_n0(1) = sqrt(2n+1). The recursion reaches it to ~4e-12 at degree
  // 2190, the same as pyshtools (3.5e-12); see identity_tolerance().
  const double* north = eval(0.0, 2190);
  for (int n = 0; n <= 2190; ++n) {
    ASSERT_NEAR(at(north, n, 0), std::sqrt(2.0 * n + 1.0), 1e-11 * std::sqrt(2.0 * n + 1.0));
    for (int m = 1; m <= n; ++m) ASSERT_EQ(at(north, n, m), 0.0) << n << ' ' << m;
  }
  // sin(kPi) is 1.2e-16, not 0, so the m > 0 terms are tiny rather than zero.
  const double* south = eval(kPi, 2190);
  for (int n = 0; n <= 2190; ++n) {
    const double sign = (n % 2 == 0) ? 1.0 : -1.0;
    ASSERT_NEAR(at(south, n, 0), sign * std::sqrt(2.0 * n + 1.0), 1e-11 * std::sqrt(2.0 * n + 1.0));
    for (int m = 1; m <= n; ++m) ASSERT_LT(std::abs(at(south, n, m)), 1e-9) << n << ' ' << m;
  }
}

TEST(LegendreHF, LowerDegreeIsAPrefixOfHigherDegree) {
  const double* full = eval(deg(33.3), 2190, g_values.data());
  const double* part = eval(deg(33.3), 360, g_values2.data());
  for (std::size_t i = 0; i < slot_count(360); ++i) ASSERT_EQ(part[i], full[i]) << i;
}

// ---------------------------------------------------------------------------
// Why the scaling is needed: the same recursion without it.

TEST(LegendreHF, UnscaledRecursionUnderflowsWhereScaledOneDoesNot) {
  // Standard forward column method, no scaling: seeds Pbar_mm = u^m * prod.
  // Uses the same a_nm, b_nm, so the only difference is the scaling.
  const int N = 2190;
  const double th = deg(20.0);  // 70 degrees latitude
  const double t = std::cos(th), u = std::sin(th);
  const hf::Recursion& r = recursion();
  double* p = g_values2.data();
  double pmm = 1.0;
  for (int m = 0; m <= N; ++m) {
    if (m == 1) pmm = std::sqrt(3.0) * u;
    if (m >= 2) pmm *= u * std::sqrt((2.0 * m + 1.0) / (2.0 * m));
    p[slot_index(m, m)] = pmm;
    double x2 = 0.0, x1 = pmm;
    for (int n = m + 1; n <= N; ++n) {
      const double x = r.a_data()[slot_index(n, m)] * t * x1 - r.b_data()[slot_index(n, m)] * x2;
      p[slot_index(n, m)] = x;
      x2 = x1;
      x1 = x;
    }
  }
  const double unscaled = identity_error(p, N);
  const double scaled = identity_error(eval(th, N), N);
  std::printf("[ info ] identity error at 70 deg latitude, degree 2190: unscaled %.3g, scaled %.3g\n",
              unscaled, scaled);
  EXPECT_GT(unscaled, 1e-3);  // the unscaled method has lost whole columns
  EXPECT_LT(scaled, 1e-12);
}

// ---------------------------------------------------------------------------
// No dynamic memory (MISRA C++:2023 Rule 21.6.1)

TEST(LegendreHF, NoHeapAllocation) {
  hf::Recursion r;
  const long before = gravlab_test::allocation_count();
  const Status s1 = r.init(g_recursion_storage.buffers(), 2190);
  const Status s2 = hf::evaluate(r, deg(42.0), 2190, g_values.data(), g_values.size());
  const Status s3 = hf::evaluate(r, -1.0, 2190, g_values.data(), g_values.size());
  const long after = gravlab_test::allocation_count();
  EXPECT_EQ(s1, Status::ok);
  EXPECT_EQ(s2, Status::ok);
  EXPECT_EQ(s3, Status::invalid_argument);
  EXPECT_EQ(after - before, 0) << "Legendre engine allocated from the heap";
  // Restore the shared full-degree recursion for later tests.
  ASSERT_EQ(r.init(g_recursion_storage.buffers(), hf::kMaxDegree), Status::ok);
}
