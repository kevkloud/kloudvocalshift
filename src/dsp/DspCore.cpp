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
        c.prepare (sampleRate, size, maxBlockSize, currentDelivery);

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

    for (auto& b : wetScratch) b.assign (block, 0.0f);
    for (auto& b : dryScratch) b.assign (block, 0.0f);

    mixRamp.assign (block, 0.0f);
    trimRamp.assign (block, 0.0f);
    gainRamp.assign (block, 0.0f);

    // 250 ms. Long enough to average over a phrase rather than ride the
    // syllables, which would turn the level match into a compressor.
    powerCoeff = 1.0 - std::exp (-1.0 / (0.250 * sampleRate));

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
    dryPower = 0.0;
    wetPower = 0.0;
    matchGain = 1.0f;
}

//==============================================================================
bool DspCore::setParams (const Params& p) noexcept
{
    bool latencyChanged = false;

    // Delivery reserves extra buffer, so crossing zero changes the latency. It
    // is the only knob on the panel that does, and it is deliberately the only
    // place the plugin will hiccup: everything else can be swept while playing.
    const auto wantsDelivery = ! p.bypass && p.delivery > 0.0f;

    if (p.window != currentWindow || wantsDelivery != currentDelivery)
    {
        currentWindow = p.window;
        currentDelivery = wantsDelivery;

        rebuild (preparedBlockSize);

        latencyChanged = true;
    }

    // Amount interpolates the ratio toward 1 rather than crossfading the wet
    // path toward the dry. Half a warp is a real thing -- it is what warping to
    // 110 instead of 120 sounds like -- whereas half the wet signal is the
    // original with a blurred copy underneath it, which is a chorus.
    const auto amount = std::clamp (p.amount, 0.0f, 100.0f) * 0.01f;
    const auto effective = p.bypass ? 1.0 : 1.0 + (double) amount * ((double) p.ratio - 1.0);

    const auto lock = std::clamp (p.lock, 0.0f, 100.0f) * 0.01f;
    const auto delivery = std::clamp (p.delivery, 0.0f, 100.0f) * 0.01f;

    for (auto& c : chains)
        c.setWarp (effective,
                   p.bypass ? 0.0 : (double) p.formantSemis,
                   p.bypass ? 1.0 : (double) lock,
                   p.bypass ? 0.0 : (double) delivery);

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

    if ((int) mixRamp.size() < numSamples)
    {
        for (auto& b : wetScratch) b.assign ((size_t) numSamples, 0.0f);
        for (auto& b : dryScratch) b.assign ((size_t) numSamples, 0.0f);

        mixRamp.assign ((size_t) numSamples, 0.0f);
        trimRamp.assign ((size_t) numSamples, 0.0f);
        gainRamp.assign ((size_t) numSamples, 0.0f);
    }

    // Mix and trim glide per sample. Both channels have to walk the same glide,
    // so it is computed once into a ramp rather than ticked inside the channel
    // loop where the second channel would start where the first one finished.
    for (int i = 0; i < numSamples; ++i)
    {
        mixRamp[(size_t) i] = mix.tick();
        trimRamp[(size_t) i] = trim.tick();
    }

    // Pass one: run the chain, and delay the dry to match it. Both have to be
    // finished for every channel before the level match can be worked out,
    // because a per-channel gain would move a stereo image.
    const auto startWrite = dryWrite;

    for (int ch = 0; ch < chans; ++ch)
    {
        auto* data = channels[ch];
        auto& delay = dryDelay[(size_t) ch];
        auto& wet = wetScratch[(size_t) ch];
        auto& dry = dryScratch[(size_t) ch];

        std::copy (data, data + numSamples, wet.begin());

        chains[(size_t) ch].process (wet.data(), numSamples);

        dryWrite = startWrite;

        for (int i = 0; i < numSamples; ++i)
        {
            dry[(size_t) i] = delay[(size_t) dryWrite];
            delay[(size_t) dryWrite] = data[i];

            dryWrite = dryWrite + 1 < dryLength ? dryWrite + 1 : 0;
        }
    }

    // Pass two: the level match, from the summed power of both sides.
    for (int i = 0; i < numSamples; ++i)
    {
        double dryFrame = 0.0, wetFrame = 0.0;

        for (int ch = 0; ch < chans; ++ch)
        {
            const auto d = (double) dryScratch[(size_t) ch][(size_t) i];
            const auto w = (double) wetScratch[(size_t) ch][(size_t) i];

            dryFrame += d * d;
            wetFrame += w * w;
        }

        dryPower += powerCoeff * (dryFrame - dryPower);
        wetPower += powerCoeff * (wetFrame - wetPower);

        // Below the follower's own noise the ratio is meaningless, and a gain
        // worked out from two numbers that are both essentially zero is how a
        // level matcher turns a silent passage into a roar.
        const auto target = (wetPower > 1.0e-12 && dryPower > 1.0e-12)
            ? std::clamp (std::sqrt (dryPower / wetPower), 0.25, 4.0)
            : 1.0;

        gainRamp[(size_t) i] = (float) target;
    }

    for (int ch = 0; ch < chans; ++ch)
    {
        auto* data = channels[ch];
        const auto& wet = wetScratch[(size_t) ch];
        const auto& dry = dryScratch[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            // Linear, not equal-power: the wet path is a processed copy of the
            // dry one rather than an uncorrelated signal, so equal-power runs
            // the middle of the knob about 3 dB hot.
            const auto w = wet[(size_t) i] * gainRamp[(size_t) i];
            const auto d = dry[(size_t) i];

            data[i] = (d + mixRamp[(size_t) i] * (w - d)) * trimRamp[(size_t) i];
        }
    }
}

} // namespace kloudvocalshift
