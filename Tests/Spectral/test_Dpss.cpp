/*
    Tests for the DPSS (Slepian) taper bank.

    The reference values come from scipy.signal.windows.dpss, which uses the same
    tridiagonal formulation and the same sign convention:

        from scipy.signal.windows import dpss
        w, lam = dpss(N, NW, Kmax=K, return_ratios=True)
*/
#include "Spectral/Dpss.h"
#include "Spectral/Tapers.h"

#include <cmath>
#include <gtest/gtest.h>
#include <numeric>

using namespace TriggeredSpectra;

namespace
{

double dot (std::span<const double> a, std::span<const double> b)
{
    return std::inner_product (a.begin(), a.end(), b.begin(), 0.0);
}

} // namespace

TEST (Dpss, DefaultTaperCountIsTwoNwMinusOne)
{
    EXPECT_EQ (Dpss::defaultNumTapers (2.0), 3);
    EXPECT_EQ (Dpss::defaultNumTapers (3.0), 5);
    EXPECT_EQ (Dpss::defaultNumTapers (4.0), 7);
    EXPECT_EQ (Dpss::defaultNumTapers (2.5), 4);

    // Never returns something unusable.
    EXPECT_GE (Dpss::defaultNumTapers (0.5), 1);
}

TEST (Dpss, RejectsDegenerateInputs)
{
    EXPECT_TRUE (Dpss::compute (0, 3.0, 5).empty());
    EXPECT_TRUE (Dpss::compute (1, 3.0, 5).empty());
    EXPECT_TRUE (Dpss::compute (128, 0.0, 5).empty());
    EXPECT_TRUE (Dpss::compute (128, 3.0, 0).empty());
}

TEST (Dpss, TapersAreOrthonormal)
{
    constexpr int n = 256;
    constexpr int k = 5;

    const TaperBank bank = Dpss::compute (n, 3.0, k);

    ASSERT_EQ (bank.numTapers(), k);
    ASSERT_EQ (bank.length(), n);

    for (int i = 0; i < k; ++i)
    {
        EXPECT_NEAR (dot (bank.taper (i), bank.taper (i)), 1.0, 1e-10) << "taper " << i;

        for (int j = i + 1; j < k; ++j)
            EXPECT_NEAR (dot (bank.taper (i), bank.taper (j)), 0.0, 1e-8)
                << "tapers " << i << " and " << j;
    }
}

TEST (Dpss, ConcentrationsDecreaseAndAreNearOne)
{
    constexpr int n = 128;
    constexpr double nw = 4.0;
    const int k = Dpss::defaultNumTapers (nw); // 7

    const TaperBank bank = Dpss::compute (n, nw, k);
    const std::vector<double> lambda = Dpss::concentrations (bank, nw);

    ASSERT_EQ (static_cast<int> (lambda.size()), k);

    // Strictly decreasing: taper 0 is the most concentrated by construction.
    for (int i = 0; i + 1 < k; ++i)
        EXPECT_GT (lambda[static_cast<std::size_t> (i)], lambda[static_cast<std::size_t> (i + 1)])
            << "lambda[" << i << "] vs lambda[" << i + 1 << "]";

    // The first 2*NW-1 tapers are the ones worth using; all should still be
    // well concentrated.
    for (int i = 0; i < k; ++i)
    {
        EXPECT_GT (lambda[static_cast<std::size_t> (i)], 0.9) << "taper " << i;
        EXPECT_LE (lambda[static_cast<std::size_t> (i)], 1.0 + 1e-9) << "taper " << i;
    }

    // Beyond 2*NW concentration collapses - a good sanity check that we really
    // are computing the Slepian sequences and not just any orthonormal basis.
    const TaperBank wide = Dpss::compute (n, nw, k + 3);
    const std::vector<double> wideLambda = Dpss::concentrations (wide, nw);

    EXPECT_LT (wideLambda.back(), 0.5);
}

TEST (Dpss, SignConventionMatchesScipy)
{
    const TaperBank bank = Dpss::compute (64, 3.0, 4);
    ASSERT_EQ (bank.numTapers(), 4);

    // Even orders are symmetric with a positive sum.
    for (const int k : { 0, 2 })
    {
        const auto w = bank.taper (k);
        EXPECT_GT (std::accumulate (w.begin(), w.end(), 0.0), 0.0) << "taper " << k;
    }

    // Odd orders are antisymmetric (sum ~ 0) with a positive first lobe.
    for (const int k : { 1, 3 })
    {
        const auto w = bank.taper (k);

        EXPECT_NEAR (std::accumulate (w.begin(), w.end(), 0.0), 0.0, 1e-8) << "taper " << k;

        double peak = 0.0;
        for (int i = 0; i < bank.length() / 2; ++i)
            if (std::abs (w[static_cast<std::size_t> (i)]) > std::abs (peak))
                peak = w[static_cast<std::size_t> (i)];

        EXPECT_GT (peak, 0.0) << "taper " << k;
    }
}

TEST (Dpss, EvenOrdersAreSymmetricAndOddOrdersAntisymmetric)
{
    constexpr int n = 65; // odd length, so there is an exact centre sample
    const TaperBank bank = Dpss::compute (n, 3.0, 4);

    for (int k = 0; k < bank.numTapers(); ++k)
    {
        const auto w = bank.taper (k);
        const double sign = (k % 2 == 0) ? 1.0 : -1.0;

        for (int i = 0; i < n; ++i)
            EXPECT_NEAR (w[static_cast<std::size_t> (i)],
                         sign * w[static_cast<std::size_t> (n - 1 - i)],
                         1e-8)
                << "taper " << k << ", sample " << i;
    }
}

/** Reference values from scipy.signal.windows.dpss(32, 2.5, Kmax=3).
 *
 *  Generated with:
 *      from scipy.signal.windows import dpss
 *      w, lam = dpss(32, 2.5, Kmax=3, return_ratios=True)
 *
 *  Only the first half of each taper is listed; taper 0 and 2 are symmetric and
 *  taper 1 antisymmetric, which the symmetry test above already pins down.
 */
TEST (Dpss, MatchesScipyReferenceValues)
{
    constexpr int n = 32;
    constexpr int k = 3;
    constexpr int half = n / 2;

    const TaperBank bank = Dpss::compute (n, 2.5, k);
    ASSERT_EQ (bank.numTapers(), k);
    ASSERT_EQ (bank.length(), n);

    const double expected[k][half] = {
        { 0.0025368751, 0.0066084334, 0.0134116297, 0.0236618116, 0.0379442936,
          0.0566089475, 0.0796749583, 0.1067608253, 0.1370528523, 0.1693212974,
          0.2019874926, 0.2332383981, 0.2611782219, 0.2840009828, 0.3001641461,
          0.3085423871 },
        { 0.0147509878, 0.0315884778, 0.0551871711, 0.0852194464, 0.1203781230,
          0.1583566006, 0.1959777820, 0.2294710131, 0.2548719835, 0.2684983579,
          0.2674378285, 0.2499784390, 0.2159151130, 0.1666811998, 0.1052775336,
          0.0360004228 },
        { 0.0565977087, 0.0973505540, 0.1431277837, 0.1888864177, 0.2286205362,
          0.2561936896, 0.2662943431, 0.2553608905, 0.2223158156, 0.1689701871,
          0.1000074285, 0.0225218020, -0.0548389251, -0.1230086113, -0.1737469945,
          -0.2008011514 }
    };

    for (int taperIndex = 0; taperIndex < k; ++taperIndex)
    {
        const auto w = bank.taper (taperIndex);

        for (int i = 0; i < half; ++i)
            EXPECT_NEAR (w[static_cast<std::size_t> (i)], expected[taperIndex][i], 1e-9)
                << "taper " << taperIndex << ", sample " << i;
    }

    // Concentrations from the same scipy call.
    const std::vector<double> lambda = Dpss::concentrations (bank, 2.5);
    ASSERT_EQ (lambda.size(), 3u);

    EXPECT_NEAR (lambda[0], 0.9999975437, 1e-9);
    EXPECT_NEAR (lambda[1], 0.9998568849, 1e-9);
    EXPECT_NEAR (lambda[2], 0.9964195811, 1e-9);
}

TEST (Dpss, WorksAtWindowLengthsSeenInPractice)
{
    // 1.5 s at 1 kHz - the sort of window a line-mode trial actually uses.
    constexpr int n = 1500;
    constexpr double nw = 3.0;
    const int k = Dpss::defaultNumTapers (nw);

    const TaperBank bank = Dpss::compute (n, nw, k);

    ASSERT_EQ (bank.numTapers(), k);
    ASSERT_EQ (bank.length(), n);

    for (int i = 0; i < k; ++i)
    {
        EXPECT_NEAR (dot (bank.taper (i), bank.taper (i)), 1.0, 1e-9) << "taper " << i;

        for (int j = i + 1; j < k; ++j)
            EXPECT_NEAR (dot (bank.taper (i), bank.taper (j)), 0.0, 1e-7)
                << "tapers " << i << " and " << j;

        // No NaNs or blow-ups from the inverse iteration.
        for (const double value : bank.taper (i))
            ASSERT_TRUE (std::isfinite (value)) << "taper " << i;
    }
}

TEST (Hann, IsPeriodicAndUnitPeak)
{
    constexpr int n = 8;
    const TaperBank bank = makeHannTaper (n);

    ASSERT_EQ (bank.numTapers(), 1);
    ASSERT_EQ (bank.length(), n);

    const auto w = bank.taper (0);

    // Periodic Hann starts at exactly 0 and does not return to 0 at the end.
    EXPECT_NEAR (w[0], 0.0, 1e-12);
    EXPECT_NEAR (w[static_cast<std::size_t> (n / 2)], 1.0, 1e-12);
    EXPECT_GT (w[static_cast<std::size_t> (n - 1)], 0.0);

    // Sum of squares of a periodic Hann is 3n/8.
    EXPECT_NEAR (bank.sumOfSquares (0), 3.0 * n / 8.0, 1e-12);
}
