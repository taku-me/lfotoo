// Dependency-free checks for the varispeed resampler (no JUCE needed).
//   g++ -std=c++17 -I../Source varispeed_test.cpp -o vt && ./vt

#include "../Source/Varispeed.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++failures; std::printf("FAIL %s\n", msg); } \
                              else { std::printf("PASS %s\n", msg); } } while (0)

// Estimate fundamental frequency by counting positive-going zero crossings.
static double estimateHz (const std::vector<float>& x, double sr)
{
    int crossings = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f) ++crossings;
    const double seconds = (double) x.size() / sr;
    return crossings / seconds;
}

int main()
{
    const double sr = 48000.0;
    const double f0 = 1000.0;
    const int    N  = (int) sr; // 1 second

    // Unity rate must pass a sine through essentially unchanged in pitch.
    {
        Varispeed v; v.prepare (sr);
        std::vector<float> out;
        for (int n = 0; n < N; ++n)
        {
            const float in = (float) std::sin (2.0 * M_PI * f0 * n / sr);
            out.push_back (v.processSample (in, 1.0));
        }
        const double hz = estimateHz (out, sr);
        CHECK (std::abs (hz - f0) < 20.0, "unity rate preserves pitch (~1000 Hz)");
        bool finite = true; for (float s : out) finite &= std::isfinite (s);
        CHECK (finite, "unity output is finite");
    }

    // 33/45 rate (0.7407) must pitch DOWN by ~5.36 semitones => ~0.7407 * 1000 Hz.
    {
        const double rate = 33.333 / 45.0;
        Varispeed v; v.prepare (sr);
        std::vector<float> out;
        for (int n = 0; n < N; ++n)
        {
            const float in = (float) std::sin (2.0 * M_PI * f0 * n / sr);
            out.push_back (v.processSample (in, rate));
        }
        const double hz = estimateHz (out, sr);
        const double expected = f0 * rate;
        std::printf ("  measured %.1f Hz, expected ~%.1f Hz\n", hz, expected);
        CHECK (std::abs (hz - expected) < 30.0, "33/45 rate pitches down to ~740 Hz");
    }

    // Stress: random rate jumps between the two states must never produce NaN/Inf or blow up.
    {
        Varispeed v; v.prepare (sr);
        bool ok = true; float peak = 0.0f;
        for (int n = 0; n < N * 4; ++n)
        {
            const float in = (float) std::sin (2.0 * M_PI * f0 * n / sr);
            const double rate = (n / 137) % 2 ? 1.0 : (33.333 / 45.0); // hard snaps
            const float o = v.processSample (in, rate);
            ok &= std::isfinite (o);
            peak = std::max (peak, std::fabs (o));
        }
        CHECK (ok, "hard rate snaps stay finite");
        CHECK (peak < 2.0f, "hard rate snaps stay bounded (<2.0)");
    }

    std::printf ("\n%s\n", failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}
