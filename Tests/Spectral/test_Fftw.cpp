/*
    Tests for the FFTW wrappers.

    These exist mostly to prove the vendored FFTW builds, links and runs, and that
    the batched plans address their input and output strides the way the transform
    code assumes.
*/
#include "Spectral/Fftw.h"

#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <vector>

using namespace TriggeredSpectra;
using namespace TriggeredSpectra::Fftw;

TEST (Fftw, AlignedBufferManagesItsMemory)
{
    RealBuffer buffer (128);

    ASSERT_EQ (buffer.size(), 128u);
    ASSERT_NE (buffer.data(), nullptr);

    buffer.clear();
    EXPECT_DOUBLE_EQ (buffer[0], 0.0);
    EXPECT_DOUBLE_EQ (buffer[127], 0.0);

    RealBuffer moved = std::move (buffer);
    EXPECT_EQ (moved.size(), 128u);
    EXPECT_EQ (buffer.size(), 0u);
    EXPECT_EQ (buffer.data(), nullptr);
}

TEST (Fftw, RealToComplexRecoversASinusoid)
{
    constexpr int n = 256;
    constexpr int binOfInterest = 8;

    RealBuffer in (n);
    ComplexBuffer out (n / 2 + 1);

    // Plan first: FFTW_MEASURE destroys the buffers, so filling before planning
    // would be wasted work (and a subtle source of bugs).
    const RealToComplexPlan plan (n, 1, in.data(), out.data(), PlanRigor::Estimate);
    ASSERT_TRUE (plan.isValid());
    EXPECT_EQ (plan.numSpectrumBins(), n / 2 + 1);

    for (int i = 0; i < n; ++i)
        in[i] = std::cos (2.0 * std::numbers::pi * binOfInterest * i / n);

    plan.execute (in.data(), out.data());

    // A pure cosine at an exact bin centre puts all its energy in that one bin,
    // with magnitude n/2 under FFTW's unnormalised convention.
    for (int bin = 0; bin < plan.numSpectrumBins(); ++bin)
    {
        const double magnitude = std::abs (out[static_cast<std::size_t> (bin)]);

        if (bin == binOfInterest)
            EXPECT_NEAR (magnitude, n / 2.0, 1e-9) << "bin " << bin;
        else
            EXPECT_NEAR (magnitude, 0.0, 1e-9) << "bin " << bin;
    }
}

TEST (Fftw, BatchedRealToComplexKeepsSignalsSeparate)
{
    constexpr int n = 64;
    constexpr int howMany = 4;
    constexpr int bins = n / 2 + 1;

    RealBuffer in (static_cast<std::size_t> (n) * howMany);
    ComplexBuffer out (static_cast<std::size_t> (bins) * howMany);

    const RealToComplexPlan plan (n, howMany, in.data(), out.data(), PlanRigor::Estimate);
    ASSERT_TRUE (plan.isValid());

    // Signal k is a cosine at bin k+1, laid out contiguously.
    for (int k = 0; k < howMany; ++k)
        for (int i = 0; i < n; ++i)
            in[static_cast<std::size_t> (k) * n + i] =
                std::cos (2.0 * std::numbers::pi * (k + 1) * i / n);

    plan.execute (in.data(), out.data());

    for (int k = 0; k < howMany; ++k)
    {
        for (int bin = 0; bin < bins; ++bin)
        {
            const double magnitude =
                std::abs (out[static_cast<std::size_t> (k) * bins + bin]);

            if (bin == k + 1)
                EXPECT_NEAR (magnitude, n / 2.0, 1e-9) << "signal " << k << ", bin " << bin;
            else
                EXPECT_NEAR (magnitude, 0.0, 1e-9) << "signal " << k << ", bin " << bin;
        }
    }
}

TEST (Fftw, ForwardThenBackwardIsIdentityTimesN)
{
    constexpr int n = 128;

    ComplexBuffer original (n);
    ComplexBuffer spectrum (n);
    ComplexBuffer roundTrip (n);

    const ComplexPlan forward (
        n, 1, original.data(), spectrum.data(), Direction::Forward, PlanRigor::Estimate);
    const ComplexPlan backward (
        n, 1, spectrum.data(), roundTrip.data(), Direction::Backward, PlanRigor::Estimate);

    ASSERT_TRUE (forward.isValid());
    ASSERT_TRUE (backward.isValid());

    std::vector<std::complex<double>> reference (n);
    for (int i = 0; i < n; ++i)
    {
        reference[static_cast<std::size_t> (i)] = { std::sin (0.1 * i), std::cos (0.03 * i) };
        original[static_cast<std::size_t> (i)] = reference[static_cast<std::size_t> (i)];
    }

    forward.execute (original.data(), spectrum.data());
    backward.execute (spectrum.data(), roundTrip.data());

    // FFTW's backward transform is unnormalised; the round trip scales by n.
    for (int i = 0; i < n; ++i)
    {
        const auto value = roundTrip[static_cast<std::size_t> (i)] / static_cast<double> (n);
        EXPECT_NEAR (value.real(), reference[static_cast<std::size_t> (i)].real(), 1e-12);
        EXPECT_NEAR (value.imag(), reference[static_cast<std::size_t> (i)].imag(), 1e-12);
    }
}

TEST (Fftw, ParsevalHoldsForTheRealTransform)
{
    constexpr int n = 512;

    RealBuffer in (n);
    ComplexBuffer out (n / 2 + 1);

    const RealToComplexPlan plan (n, 1, in.data(), out.data(), PlanRigor::Estimate);
    ASSERT_TRUE (plan.isValid());

    double timeEnergy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        in[i] = std::sin (0.05 * i) + 0.5 * std::cos (0.31 * i);
        timeEnergy += in[i] * in[i];
    }

    plan.execute (in.data(), out.data());

    // Sum |X_k|^2 over the full spectrum equals n * sum x_i^2. The r2c output
    // holds only the non-negative half, so the interior bins count twice.
    double spectrumEnergy = 0.0;
    for (int bin = 0; bin <= n / 2; ++bin)
    {
        const double magnitudeSquared = std::norm (out[static_cast<std::size_t> (bin)]);
        const bool isSelfConjugate = (bin == 0) || (bin == n / 2);
        spectrumEnergy += isSelfConjugate ? magnitudeSquared : 2.0 * magnitudeSquared;
    }

    EXPECT_NEAR (spectrumEnergy / n, timeEnergy, 1e-8 * timeEnergy);
}
