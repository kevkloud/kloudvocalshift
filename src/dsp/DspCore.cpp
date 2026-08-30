#include "DspCore.h"

namespace kloudvocalshift
{

namespace
{
    /** Quoted at 48 kHz. Normal is 2048, which is 43 ms -- close to what Live's
        own Complex mode works at, and long enough to resolve a male
        fundamental. Short keeps consonants intact at the cost of a weaker
        effect; Long is where it stops sounding like a warp and starts sounding
        like a freeze. */
    constexpr int sizeAt48k (Window w) noexcept
    {
        switch (w)
        {
            case Window::shortWindow: return 1024;
            case Window::longWindow:  return 4096;
            case Window::normal:
            default:                  return 2048;
        }
    }

    inline float dbToGain (float db) noexcept
    {
        return db == 0.0f ? 1.0f : std::pow (10.0f, db * 0.05f);
    }
}

//==============================================================================
int DspCore::windowForRate (Window w) const noexcept
{
    const auto scaled = (double) sizeAt48k (w) * sampleRate / 48000.0;

    int size = 256;

    while (size < 16384 && (double) size * 1.5 < scaled)
        size *= 2;

    return size;
}

void DspCore::rebuild (int maxBlockSize)
{
    const auto size = windowForRate (currentWindow);

    for (auto& c : chains)
        c.prepare (sampleRate, size, maxBlockSize, currentPasses);

    latencySamples = chains[0].getLatencySamples();
    dryLength = std::max (latencySamples, 1);

    for (auto& d : dryDelay)
        d.assign ((size_t) dryLength, 0.0f);

    dryWrite = 0;
}

void DspCore::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    preparedChannels = std::clamp (numChannels, 1, kMaxChannels);

    preparedBlockSize = std::max (maxBlockSize, 1);

    rebuild (preparedBlockSize);

    const auto block = (size_t) std::max (maxBlockSize, 1);

    wetScratch.assign (block, 0.0f);
    mixRamp.assign (block, 0.0f);
    trimRamp.assign (block, 0.0f);

    // Both are per-sample, so the control rate is the sample rate. 20 ms is
    // long enough that a Mix sweep does not zipper and short enough that it
    // still feels like a knob rather than a fade.
    mix.prepare (sampleRate, 20.0);
    trim.prepare (sampleRate, 20.0);

    freshlyPrepared = true;

    reset();
}

void DspCore::reset() noexcept
{
    for (auto& c : chains)
        c.reset();

    for (auto& d : dryDelay)
        std::fill (d.begin(), d.end(), 0.0f);

    dryWrite = 0;
}

//==============================================================================
bool DspCore::setParams (const Params& p) noexcept
{
    bool latencyChanged = false;

    if (p.window != currentWindow || p.passes != currentPasses)
    {
        currentWindow = p.window;
        currentPasses = std::clamp (p.passes, 1, WarpChain::kMaxPasses);

        rebuild (preparedBlockSize);

        latencyChanged = true;
    }

    // Amount interpolates the ratio toward 1 rather than crossfading the wet
    // path toward the dry. Half a warp is a real thing -- it is what warping to
    // 110 instead of 120 sounds like -- whereas half the wet signal is the
    // original with a blurred copy underneath it, which is a chorus.
    const auto amount = std::clamp (p.amount, 0.0f, 100.0f) * 0.01f;
    const auto effective = p.bypass ? 1.0 : 1.0 + (double) amount * ((double) p.ratio - 1.0);

    for (auto& c : chains)
        c.setWarp (effective, p.bypass ? 0.0 : (double) p.formantSemis);

    const auto mixTarget = p.bypass ? 0.0f : std::clamp (p.mixPercent, 0.0f, 100.0f) * 0.01f;
    const auto trimTarget = p.bypass ? 1.0f : dbToGain (std::clamp (p.trimDb, -24.0f, 24.0f));

    if (freshlyPrepared)
    {
        // A plugin restored from a saved set should already be at its stored
        // state, not audibly sliding toward it from the defaults.
        mix.snap (mixTarget);
        trim.snap (trimTarget);
        freshlyPrepared = false;
    }
    else
    {
        mix.setTarget (mixTarget);
        trim.setTarget (trimTarget);
    }

    return latencyChanged;
}

//==============================================================================
void DspCore::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto chans = std::clamp (numChannels, 0, preparedChannels);

    if (chans <= 0 || numSamples <= 0)
        return;

    if ((int) wetScratch.size() < numSamples)
    {
        wetScratch.assign ((size_t) numSamples, 0.0f);
        mixRamp.assign ((size_t) numSamples, 0.0f);
        trimRamp.assign ((size_t) numSamples, 0.0f);
    }

    // Mix and trim glide per sample. Both channels have to walk the same
    // glide, so it is computed once into a ramp and then read, rather than
    // ticked inside the channel loop where the second channel would start
    // where the first one finished.
    for (int i = 0; i < numSamples; ++i)
    {
        mixRamp[(size_t) i] = mix.tick();
        trimRamp[(size_t) i] = trim.tick();
    }

    const auto startWrite = dryWrite;

    for (int ch = 0; ch < chans; ++ch)
    {
        auto* data = channels[ch];
        auto& delay = dryDelay[(size_t) ch];

        std::copy (data, data + numSamples, wetScratch.begin());

        chains[(size_t) ch].process (wetScratch.data(), numSamples);

        dryWrite = startWrite;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto dry = delay[(size_t) dryWrite];
            delay[(size_t) dryWrite] = data[i];

            dryWrite = dryWrite + 1 < dryLength ? dryWrite + 1 : 0;

            // Linear, not equal-power: the wet path is a processed copy of the
            // dry one rather than an uncorrelated signal, so equal-power runs
            // the middle of the knob about 3 dB hot.
            const auto m = mixRamp[(size_t) i];

            data[i] = (dry + m * (wetScratch[(size_t) i] - dry)) * trimRamp[(size_t) i];
        }
    }
}

} // namespace kloudvocalshift
