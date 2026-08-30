#pragma once

#include "Fft.h"

#include <complex>
#include <vector>

namespace kloudvocalshift
{

//==============================================================================
/** One short-time Fourier stage: analyse at a fixed hop, optionally move the
    spectral envelope, and resynthesise at a different hop.

    This is the machine underneath Ableton's Complex warp mode, and the only
    thing that makes it a *time stretch* is that the synthesis hop is not the
    analysis hop. Everything else -- what the sound loses on the way through --
    follows from that one mismatch:

    - The synthesis hop is shorter than the analysis hop when speeding up, so
      the output runs out of room before the input runs out of frames and some
      frames are never used. That is the "losing data" part. It is frames, not
      sample rate.

    - Each bin's output phase is propagated from its own estimated
      instantaneous frequency, independently of every other bin's. A voice is a
      train of glottal pulses, and a pulse is every harmonic arriving
      phase-aligned at one instant; after enough frames of independent
      propagation they no longer line up. The magnitude spectrum is intact, the
      pulse is not, and the ear reads that as less chest and more air.

    - A transient is spread over the whole window, so consonants soften.

    Set `phaseMode` to passthrough and `ratio` to 1 and the stage is an exact
    windowed identity -- Hann squared at 75 % overlap sums to a constant -- which
    is what lets a stage be left in the chain doing nothing rather than switched
    out and changing the latency.

    Output is produced at a variable rate, so the stage is push/pull rather than
    in-place: push input, then pull whatever it has finished.
*/
class SpectralStage
{
public:
    void prepare (double sampleRate, int fftSize);
    void reset() noexcept;

    /** Output length over input length. Above 1 the stage stretches. */
    void setRatio (double newRatio) noexcept { ratio = newRatio; }

    /** Spectral envelope shift in semitones; magnitudes only, phase untouched,
        so this composes with the warp rather than fighting it. */
    void setFormant (double semitones) noexcept { formantSemis = semitones; }

    /** False leaves the analysis phase alone, which is the identity. True is a
        phase vocoder. */
    void setVocoderPhase (bool shouldUseVocoder) noexcept { vocoderPhase = shouldUseVocoder; }

    int getWindow() const noexcept { return fftSize; }

    void push (const float* input, int numSamples);

    /** Samples that no future frame can still change. */
    int available() const noexcept { return (int) (writePos - readPos); }

    /** Pops up to numSamples; returns how many were actually written. */
    int pop (float* output, int numSamples) noexcept;

private:
    void processFrame();
    void applyFormantShift() noexcept;
    void compact();

    Fft fft;
    int fftSize = 0;
    int analysisHop = 0;

    double rate = 48000.0;
    double ratio = 1.0;
    double formantSemis = 0.0;
    bool vocoderPhase = false;

    std::vector<double> window;
    std::vector<float> inRing;
    int ringPos = 0;
    int hopCount = 0;

    // Output accumulator, addressed in absolute sample positions so the
    // fractional synthesis hop never has to be rounded twice.
    std::vector<float> accum;
    long long accumBase = 0;
    long long writePos = 0;
    long long readPos = 0;
    double writeCursor = 0.0;
    bool firstFrame = true;

    std::vector<std::complex<double>> spectrum, scratch;
    std::vector<double> magnitude, phase, prevPhase, sumPhase, expected;
    std::vector<int> peakOf;
    std::vector<double> logMag, envelope;
};

} // namespace kloudvocalshift
