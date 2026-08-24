/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

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
#include "PngWriter.h"

#include "RfMath/BackProjection.h"
#include "RfMath/ResponseProfile.h"
#include "RfMath/RfMetrics.h"
#include "RfMath/RfSimulator.h"
#include "RfMath/StimulusGeometry.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace EventTriggered::Rf;
using namespace EventTriggered::Rf::Tools;

namespace
{

struct Options
{
    double rfCentreXDeg = 4.0;
    double rfCentreYDeg = -3.0;
    double rfDiameterDeg = 4.0;
    double spikeProbability = 0.3;
    double latencyMs = 60.0;
    double directionSelectivity = 0.0;
    double preferredDirectionDeg = 90.0;

    int directions = 8;
    int trials = 10;
    int pixels = 201;
    double degreesPerPixel = 0.1;

    bool scanLatency = false;
    std::uint64_t seed = 20140912;
    std::string outPath = "rf_map.png";
};

void printUsage()
{
    std::puts (
        "rf_demo — renders a back-projection receptive-field map from simulated data.\n"
        "\n"
        "Nothing here touches the Open Ephys GUI: it runs the same rf_math code the\n"
        "plugin runs, on the paper's own simulation, and writes the result as a PNG.\n"
        "That makes the algorithm inspectable before any GUI code exists, and lets the\n"
        "output be compared against the figures in Fiorani et al. (2014) by eye.\n"
        "\n"
        "Options:\n"
        "  --rf-centre X,Y      RF centre in degrees      (default 4,-3)\n"
        "  --rf-size D          RF diameter in degrees    (default 4)\n"
        "  --directions N       number of sweep directions(default 8)\n"
        "  --trials N           trials per direction      (default 10)\n"
        "  --p P                peak spike probability    (default 0.3)\n"
        "  --latency MS         true neuronal latency     (default 60)\n"
        "  --selectivity S      direction selectivity 0-1 (default 0)\n"
        "  --preferred DEG      preferred direction       (default 90)\n"
        "  --pixels N           map size in pixels, odd   (default 201)\n"
        "  --deg-per-pixel D    map resolution            (default 0.1)\n"
        "  --scan-latency       estimate latency instead of using the true value\n"
        "  --seed N             noise seed                (default 20140912)\n"
        "  --out PATH           output PNG                (default rf_map.png)\n");
}

bool parseDouble (std::string_view text, double& out)
{
    try
    {
        out = std::stod (std::string (text));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parseOptions (int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const auto next = [&] () -> std::string_view {
            return i + 1 < argc ? std::string_view (argv[++i]) : std::string_view {};
        };

        if (arg == "--help" || arg == "-h")
            return false;
        else if (arg == "--rf-centre" || arg == "--rf-center")
        {
            const std::string value { next() };
            const auto comma = value.find (',');
            if (comma == std::string::npos)
                return false;
            if (! parseDouble (value.substr (0, comma), options.rfCentreXDeg)
                || ! parseDouble (value.substr (comma + 1), options.rfCentreYDeg))
                return false;
        }
        else if (arg == "--rf-size")
            parseDouble (next(), options.rfDiameterDeg);
        else if (arg == "--directions")
            options.directions = std::stoi (std::string (next()));
        else if (arg == "--trials")
            options.trials = std::stoi (std::string (next()));
        else if (arg == "--p")
            parseDouble (next(), options.spikeProbability);
        else if (arg == "--latency")
            parseDouble (next(), options.latencyMs);
        else if (arg == "--selectivity")
            parseDouble (next(), options.directionSelectivity);
        else if (arg == "--preferred")
            parseDouble (next(), options.preferredDirectionDeg);
        else if (arg == "--pixels")
            options.pixels = std::stoi (std::string (next()));
        else if (arg == "--deg-per-pixel")
            parseDouble (next(), options.degreesPerPixel);
        else if (arg == "--scan-latency")
            options.scanLatency = true;
        else if (arg == "--seed")
            options.seed = std::stoull (std::string (next()));
        else if (arg == "--out")
            options.outPath = std::string (next());
        else
        {
            std::printf ("unknown option: %.*s\n\n", static_cast<int> (arg.size()), arg.data());
            return false;
        }
    }

    return true;
}

/** Draws the map, plus a cross at the true RF centre and a ring at the recovered
 *  border. Seeing where the estimate landed relative to the truth is the whole
 *  point of the tool; a bare heat map cannot show an error. */
void render (const Map2D& map,
             const RfEstimate& estimate,
             const Options& options,
             const std::string& path)
{
    const int n = map.pixels();
    std::vector<std::uint8_t> rgb (static_cast<std::size_t> (n) * n * 3, 0);

    float lowest = map.values().front();
    float highest = lowest;
    for (const float v : map.values())
    {
        lowest = std::min (lowest, v);
        highest = std::max (highest, v);
    }
    const double range = std::max (1e-9, static_cast<double> (highest - lowest));

    for (int row = 0; row < n; ++row)
    {
        for (int col = 0; col < n; ++col)
        {
            const double t = (map.at (row, col) - lowest) / range;
            const std::size_t offset = (static_cast<std::size_t> (row) * n + col) * 3;
            jetColour (t, rgb[offset], rgb[offset + 1], rgb[offset + 2]);
        }
    }

    const auto paint = [&] (int row, int col, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        if (row < 0 || row >= n || col < 0 || col >= n)
            return;
        const std::size_t offset = (static_cast<std::size_t> (row) * n + col) * 3;
        rgb[offset] = r;
        rgb[offset + 1] = g;
        rgb[offset + 2] = b;
    };

    const MapGeometry& geometry = map.geometry();
    const auto colOf = [&] (double x) {
        return geometry.centreIndex() + static_cast<int> (std::lround ((x - geometry.centreXDeg) / geometry.degreesPerPixel));
    };
    const auto rowOf = [&] (double y) {
        return geometry.centreIndex() - static_cast<int> (std::lround ((y - geometry.centreYDeg) / geometry.degreesPerPixel));
    };

    // White cross: where the RF actually is.
    const int trueRow = rowOf (options.rfCentreYDeg);
    const int trueCol = colOf (options.rfCentreXDeg);
    for (int d = -6; d <= 6; ++d)
    {
        paint (trueRow, trueCol + d, 255, 255, 255);
        paint (trueRow + d, trueCol, 255, 255, 255);
    }

    // Black ring: the recovered border, at 0.76 of the peak.
    if (estimate.equivalentDiameterDeg > 0.0)
    {
        const double radiusPixels = 0.5 * estimate.equivalentDiameterDeg / geometry.degreesPerPixel;
        const int estRow = rowOf (estimate.centreYDeg);
        const int estCol = colOf (estimate.centreXDeg);

        for (int i = 0; i < 1440; ++i)
        {
            const double theta = 2.0 * 3.14159265358979323846 * i / 1440.0;
            paint (estRow - static_cast<int> (std::lround (radiusPixels * std::sin (theta))),
                   estCol + static_cast<int> (std::lround (radiusPixels * std::cos (theta))),
                   0, 0, 0);
        }
    }

    writePng (path, n, n, rgb);
}

} // namespace

int main (int argc, char** argv)
{
    Options options;

    if (! parseOptions (argc, argv, options))
    {
        printUsage();
        return 1;
    }

    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = options.rfCentreXDeg;
    neuron.rfCentreYDeg = options.rfCentreYDeg;
    neuron.rfDiameterDeg = options.rfDiameterDeg;
    neuron.peakSpikeProbability = options.spikeProbability;
    neuron.latencyMs = options.latencyMs;
    neuron.directionSelectivity = options.directionSelectivity;
    neuron.preferredDirectionDeg = options.preferredDirectionDeg;

    SimulationSettings settings;
    settings.sampleRateHz = 1000.0;
    settings.preSamples = 300;
    settings.postSamples = 3000;
    settings.trialsPerDirection = options.trials;
    settings.seed = options.seed;
    settings.sweep.speedDegPerSec = 10.0;
    settings.sweep.sweepStartDeg = -15.0;

    const std::vector<double> angles = evenlySpacedAngles (options.directions);

    for (const AngleSetWarning warning : checkAngleSet (angles))
        std::printf ("warning: %s\n", describe (warning).c_str());

    const std::vector<std::vector<float>> traces = simulateAllDirections (neuron, settings, angles);

    ProfileOptions profileOptions;
    profileOptions.zScore.source = BaselineSource::PreTrigger;
    profileOptions.zScore.preTriggerSamples = settings.preSamples;
    profileOptions.smoothingSigmaMs =
        (options.rfDiameterDeg / 4.0 / settings.sweep.speedDegPerSec) * 1000.0;

    MapGeometry geometry;
    geometry.pixels = options.pixels;
    geometry.degreesPerPixel = options.degreesPerPixel;

    // Built with no latency correction, so the scan can apply candidates itself.
    std::vector<SpatialProfile> zeroLatencyProfiles;
    std::vector<double> speeds;

    for (std::size_t i = 0; i < angles.size(); ++i)
    {
        SweepGeometry sweep = settings.sweep;
        sweep.angleDeg = angles[i];
        sweep.latencyMs = 0.0;

        zeroLatencyProfiles.push_back (
            makeProfile (traces[i], settings.sampleRateHz, settings.preSamples, sweep, profileOptions));
        speeds.push_back (sweep.speedDegPerSec);
    }

    double latencyMs = options.latencyMs;

    if (options.scanLatency)
    {
        const LatencyScanResult scan =
            scanLatency (zeroLatencyProfiles, speeds, geometry, 0.0, 200.0, 2.0);
        latencyMs = scan.bestLatencyMs;
        std::printf ("latency scan: %.1f ms estimated, %.1f ms true (peak %.3f)\n",
                     latencyMs, options.latencyMs, static_cast<double> (scan.bestPeak));
    }

    std::vector<SpatialProfile> profiles = zeroLatencyProfiles;
    for (std::size_t i = 0; i < profiles.size(); ++i)
        profiles[i].startDeg -= (latencyMs / 1000.0) * speeds[i];

    const Map2D map = backProject (profiles, geometry);
    const RfEstimate estimate = estimateRf (map);

    const double centreError = std::hypot (estimate.centreXDeg - options.rfCentreXDeg,
                                           estimate.centreYDeg - options.rfCentreYDeg);
    const double sizeError =
        (estimate.equivalentDiameterDeg - options.rfDiameterDeg) / options.rfDiameterDeg;

    std::printf ("\n%d directions, %d trials, p = %.2f\n",
                 options.directions, options.trials, options.spikeProbability);
    std::printf ("true   centre (%+.2f, %+.2f)  diameter %.2f deg\n",
                 options.rfCentreXDeg, options.rfCentreYDeg, options.rfDiameterDeg);
    std::printf ("mapped centre (%+.2f, %+.2f)  diameter %.2f deg  peak z = %.2f\n",
                 estimate.centreXDeg, estimate.centreYDeg,
                 estimate.equivalentDiameterDeg, static_cast<double> (estimate.peak));
    std::printf ("centre error %.1f%% of RF radius, size error %+.1f%%\n",
                 100.0 * centreError / (options.rfDiameterDeg / 2.0), 100.0 * sizeError);

    const std::vector<float> responses =
        directionResponses (profiles, estimate.centreXDeg, estimate.centreYDeg);

    if (const auto index = directionSelectivityIndex (responses, angles))
        std::printf ("direction selectivity %.3f", *index);
    if (const auto index = orientationSelectivityIndex (responses, angles))
        std::printf (", orientation selectivity %.3f", *index);
    std::printf ("\n");

    render (map, estimate, options, options.outPath);
    std::printf ("wrote %s\n", options.outPath.c_str());

    return 0;
}
