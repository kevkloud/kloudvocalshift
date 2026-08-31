#pragma once

#include "WarpChain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace kloudvocalshift
{

/** One-pole smoother that snaps to its target once it is close enough, so a
    settled Mix of 0 % compares exactly equal to zero and the dry path can be
    handed through untouched rather than crossfaded with itself. */
class Smoother
{
public:
    void prepare (double controlRateHz, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (controlRateHz, 1.0) * tau)));
    }

    void snap (float v) noexcept      { current = target = v; }
    void setTarget (float t) noexcept { target = t; }
    float value() const noexcept      { return current; }

    float tick() noexcept
    {
        current += coeff * (target - current);

        if (std::abs (target - current) < 1.0e-6f)
            current = target;

        return current;
    }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** Everything the plugin does to audio, with no dependency on JUCE or on a
    host, so the tests and the offline harness drive the real signal path.

    The dry path is delayed to match the vocoder exactly. An undelayed dry would
    comb against the wet at every intermediate Mix setting, and since the whole
    point of this plugin is a subtle change in body, a comb filter sitting on
    top of it would be indistinguishable from the effect.
*/
class DspCore
{
public:
    static constexpr int kMaxChannels = 2;

    struct Params
    {
        float ratio        = 1.0f;     // playing tempo / recorded tempo
        float amount       = 100.0f;   // percent of that ratio actually applied
        int   passes       = 1;
        float formantSemis = 0.0f;
        float mixPercent   = 100.0f;
        float trimDb       = 0.0f;
        bool  bypass       = false;
        Window window      = Window::normal;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    int getLatencySamples() const noexcept { return latencySamples; }
    int getWindow() const noexcept         { return chains[0].getWindow(); }
    double getSampleRate() const noexcept  { return sampleRate; }

    /** Should stay at zero. An underrun moves everything after it a sample off
        the grid, and nothing but this counter would show it. */
    int getUnderruns() const noexcept
    {
        return chains[0].getUnderruns() + chains[1].getUnderruns();
    }

    /** Returns true if the window size changed and the host needs to be told
        about the new latency. */
    bool setParams (const Params&) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

private:
    void rebuild (int maxBlockSize);

    /** Window sizes are quoted at 48 kHz. What should stay constant across
        session rates is the *time* the window spans -- that is what decides how
        far a transient is smeared -- so the sample count is scaled and rounded
        to a power of two for the radix-2 transform. */
    int windowForRate (Window) const noexcept;

    std::array<WarpChain, kMaxChannels> chains;
    std::array<std::vector<float>, kMaxChannels> dryDelay;

    std::vector<float> wetScratch, mixRamp, trimRamp;

    int dryWrite = 0;
    int dryLength = 0;

    double sampleRate = 48000.0;
    int latencySamples = 0;
    int preparedChannels = 0;
    Window currentWindow = Window::normal;
    int currentPasses = 1;
    int preparedBlockSize = 512;

    Smoother mix, trim;

    bool freshlyPrepared = true;
};

} // namespace kloudvocalshift
