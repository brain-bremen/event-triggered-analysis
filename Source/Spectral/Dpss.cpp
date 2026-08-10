/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Dpss.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

namespace TriggeredSpectra
{

// --- TaperBank -------------------------------------------------------------

void TaperBank::setSize (int numTapers, int length)
{
    m_numTapers = std::max (0, numTapers);
    m_length = std::max (0, length);
    m_data.assign (static_cast<std::size_t> (m_numTapers) * m_length, 0.0);
}

double TaperBank::sumOfSquares (int index) const
{
    if (index < 0 || index >= m_numTapers)
        return 0.0;

    const auto w = taper (index);
    return std::inner_product (w.begin(), w.end(), w.begin(), 0.0);
}

TaperBank makeHannTaper (int length)
{
    TaperBank bank (1, length);

    if (length <= 0)
        return bank;

    const auto w = bank.taper (0);

    for (int i = 0; i < length; ++i)
        w[static_cast<std::size_t> (i)] =
            0.5 * (1.0 - std::cos (2.0 * std::numbers::pi * i / length));

    return bank;
}

// --- Symmetric tridiagonal eigensolver -------------------------------------

namespace
{

/** Number of eigenvalues of the symmetric tridiagonal matrix strictly below x.
 *
 *  Sturm sequence: counts the sign changes of the leading principal minors, which
 *  equals the number of eigenvalues below x. The guard against a vanishing pivot
 *  keeps the recurrence finite when x lands exactly on an eigenvalue of a leading
 *  submatrix.
 */
int countEigenvaluesBelow (const std::vector<double>& diagonal,
                           const std::vector<double>& offDiagonal,
                           double x)
{
    const auto n = diagonal.size();

    constexpr double tiny = std::numeric_limits<double>::min() * 1024.0;

    int count = 0;
    double q = diagonal[0] - x;

    if (q < 0.0)
        ++count;

    for (std::size_t i = 1; i < n; ++i)
    {
        if (std::abs (q) < tiny)
            q = (q < 0.0) ? -tiny : tiny;

        q = (diagonal[i] - x) - offDiagonal[i] * offDiagonal[i] / q;

        if (q < 0.0)
            ++count;
    }

    return count;
}

/** The k-th smallest eigenvalue (0-based), by bisection on the Sturm count. */
double bisectEigenvalue (const std::vector<double>& diagonal,
                         const std::vector<double>& offDiagonal,
                         int k,
                         double lower,
                         double upper)
{
    const double scale = std::max (std::abs (lower), std::abs (upper));
    const double tolerance = std::numeric_limits<double>::epsilon() * std::max (1.0, scale) * 4.0;

    // ~60 halvings takes any Gershgorin interval below double precision; the
    // width test normally exits well before that.
    for (int iteration = 0; iteration < 200 && (upper - lower) > tolerance; ++iteration)
    {
        const double middle = lower + 0.5 * (upper - lower);

        if (middle <= lower || middle >= upper)
            break; // interval has collapsed to adjacent representable doubles

        if (countEigenvaluesBelow (diagonal, offDiagonal, middle) > k)
            upper = middle;
        else
            lower = middle;
    }

    return lower + 0.5 * (upper - lower);
}

/** Solves (T - shift I) x = rhs in place, by Gaussian elimination with row
 *  interchanges. Partial pivoting on a tridiagonal creates one extra
 *  superdiagonal of fill-in, which is what `secondSuper` holds.
 *
 *  Zero pivots are replaced by a small multiple of eps: for inverse iteration an
 *  exactly singular factor is the *expected* case (the shift is an eigenvalue),
 *  and the resulting huge solution is normalised away.
 */
void solveShiftedTridiagonal (const std::vector<double>& diagonal,
                              const std::vector<double>& offDiagonal,
                              double shift,
                              std::vector<double>& rhs,
                              std::vector<double>& sub,
                              std::vector<double>& main,
                              std::vector<double>& super,
                              std::vector<double>& secondSuper)
{
    const auto n = diagonal.size();

    const double norm = std::accumulate (
        diagonal.begin(), diagonal.end(), 0.0, [] (double acc, double v) { return acc + std::abs (v); });
    const double tiny = std::numeric_limits<double>::epsilon() * std::max (1.0, norm / static_cast<double> (n));

    for (std::size_t i = 0; i < n; ++i)
    {
        main[i] = diagonal[i] - shift;
        sub[i] = (i + 1 < n) ? offDiagonal[i + 1] : 0.0;
        super[i] = (i + 1 < n) ? offDiagonal[i + 1] : 0.0;
        secondSuper[i] = 0.0;
    }

    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        if (std::abs (main[i]) >= std::abs (sub[i]))
        {
            // No interchange.
            if (std::abs (main[i]) < tiny)
                main[i] = tiny;

            const double factor = sub[i] / main[i];
            main[i + 1] -= factor * super[i];
            rhs[i + 1] -= factor * rhs[i];
            sub[i] = 0.0;
        }
        else
        {
            // Interchange rows i and i+1; the swap pushes a nonzero into the
            // second superdiagonal of row i.
            const double factor = main[i] / sub[i];

            main[i] = sub[i];
            const double temporary = main[i + 1];
            main[i + 1] = super[i] - factor * temporary;
            super[i] = temporary;

            if (i + 2 < n)
            {
                secondSuper[i] = super[i + 1];
                super[i + 1] = -factor * secondSuper[i];
            }

            std::swap (rhs[i], rhs[i + 1]);
            rhs[i + 1] -= factor * rhs[i];
        }
    }

    if (std::abs (main[n - 1]) < tiny)
        main[n - 1] = tiny;

    // Back substitution over the (at most) three surviving diagonals.
    for (std::size_t step = 0; step < n; ++step)
    {
        const std::size_t i = n - 1 - step;

        double value = rhs[i];

        if (i + 1 < n)
            value -= super[i] * rhs[i + 1];
        if (i + 2 < n)
            value -= secondSuper[i] * rhs[i + 2];

        rhs[i] = value / main[i];
    }
}

/** Eigenvector for a known, well-separated eigenvalue, by inverse iteration. */
std::vector<double> inverseIteration (const std::vector<double>& diagonal,
                                      const std::vector<double>& offDiagonal,
                                      double eigenvalue,
                                      int seed)
{
    const auto n = diagonal.size();

    std::vector<double> x (n);
    std::vector<double> sub (n), main (n), super (n), secondSuper (n);

    // A deterministic but non-degenerate start. A constant vector would be
    // orthogonal to every odd-order taper and the iteration would stall.
    for (std::size_t i = 0; i < n; ++i)
        x[i] = std::sin (static_cast<double> ((i + 1) * (seed + 1)) * 0.7390851332151607);

    for (int iteration = 0; iteration < 8; ++iteration)
    {
        solveShiftedTridiagonal (
            diagonal, offDiagonal, eigenvalue, x, sub, main, super, secondSuper);

        const double norm = std::sqrt (std::inner_product (x.begin(), x.end(), x.begin(), 0.0));

        if (! (norm > 0.0) || ! std::isfinite (norm))
            break;

        for (auto& value : x)
            value /= norm;
    }

    return x;
}

} // namespace

// --- Dpss ------------------------------------------------------------------

int Dpss::defaultNumTapers (double timeBandwidth)
{
    return std::max (1, static_cast<int> (std::floor (2.0 * timeBandwidth)) - 1);
}

TaperBank Dpss::compute (int length, double timeBandwidth, int numTapers)
{
    if (length < 2 || numTapers < 1 || ! (timeBandwidth > 0.0))
        return {};

    // More tapers than samples is meaningless, and beyond 2*NW concentration has
    // collapsed anyway.
    numTapers = std::min (numTapers, length);

    const auto n = static_cast<std::size_t> (length);
    const double halfBandwidth = timeBandwidth / static_cast<double> (length);

    std::vector<double> diagonal (n);
    std::vector<double> offDiagonal (n, 0.0);

    const double cosTerm = std::cos (2.0 * std::numbers::pi * halfBandwidth);

    for (std::size_t i = 0; i < n; ++i)
    {
        const double centred = (static_cast<double> (length - 1) - 2.0 * static_cast<double> (i)) / 2.0;
        diagonal[i] = centred * centred * cosTerm;
    }

    // offDiagonal[i] couples rows i-1 and i, so index 0 is unused.
    for (std::size_t i = 1; i < n; ++i)
        offDiagonal[i] = static_cast<double> (i) * static_cast<double> (n - i) / 2.0;

    // Gershgorin bounds on the whole spectrum.
    double lower = std::numeric_limits<double>::max();
    double upper = std::numeric_limits<double>::lowest();

    for (std::size_t i = 0; i < n; ++i)
    {
        const double radius =
            std::abs (offDiagonal[i]) + (i + 1 < n ? std::abs (offDiagonal[i + 1]) : 0.0);

        lower = std::min (lower, diagonal[i] - radius);
        upper = std::max (upper, diagonal[i] + radius);
    }

    // Widen slightly so the true extremes are strictly inside the bracket.
    const double margin = std::max (1.0, std::abs (upper - lower)) * 1e-9;
    lower -= margin;
    upper += margin;

    TaperBank bank (numTapers, length);

    // Taper k corresponds to the k-th *largest* eigenvalue of T.
    for (int k = 0; k < numTapers; ++k)
    {
        const int ascendingIndex = length - 1 - k;

        const double eigenvalue =
            bisectEigenvalue (diagonal, offDiagonal, ascendingIndex, lower, upper);

        std::vector<double> vector = inverseIteration (diagonal, offDiagonal, eigenvalue, k);

        // Sign convention, matching scipy.signal.windows.dpss.
        if (k % 2 == 0)
        {
            // Even order: symmetric, so fix the sign of the sum.
            if (std::accumulate (vector.begin(), vector.end(), 0.0) < 0.0)
                for (auto& value : vector)
                    value = -value;
        }
        else
        {
            // Odd order: antisymmetric and sums to ~0, so fix the first lobe.
            const auto half = vector.begin() + static_cast<std::ptrdiff_t> (n / 2);
            const auto peak = std::max_element (
                vector.begin(), half, [] (double a, double b) { return std::abs (a) < std::abs (b); });

            if (peak != half && *peak < 0.0)
                for (auto& value : vector)
                    value = -value;
        }

        std::copy (vector.begin(), vector.end(), bank.taper (k).begin());
    }

    return bank;
}

std::vector<double> Dpss::concentrations (const TaperBank& tapers, double timeBandwidth)
{
    const int n = tapers.length();
    const int k = tapers.numTapers();

    if (n < 1 || k < 1)
        return {};

    const double halfBandwidth = timeBandwidth / static_cast<double> (n);

    // Sinc kernel row: A[m][j] = sin(2 pi W (m-j)) / (pi (m-j)), A[m][m] = 2W.
    // Only the difference matters, so one vector of length n suffices.
    std::vector<double> kernel (static_cast<std::size_t> (n));
    kernel[0] = 2.0 * halfBandwidth;

    for (int d = 1; d < n; ++d)
        kernel[static_cast<std::size_t> (d)] =
            std::sin (2.0 * std::numbers::pi * halfBandwidth * d) / (std::numbers::pi * d);

    std::vector<double> result (static_cast<std::size_t> (k), 0.0);

    for (int taperIndex = 0; taperIndex < k; ++taperIndex)
    {
        const auto w = tapers.taper (taperIndex);

        double quadraticForm = 0.0;

        for (int m = 0; m < n; ++m)
        {
            double row = 0.0;

            for (int j = 0; j < n; ++j)
                row += kernel[static_cast<std::size_t> (std::abs (m - j))]
                       * w[static_cast<std::size_t> (j)];

            quadraticForm += w[static_cast<std::size_t> (m)] * row;
        }

        result[static_cast<std::size_t> (taperIndex)] = quadraticForm;
    }

    return result;
}

} // namespace TriggeredSpectra
