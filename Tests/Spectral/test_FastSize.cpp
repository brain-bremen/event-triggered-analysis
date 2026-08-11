/*
    Tests for nextFastSize / isFastSize.
*/
#include "Spectral/FastSize.h"

#include <gtest/gtest.h>

using namespace EventTriggered;

TEST (FastSize, RecognisesSevenSmoothNumbers)
{
    EXPECT_TRUE (isFastSize (1));
    EXPECT_TRUE (isFastSize (2));
    EXPECT_TRUE (isFastSize (1024));
    EXPECT_TRUE (isFastSize (2 * 3 * 5 * 7));
    EXPECT_TRUE (isFastSize (2048));

    EXPECT_FALSE (isFastSize (11));
    EXPECT_FALSE (isFastSize (13 * 4));
    EXPECT_FALSE (isFastSize (1021)); // prime
    EXPECT_FALSE (isFastSize (0));
    EXPECT_FALSE (isFastSize (-8));
}

TEST (FastSize, ReturnsSmallestFastSizeAtOrAboveN)
{
    EXPECT_EQ (nextFastSize (1000), 1000); // 2^3 * 5^3
    EXPECT_EQ (nextFastSize (1024), 1024);
    EXPECT_EQ (nextFastSize (1021), 1024);
    EXPECT_EQ (nextFastSize (1025), 1029); // 3 * 7^3
    EXPECT_EQ (nextFastSize (11), 12);
}

TEST (FastSize, ResultIsAlwaysFastAndNeverShrinks)
{
    for (int n = 1; n <= 5000; ++n)
    {
        const int fast = nextFastSize (n);

        ASSERT_GE (fast, n) << "n = " << n;
        ASSERT_TRUE (isFastSize (fast)) << "n = " << n << " -> " << fast;

        // Minimality: nothing in [n, fast) may be fast.
        for (int candidate = n; candidate < fast; ++candidate)
            ASSERT_FALSE (isFastSize (candidate)) << "n = " << n << ", candidate " << candidate;
    }
}

TEST (FastSize, PassesThroughDegenerateInputs)
{
    EXPECT_EQ (nextFastSize (0), 0);
    EXPECT_EQ (nextFastSize (1), 1);
    EXPECT_EQ (nextFastSize (-5), -5);
}
