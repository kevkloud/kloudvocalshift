#include "WarpChain.h"

#include <algorithm>
#include <cmath>

namespace kloudvocalshift
{

void WarpChain::prepare (double sampleRate, int fftSize, int maxBlockSize, int passes)
{
    activePasses = std::clamp (passes, 1, kMaxPasses);

    windowSize = fftSize;
    analysisHop = fftSize / 4;

    for (auto& s : stages)
        s.prepare (sampleRate, fftSize);

    // A stage produces up to a window past what it consumed while it is filling
    // up, and the expanding stage of a pass produces more than it is given, so
    // the hand-off buffers are sized for the worst block rather than the
    // typical one.
    const auto scratch = (size_t) (maxBlockSize * 4 + fftSize * 4);

    ping.assign (scratch, 0.0f);
    pong.assign (scratch, 0.0f);
    fifo.assign (scratch, 0.0f);
    inQueue.assign (scratch, 0.0f);

    rebuild();
    reset();
}

void WarpChain::reset()
{
    for (auto& s : stages)
        s.reset();

    std::fill (fifo.begin(), fifo.end(), 0.0f);
    std::fill (inQueue.begin(), inQueue.end(), 0.0f);

    fifoRead = 0;
    fifoWrite = 0;
    fifoCount = 0;
    inCount = 0;
    underruns = 0;
    padRemaining = 0;

    // Run silence through until every stage is producing as fast as it is being
    // fed, then throw all of it away. A window per stage plus a few hops is
    // comfortably past the point where the last stage has started emitting.
    const std::vector<float> silence ((size_t) analysisHop, 0.0f);

    for (int fed = 0; fed < activeStages * windowSize + 4 * analysisHop; fed += analysisHop)
        pump (silence.data());

    fifoRead = 0;
    fifoWrite = 0;
    fifoCount = 0;
    padRemaining = margin;
}

//==============================================================================
void WarpChain::setWarp (double newRatio, double newFormant) noexcept
{
    ratio = newRatio;
    formantSemis = newFormant;

    // At equal tempos there is nothing to undo, and a phase vocoder run at
    // ratio 1 is not quite the identity -- the per-bin frequency estimates
    // carry a small error that accumulates. Passing the analysis phase through
    // instead makes the chain an exact windowed identity, which is the property
    // that lets this sit on a channel switched on and be provably inaudible
    // until the two tempos differ.
    const auto engaged = (ratio != 1.0);

    for (int i = 0; i < activeStages - 1; ++i)
        stages[(size_t) i].setVocoderPhase (engaged);

    for (int p = 0; p < activePasses; ++p)
    {
        // Stretch by the ratio, then put the length back. Whichever way round
        // the tempo went, both stages run; the sound is not symmetrical about
        // 1, and neither is what people reach for.
        stages[(size_t) (2 * p)].setRatio (1.0 / ratio);
        stages[(size_t) (2 * p + 1)].setRatio (ratio);
    }

    stages[(size_t) (activeStages - 1)].setFormant (formantSemis);
}

void WarpChain::rebuild() noexcept
{
    activeStages = 2 * activePasses + 1;

    for (int i = 0; i < activeStages; ++i)
    {
        auto& s = stages[(size_t) i];

        // The last stage is the formant stage: ratio 1 and the analysis phase
        // passed through, so it is an exact identity at zero shift.
        const auto isFormantStage = (i == activeStages - 1);

        s.setVocoderPhase (! isFormantStage);
        s.setFormant (0.0);

        if (isFormantStage)
            s.setRatio (1.0);
    }

    // A window per stage, plus two hops of margin for the burst jitter between
    // two stages whose ratios are reciprocal but whose frames do not line up.
    // Not start-up -- the priming in reset() has already dealt with that. The
    // figure is exact, and asserted against a measured impulse in the tests,
    // because a plugin that misreports its latency drags everything downstream
    // of it off the grid and the host has no way to notice.
    margin = 2 * analysisHop;
    latency = activeStages * windowSize + margin;
}

//==============================================================================
void WarpChain::fifoPush (const float* data, int n)
{
    if (fifoCount + n > (int) fifo.size())
        fifo.resize ((size_t) (fifoCount + n) * 2, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        fifo[(size_t) fifoWrite] = data[i];
        fifoWrite = (fifoWrite + 1) % (int) fifo.size();
    }

    fifoCount += n;
}

void WarpChain::fifoPop (float* data, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        if (padRemaining > 0)
        {
            --padRemaining;
            data[i] = 0.0f;
            continue;
        }

        if (fifoCount <= 0)
        {
            // Not supposed to be reachable. Counted rather than ignored,
            // because if it ever happens it moves everything downstream of it
            // off the grid by a sample and nothing else would show that.
            ++underruns;
            data[i] = 0.0f;
            continue;
        }

        data[i] = fifo[(size_t) fifoRead];
        fifoRead = (fifoRead + 1) % (int) fifo.size();
        --fifoCount;
    }
}

//==============================================================================
void WarpChain::pump (const float* chunk)
{
    int count = analysisHop;
    std::copy (chunk, chunk + analysisHop, ping.begin());

    for (int i = 0; i < activeStages; ++i)
    {
        stages[(size_t) i].push (ping.data(), count);

        const auto ready = stages[(size_t) i].available();

        if ((int) pong.size() < ready)
            pong.resize ((size_t) ready * 2, 0.0f);

        count = stages[(size_t) i].pop (pong.data(), ready);

        ping.swap (pong);
    }

    fifoPush (ping.data(), count);
}

void WarpChain::process (float* samples, int numSamples)
{
    if (windowSize <= 0 || numSamples <= 0)
        return;

    if ((int) ping.size() < numSamples * 4 + windowSize * 4)
    {
        const auto scratch = (size_t) (numSamples * 4 + windowSize * 4);
        ping.assign (scratch, 0.0f);
        pong.assign (scratch, 0.0f);
    }

    if ((int) inQueue.size() < inCount + numSamples)
        inQueue.resize ((size_t) (inCount + numSamples) * 2, 0.0f);

    std::copy (samples, samples + numSamples, inQueue.begin() + inCount);
    inCount += numSamples;

    int consumed = 0;

    while (inCount - consumed >= analysisHop)
    {
        pump (inQueue.data() + consumed);
        consumed += analysisHop;
    }

    if (consumed > 0)
    {
        std::copy (inQueue.begin() + consumed, inQueue.begin() + inCount, inQueue.begin());
        inCount -= consumed;
    }

    fifoPop (samples, numSamples);
}

} // namespace kloudvocalshift
