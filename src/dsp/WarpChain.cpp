#include "WarpChain.h"

#include <algorithm>
#include <cmath>

namespace kloudvocalshift
{

void WarpChain::prepare (double sampleRate, int fftSize, int maxBlockSize, bool deliveryEnabled)
{
    deliveryReserved = deliveryEnabled;

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

    for (int fed = 0; fed < kStages * windowSize + 4 * analysisHop; fed += analysisHop)
        pump (silence.data());

    fifoRead = 0;
    fifoWrite = 0;
    fifoCount = 0;
    padRemaining = margin;
}

//==============================================================================
void WarpChain::setWarp (double newRatio, double newFormant,
                         double lock, double delivery) noexcept
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

    for (auto& s : stages)
    {
        s.setVocoderPhase (engaged);
        s.setLock (lock);
    }

    // Delivery rides on the first stage only. It is a modulation of that
    // stage's ratio, and the debt controller inside it already nets to zero, so
    // putting it on more than one stage would multiply the swing without
    // pinning the onsets any harder.
    stages[0].setDelivery (engaged ? delivery : 0.0, ratio >= 1.0 ? ratio : 1.0 / ratio);

    // Stretch by the ratio, then put the length back. Whichever way round the
    // tempo went, both stages run; the sound is not symmetrical about 1, and
    // neither is what people reach for.
    stages[0].setRatio (1.0 / ratio);
    stages[1].setRatio (ratio);

    stages[1].setFormant (formantSemis);
}

void WarpChain::rebuild() noexcept
{
    for (auto& s : stages)
    {
        s.setVocoderPhase (true);
        s.setFormant (0.0);
        s.setLock (1.0);
        s.setDelivery (0.0, 1.0);
    }

    // A window per stage, plus margin.
    //
    // The margin is burst jitter, not start-up -- the priming in reset() deals
    // with that. Each stage releases only up to the start of its latest frame,
    // so it hands the next stage an irregular trickle, and every stage adds
    // about a hop of that. Swept over every ratio, block size and window, the
    // chain needs stages + 1 hops before it stops underrunning; this is that
    // plus one, because an underrun shifts everything after it a sample off the
    // grid and is exactly the kind of thing nobody catches by ear.
    //
    // Delivery banks time inside a syllable and gives it back in the gap, so it
    // needs its own budget on top -- which is why turning it up from zero is
    // the one knob move on the panel that changes the reported latency.
    const auto marginHops = kStages + 2
                              + (deliveryReserved ? (int) (2.0 * SpectralStage::kMaxDebtHops) : 0);

    margin = marginHops * analysisHop;
    latency = kStages * windowSize + margin;
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

    for (auto& s : stages)
    {
        s.push (ping.data(), count);

        const auto ready = s.available();

        if ((int) pong.size() < ready)
            pong.resize ((size_t) ready * 2, 0.0f);

        count = s.pop (pong.data(), ready);

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
