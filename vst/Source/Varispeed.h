#pragma once

// Vinyl-style varispeed resampler. Dependency-free on purpose so it can be unit-tested
// without JUCE (see vst/tests/varispeed_test.cpp).
//
// Model: a circular buffer written at 1 sample/sample and read at `rate` samples/sample.
//   rate = rpm / ref_rpm  (45 rpm reference => 1.0 = unity).
// rate < 1 pitches down and lets the read pointer fall behind the write pointer (latency
// grows) exactly like physically slowing a turntable; the lag is clamped to the buffer so
// it can never read unwritten samples or run unbounded. 4-point Catmull-Rom interpolation.

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

class Varispeed
{
public:
    void prepare (double sampleRate, double maxLatencySeconds = 4.0)
    {
        const auto needed = (size_t) std::ceil (std::max (1.0, sampleRate) * maxLatencySeconds);
        size_t n = 1;
        while (n < needed) n <<= 1;          // round up to a power of two for cheap masking
        size_  = n;
        mask_  = (int64_t) n - 1;
        buffer_.assign (size_, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        writeCount_ = 0;
        readPos_    = 0.0;
        std::fill (buffer_.begin(), buffer_.end(), 0.0f);
    }

    // Push one input sample, return one output sample read at the current rate.
    float processSample (float x, double rate) noexcept
    {
        buffer_[(size_t) ((int64_t) writeCount_ & mask_)] = x;
        ++writeCount_;

        const double newest = (double) (writeCount_ - 1);
        const double minLag  = 4.0;                          // interp headroom
        const double maxLag  = (double) size_ - 8.0;         // never lap the write pointer
        const double lag     = newest - readPos_;
        if (lag < minLag)       readPos_ = newest - minLag;  // buffer drained (rate > 1)
        else if (lag > maxLag)  readPos_ = newest - maxLag;  // buffer overfull (slow too long)

        const float out = readHermite (readPos_);
        readPos_ += (rate > 0.0 ? rate : 0.0);
        return out;
    }

private:
    inline size_t wrap (int64_t i) const noexcept
    {
        // Two's-complement AND with (2^k - 1) gives the correct positive modulo, including
        // for negative indices that occur before the buffer has filled.
        return (size_t) (i & mask_);
    }

    float readHermite (double pos) const noexcept
    {
        const int64_t i1 = (int64_t) std::floor (pos);
        const float   f  = (float) (pos - (double) i1);
        const float xm1 = buffer_[wrap (i1 - 1)];
        const float x0  = buffer_[wrap (i1    )];
        const float x1  = buffer_[wrap (i1 + 1)];
        const float x2  = buffer_[wrap (i1 + 2)];

        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    std::vector<float> buffer_;
    size_t             size_  = 0;
    int64_t            mask_  = 0;
    int64_t            writeCount_ = 0;
    double             readPos_    = 0.0;
};
