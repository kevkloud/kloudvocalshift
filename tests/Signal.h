#pragma once

#include <cmath>
#include <vector>

namespace kvstest
{

constexpr double kPi = 3.14159265358979323846;

/** A synthetic voice: a phase-aligned harmonic series -- which is what a
    glottal pulse train is -- shaped by three formant resonances.

    Phase alignment is the point. Every harmonic crosses zero together at the
    start of each period, which is what gives a real voice its pulse and its
    high crest factor, and it is exactly the property a phase vocoder destroys.
    A test signal built from random-phase harmonics would have the right
    spectrum and would be unable to show the effect at all.
*/
inline std::vector<float> voice (int numSamples, double sampleRate,
                                 double f0 = 120.0)
{
    const double formants[3] = { 700.0, 1220.0, 2600.0 };
    const double widths[3]   = { 130.0, 70.0, 160.0 };

    std::vector<float> out ((size_t) numSamples, 0.0f);

    // Harmonic amplitudes: a 1/h^2 glottal roll-off with three resonances on
    // top of it.
    std::vector<double> amplitude;

    for (int h = 1; h * f0 < sampleRate * 0.45; ++h)
    {
        const auto f = (double) h * f0;

        double a = 1.0 / (double) (h * h);

        for (int r = 0; r < 3; ++r)
        {
            const auto d = (f - formants[r]) / widths[r];
            a += 0.9 / (double) h * std::exp (-0.5 * d * d);
        }

        amplitude.push_back (a);
    }

    // A stationary tone is the one signal a phase vocoder handles well, so it
    // would show nothing. This is a sung note: vibrato, a slow drift, and a
    // syllable envelope with hard attacks. All three are what the vocoder
    // actually has trouble with, and all three are present in the material this
    // plugin is for.
    double carrier = 0.0;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto t = (double) n / sampleRate;

        const auto vibrato = 1.0 + 0.022 * std::sin (2.0 * kPi * 5.4 * t);
        const auto drift   = 1.0 + 0.04 * std::sin (2.0 * kPi * 0.31 * t);

        carrier += 2.0 * kPi * f0 * vibrato * drift / sampleRate;

        // Four syllables a second: a 4 ms attack and a decay, so there are real
        // transients to smear.
        const auto phaseInSyllable = std::fmod (t * 4.0, 1.0);
        const auto envelope = phaseInSyllable < 0.02
                                ? phaseInSyllable / 0.02
                                : std::exp (-3.0 * (phaseInSyllable - 0.02));

        double sample = 0.0;

        for (size_t h = 0; h < amplitude.size(); ++h)
            sample += amplitude[h] * std::cos ((double) (h + 1) * carrier);

        out[(size_t) n] = (float) (sample * envelope);
    }

    double peak = 0.0;

    for (auto v : out)
        peak = std::max (peak, (double) std::abs (v));

    if (peak > 0.0)
        for (auto& v : out)
            v = (float) ((double) v * 0.5 / peak);

    return out;
}

inline double rms (const std::vector<float>& x, int from = 0, int to = -1)
{
    if (to < 0)
        to = (int) x.size();

    double sum = 0.0;

    for (int i = from; i < to; ++i)
        sum += (double) x[(size_t) i] * (double) x[(size_t) i];

    return std::sqrt (sum / std::max (1, to - from));
}

inline double peakOf (const std::vector<float>& x, int from = 0, int to = -1)
{
    if (to < 0)
        to = (int) x.size();

    double p = 0.0;

    for (int i = from; i < to; ++i)
        p = std::max (p, (double) std::abs (x[(size_t) i]));

    return p;
}

/** Peak over RMS, in dB. A pulse train scores high; smear it and the peaks come
    down while the energy between them goes up, so the number falls. This is the
    single most direct measurement of the thing the plugin exists to do. */
inline double crestDb (const std::vector<float>& x, int from, int to)
{
    const auto r = rms (x, from, to);

    return r > 0.0 ? 20.0 * std::log10 (peakOf (x, from, to) / r) : 0.0;
}

inline double toDb (double linear)
{
    return 20.0 * std::log10 (std::max (linear, 1.0e-30));
}

} // namespace kvstest
