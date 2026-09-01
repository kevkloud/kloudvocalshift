#include "SpectralStage.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kloudvocalshift
{

namespace
{
    constexpr double kPi  = 3.14159265358979323846;
    constexpr double kTau = 2.0 * kPi;

    inline double wrapToPi (double x) noexcept
    {
        return x - kTau * std::round (x / kTau);
    }
}

//==============================================================================
void SpectralStage::prepare (double sampleRate, int newFftSize)
{
    rate = sampleRate;
    fftSize = newFftSize;
    analysisHop = fftSize / 4;

    fft.prepare (fftSize);

    // Periodic Hann on both ends. At 75 % overlap the squared window sums to a
    // constant, which is what makes the ratio-1 passthrough exact rather than
    // merely close.
    window.resize ((size_t) fftSize);

    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = 0.5 * (1.0 - std::cos (kTau * (double) n / (double) fftSize));

    inRing.assign ((size_t) fftSize, 0.0f);

    // Room for several frames past the write head, so push() never reallocates
    // in the middle of a block.
    accum.assign ((size_t) fftSize * 4, 0.0f);

    spectrum.assign ((size_t) fftSize, {});
    scratch.assign ((size_t) fftSize, {});

    const auto bins = (size_t) (fftSize / 2 + 1);
    magnitude.assign (bins, 0.0);
    phase.assign (bins, 0.0);
    prevPhase.assign (bins, 0.0);
    sumPhase.assign (bins, 0.0);
    sumFree.assign (bins, 0.0);
    expected.assign (bins, 0.0);
    peakOf.assign (bins, 0);

    for (size_t k = 0; k < bins; ++k)
        expected[k] = kTau * (double) k * (double) analysisHop / (double) fftSize;

    // The fast follower tracks a syllable, the slow one tracks the passage it
    // sits in; the difference between them is "is this frame loud for this
    // performance", which is what Delivery steers on. Absolute level would
    // steer on how hard the singer was mic'd.
    const auto coeff = [this] (double ms)
    {
        return 1.0 - std::exp (-(double) analysisHop / (std::max (ms, 1.0) * 0.001 * rate));
    };

    envFastCoeff = coeff (35.0);
    envSlowCoeff = coeff (900.0);

    logMag.assign ((size_t) fftSize, 0.0);
    envelope.assign ((size_t) fftSize, 0.0);

    reset();
}

void SpectralStage::reset() noexcept
{
    std::fill (inRing.begin(), inRing.end(), 0.0f);
    std::fill (accum.begin(), accum.end(), 0.0f);
    std::fill (prevPhase.begin(), prevPhase.end(), 0.0);
    std::fill (sumPhase.begin(), sumPhase.end(), 0.0);
    std::fill (sumFree.begin(), sumFree.end(), 0.0);

    ringPos = 0;
    hopCount = 0;
    accumBase = 0;
    writePos = 0;
    readPos = 0;
    writeCursor = 0.0;
    nominalCursor = 0.0;
    envFast = 0.0;
    envSlow = 0.0;
    firstFrame = true;
}

//==============================================================================
void SpectralStage::push (const float* input, int numSamples)
{
    if (fftSize <= 0)
        return;

    const auto n = (size_t) fftSize;

    for (int i = 0; i < numSamples; ++i)
    {
        inRing[(size_t) ringPos] = input[i];
        ringPos = (int) (((size_t) ringPos + 1) % n);

        if (++hopCount >= analysisHop)
        {
            hopCount = 0;
            processFrame();
        }
    }
}

int SpectralStage::pop (float* output, int numSamples) noexcept
{
    const auto ready = std::min ((long long) numSamples, writePos - readPos);

    for (long long i = 0; i < ready; ++i)
        output[i] = accum[(size_t) (readPos + i - accumBase)];

    readPos += ready;

    return (int) ready;
}

void SpectralStage::compact()
{
    // Everything before readPos has been handed out and every frame that could
    // still touch it has been added, so the front of the buffer is dead and can
    // be slid off. Doing it here rather than per sample keeps it to one memmove
    // per few frames.
    const auto dead = readPos - accumBase;

    if (dead < (long long) fftSize * 2)
        return;

    const auto live = accum.size() - (size_t) dead;

    std::memmove (accum.data(), accum.data() + dead, live * sizeof (float));
    std::fill (accum.end() - (long) dead, accum.end(), 0.0f);

    accumBase = readPos;
}

//==============================================================================
void SpectralStage::processFrame()
{
    const auto n = (size_t) fftSize;
    const auto bins = (size_t) (fftSize / 2 + 1);
    const auto start = (size_t) ringPos;

    // ringPos points at the oldest sample, so reading forward walks the window
    // in time order.
    for (size_t i = 0; i < n; ++i)
        scratch[i] = { (double) inRing[(start + i) % n] * window[i], 0.0 };

    fft.forward (scratch.data());

    for (size_t k = 0; k < bins; ++k)
    {
        magnitude[k] = std::abs (scratch[k]);
        phase[k] = std::arg (scratch[k]);
    }

    if (firstFrame)
    {
        prevPhase = phase;
        sumPhase = phase;
        sumFree = phase;
    }

    if (formantSemis != 0.0)
        applyFormantShift();

    // Where this frame lands in the output, and how far that is from where the
    // last one landed. This is the whole effect: everything the stage does to
    // the sound is a consequence of that distance not being the analysis hop.
    const auto effectiveRatio = deliveryRatio();

    const auto frameStart = (long long) std::llround (writeCursor);
    const auto synthesisHop = firstFrame ? (long long) analysisHop : frameStart - writePos;

    writeCursor += (double) analysisHop * effectiveRatio;
    nominalCursor += (double) analysisHop * ratio;

    if (vocoderPhase)
    {
        // Identity phase locking (Laroche and Dolson).
        //
        // Propagating every bin independently is what makes a naive phase
        // vocoder sound like a chorus pedal: neighbouring bins belonging to the
        // same harmonic drift apart, the overlapping frames stop adding
        // coherently, and the output loses several dB of level for no musical
        // reason. Live's Complex mode does not do that, so neither does this.
        //
        // Only the peaks are propagated. Every other bin is pinned to its
        // peak's new phase with the same offset it had in the analysis, which
        // keeps each harmonic internally rigid.
        //
        // What is deliberately *not* preserved is the relationship between one
        // peak and the next: those still drift apart, because that drift is the
        // effect. The glottal pulse is every harmonic arriving together, and it
        // is exactly that alignment a warp takes away.
        int lastPeak = -1;

        for (size_t k = 0; k < bins; ++k)
        {
            const auto m = magnitude[k];

            const auto isPeak =
                (k >= 2 && k + 2 < bins)
                    && m > magnitude[k - 1] && m > magnitude[k + 1]
                    && m > magnitude[k - 2] && m > magnitude[k + 2];

            // Every bin gets its own free-running accumulator whether it is a
            // peak or not, because Lock blends toward it. At full lock the
            // free version is computed and thrown away, which costs a few
            // multiplies and buys a control with no special cases in it.
            const auto deviation = wrapToPi (phase[k] - prevPhase[k] - expected[k]);
            const auto omega = (expected[k] + deviation) / (double) analysisHop;

            sumFree[k] = wrapToPi (sumFree[k] + omega * (double) synthesisHop);

            if (isPeak || lastPeak < 0)
            {
                lastPeak = (int) k;
                sumPhase[k] = sumFree[k];
            }

            peakOf[k] = lastPeak;
        }

        // A bin between two peaks belongs to the nearer one.
        for (size_t k = 0; k + 1 < bins; ++k)
        {
            const auto owner = (size_t) peakOf[k];

            for (size_t j = k + 1; j < bins; ++j)
            {
                if ((size_t) peakOf[j] != owner)
                {
                    const auto midpoint = (k + j) / 2;

                    for (size_t b = midpoint + 1; b < j; ++b)
                        peakOf[b] = peakOf[j];

                    k = j - 1;
                    break;
                }
            }
        }
    }

    for (size_t k = 0; k < bins; ++k)
    {
        double out;

        if (vocoderPhase)
        {
            const auto p = (size_t) peakOf[k];

            // Rigid within a harmonic: the offset this bin had from its peak in
            // the analysis is the offset it keeps.
            const auto locked = sumPhase[p] + (phase[k] - phase[p]);

            // Lock at 1 is exactly that. Lock at 0 is the free-running bin,
            // which is the naive phase vocoder and sounds like one.
            out = lock >= 1.0 ? locked
                              : sumFree[k] + lock * wrapToPi (locked - sumFree[k]);
        }
        else
        {
            out = phase[k];
        }

        prevPhase[k] = phase[k];

        const std::complex<double> value { magnitude[k] * std::cos (out),
                                           magnitude[k] * std::sin (out) };

        spectrum[k] = value;

        if (k > 0 && k < bins - 1)
            spectrum[n - k] = std::conj (value);
    }

    firstFrame = false;

    fft.inverse (spectrum.data());

    // Hann squared summed at hop H over a window of N comes to 0.375 N / H, and
    // the hop that matters for the sum is the synthesis one.
    // The exact average hop rather than this frame's rounded one, so the
    // rounding between 426 and 427 samples does not amplitude-modulate the
    // output at the frame rate.
    const auto norm = (double) analysisHop * effectiveRatio / (0.375 * (double) fftSize);

    // Grow before writing: a frame lands N samples past its own start, and the
    // read head is only ever let up to the start.
    const auto needed = (size_t) (frameStart + (long long) fftSize - accumBase);

    if (needed > accum.size())
        accum.resize (needed + (size_t) fftSize, 0.0f);

    const auto offset = (size_t) (frameStart - accumBase);

    for (size_t i = 0; i < n; ++i)
        accum[offset + i] += (float) (spectrum[i].real() * window[i] * norm);

    writePos = frameStart;

    compact();
}

//==============================================================================
/** Delivery: the ratio this frame actually gets, rather than the nominal one.

    Singing to a 100 BPM click and hearing it at 120 shortens every syllable and
    lengthens every gap. That is a *timing* change, so it looks like the one
    thing a plugin doing this in place cannot have -- but it only has to net to
    zero, not be zero everywhere. Compress the loud frames, stretch the quiet
    ones, and hold the running total with a debt controller: syllables get
    shorter, gaps get longer, onsets stay on the grid and the clip is still the
    same length.

    At amount 0 every term collapses to exactly 1 -- the modulation by
    construction, and the debt because nominalCursor and writeCursor have then
    been fed the identical sequence of additions -- so the stage stays a
    bit-exact identity.
*/
double SpectralStage::deliveryRatio() noexcept
{
    if (deliveryAmount <= 0.0)
        return ratio;

    double energy = 0.0;

    for (size_t k = 0; k < magnitude.size(); ++k)
        energy += magnitude[k] * magnitude[k];

    energy = std::sqrt (energy);

    envFast += envFastCoeff * (energy - envFast);
    envSlow += envSlowCoeff * (energy - envSlow);

    // How loud this frame is for this performance, in octaves, squashed to
    // -1..1. Silence reads as quiet rather than as a divide by zero.
    const auto loudness = envSlow > 1.0e-9
        ? std::tanh (std::log2 (std::max (envFast, 1.0e-12) / envSlow))
        : -1.0;

    // Loud compresses, quiet stretches.
    auto modulation = std::pow (std::max (deliverySpeed, 1.0e-3), -loudness);

    modulation = 1.0 + deliveryAmount * (modulation - 1.0);

    // Debt, in hops. Positive means the modulation has taken time it has not
    // given back yet. A gentle pull keeps the average honest without fighting
    // the modulation inside a syllable, which is what an earlier, much tighter
    // controller did -- it cancelled the effect it was supposed to be
    // regulating.
    const auto debtHops = (nominalCursor - writeCursor) / (double) analysisHop;

    modulation *= std::exp (std::clamp (debtHops / 48.0, -0.6, 0.6));

    // Then a hard stop. Past this the chain would be waiting on samples that
    // have not been produced, which is an underrun, which is a click. The
    // budget for it is bought in WarpChain's margin.
    if (debtHops > kMaxDebtHops)
        modulation = std::max (modulation, 1.0);
    else if (debtHops < -kMaxDebtHops)
        modulation = std::min (modulation, 1.0);

    return ratio * std::clamp (modulation, 0.55, 1.8);
}

//==============================================================================
/** Spectral-envelope shift, in the magnitude domain only.

    Stands in for Complex Pro's Formants control, and for the fact that most of
    the records this is chasing are formant-shifted as well as warped. The
    envelope is the cepstrally smoothed log magnitude -- low-quefrency terms
    only, so the harmonic comb is left behind and what survives is the vocal
    tract. Move it, divide out the difference, and apply the result as a gain
    curve to the magnitudes the frame already has. Nothing is resynthesised and
    phase is never touched, so at 0 semitones this is exactly nothing.
*/
void SpectralStage::applyFormantShift() noexcept
{
    const auto n = (size_t) fftSize;
    const auto bins = (size_t) (fftSize / 2 + 1);

    for (size_t k = 0; k < bins; ++k)
        logMag[k] = std::log (std::max (magnitude[k], 1.0e-12));

    for (size_t k = 1; k < bins - 1; ++k)
        logMag[n - k] = logMag[k];

    for (size_t i = 0; i < n; ++i)
        scratch[i] = { logMag[i], 0.0 };

    // Forward then inverse: Fft::inverse carries the 1/N, so this is the order
    // whose round trip is unity. The signal is real and even, so which of the
    // two produces the cepstrum is otherwise immaterial.
    fft.forward (scratch.data());

    // 1.8 ms of quefrency keeps formant-scale structure and discards everything
    // as fine as a pitch period, down to a 555 Hz fundamental -- above any sung
    // note that matters here.
    const auto cutoff = std::max<size_t> (8, (size_t) (rate * 0.0018));

    for (size_t q = cutoff; q < n - cutoff; ++q)
        scratch[q] = { 0.0, 0.0 };

    fft.inverse (scratch.data());

    for (size_t i = 0; i < n; ++i)
        envelope[i] = scratch[i].real();

    const auto shift = std::pow (2.0, formantSemis / 12.0);
    const auto last = (double) (bins - 1);

    for (size_t k = 0; k < bins; ++k)
    {
        // Shifting the envelope up by `shift` means bin k should now hold what
        // bin k / shift used to.
        const auto source = std::min ((double) k / shift, last);
        const auto lo = (size_t) source;
        const auto frac = source - (double) lo;
        const auto hi = std::min (lo + 1, bins - 1);

        const auto moved = envelope[lo] + frac * (envelope[hi] - envelope[lo]);

        // +/- 24 dB. Without a clamp the ratio runs away in the near-silent bins
        // between harmonics, where both envelopes are extrapolated noise.
        const auto correction = std::clamp (moved - envelope[k], -2.76, 2.76);

        magnitude[k] *= std::exp (correction);
    }
}

} // namespace kloudvocalshift
