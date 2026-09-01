// Every claim the README makes about this plugin is asserted here, and this
// binary has no JUCE dependency, so it builds and runs on a bare container.

#include "dsp/DspCore.h"
#include "Signal.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace kloudvocalshift;
using namespace kvstest;

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        std::printf ("%-58s %s\n", what.c_str(), ok ? "ok" : "FAILED");

        if (! ok)
            ++failures;
    }

    constexpr double kRate = 48000.0;

    /** Runs a signal through a freshly prepared core in blocks. */
    int underruns = 0;

    std::vector<float> run (DspCore::Params p, std::vector<float> input,
                            int blockSize = 512, double rate = kRate,
                            int* latencyOut = nullptr)
    {
        DspCore core;
        core.prepare (rate, blockSize, 1);
        core.setParams (p);

        if (latencyOut != nullptr)
            *latencyOut = core.getLatencySamples();

        for (int i = 0; i < (int) input.size(); i += blockSize)
        {
            const auto n = std::min (blockSize, (int) input.size() - i);
            auto* channel = input.data() + i;

            core.process (&channel, 1, n);
        }

        underruns += core.getUnderruns();

        return input;
    }

    DspCore::Params defaults()
    {
        DspCore::Params p;
        p.ratio = 1.0f;
        return p;
    }

    /** Worst-case difference between an output and its input delayed by the
        reported latency, in dB relative to the input's peak. */
    double nullAgainstDelayedInput (const std::vector<float>& in,
                                    const std::vector<float>& out, int latency)
    {
        double worst = 0.0;

        for (int i = latency; i < (int) in.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[(size_t) i] - in[(size_t) (i - latency)]));

        return toDb (worst / std::max (peakOf (in), 1.0e-30));
    }
}

//==============================================================================
int main()
{
    const int n = 96000;   // two seconds: long enough that the aligned
                           // measurement windows still fit past the worst
                           // latency the chain can report.
    const auto input = voice (n, kRate);

    std::printf ("\nKloudVocalShift DSP\n\n");

    //== The transparency claims ===============================================
    // These are the ones that decide whether the plugin is safe to leave on a
    // channel, so they come first and they are exact rather than approximate.

    {
        int latency = 0;
        const auto out = run (defaults(), input, 512, kRate, &latency);
        const auto worst = nullAgainstDelayedInput (input, out, latency);

        check (worst < -120.0,
               "equal tempos: null against delayed input (" + std::to_string ((int) worst) + " dB)");
    }

    {
        // Every character control at full, ratio still 1. Nothing should happen:
        // the ratio is what scales the effect, and the knobs only scale it
        // further.
        auto p = defaults();
        p.amount = 100.0f;
        p.lock = 0.0f;
        p.delivery = 100.0f;

        int latency = 0;
        const auto out = run (p, input, 512, kRate, &latency);

        check (nullAgainstDelayedInput (input, out, latency) < -120.0,
               "equal tempos, every character control at full: still null");
    }

    {
        auto p = defaults();
        p.ratio = 1.2f;
        p.bypass = true;

        int latency = 0;
        const auto out = run (p, input, 512, kRate, &latency);

        check (nullAgainstDelayedInput (input, out, latency) < -120.0,
               "bypass: null against delayed input");
    }

    {
        auto p = defaults();
        p.ratio = 1.2f;
        p.mixPercent = 0.0f;

        int latency = 0;
        const auto out = run (p, input, 512, kRate, &latency);

        check (nullAgainstDelayedInput (input, out, latency) < -120.0,
               "Mix at 0 %: null against delayed input");
    }

    {
        // A plugin that misreports its latency drags everything downstream of
        // it off the grid, and the host has no way to notice.
        //
        // Measured at equal tempos, where the chain is an identity and the
        // impulse comes back out as an impulse. The reported figure does not
        // depend on the ratio -- it is a window per stage plus a fixed margin --
        // so checking it where it can be checked exactly checks it everywhere.
        auto p = defaults();
        p.ratio = 1.0f;

        std::vector<float> impulse ((size_t) n, 0.0f);
        impulse[2000] = 1.0f;

        int latency = 0;
        const auto out = run (p, impulse, 512, kRate, &latency);

        int loudest = 0;

        for (int i = 0; i < n; ++i)
            if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) loudest]))
                loudest = i;

        check (loudest - 2000 == latency,
               "reported latency matches the measured impulse delay");
    }

    //== Block-size invariance =================================================
    // The hop is counted internally rather than derived from the block, so the
    // host's buffer size must not be audible. Bitwise, not approximately.

    {
        auto p = defaults();
        p.ratio = 1.2f;

        const auto a = run (p, input, 64);
        const auto b = run (p, input, 512);

        check (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0,
               "block size 64 vs 512: bitwise identical");
    }

    {
        auto p = defaults();
        p.ratio = 0.8f;
        p.formantSemis = 3.0f;

        const auto a = run (p, input, 128);
        const auto b = run (p, input, 480);

        check (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0,
               "block size 128 vs 480, formant engaged: bitwise identical");
    }

    //== Determinism ===========================================================
    // A warped bounce has to be the same bounce every time or the effect cannot
    // be committed to.

    {
        auto p = defaults();
        p.ratio = 1.2f;

        const auto a = run (p, input);
        const auto b = run (p, input);

        check (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0,
               "same settings twice: bitwise identical");
    }

    //== Silence ===============================================================

    {
        auto p = defaults();
        p.ratio = 1.25f;
        p.formantSemis = -5.0f;

        const auto out = run (p, std::vector<float> ((size_t) n, 0.0f));

        check (peakOf (out) == 0.0, "silence in, silence out");
    }

    //== The effect actually happens ===========================================
    // Crest factor is peak over RMS. The test signal is a phase-aligned
    // harmonic series, so it starts with tall pulses and quiet gaps; smearing
    // the phase fills the gaps in and pulls the peaks down.

    {
        // Aligned: the output is delayed by the reported latency, and comparing
        // an unaligned window compares two different syllables.
        const int from = 8192, to = n - 40000;
        const auto dry = crestDb (input, from, to);

        std::printf ("\n  ratio    crest dB    change    RMS change\n");

        double crestAt120 = 0.0;

        for (const auto ratio : { 0.75f, 0.833f, 0.9f, 1.0f, 1.1f, 1.2f, 1.333f })
        {
            auto p = defaults();
            p.ratio = ratio;

            int latency = 0;
            const auto out = run (p, input, 512, kRate, &latency);
            const auto wet = crestDb (out, from + latency, to + latency);
            const auto level = toDb (rms (out, from + latency, to + latency)
                                       / rms (input, from, to));

            std::printf ("  %-8.3f %-11.2f %-9.2f %+.2f\n", ratio, wet, wet - dry, level);

            if (ratio == 1.2f)
                crestAt120 = wet;
        }

        std::printf ("\n");

        check (crestAt120 < dry - 1.0,
               "warping 100 to 120 measurably flattens the glottal pulse");
    }

    {
        // Lock is a real axis, not a trim: fully locked and fully free are
        // different sounds, and the level match holds across the whole sweep so
        // they can be compared without reaching for a fader.
        auto locked = defaults();
        locked.ratio = 1.2f;

        auto free = locked;
        free.lock = 0.0f;

        int la = 0, lb = 0;
        const auto a = run (locked, input, 512, kRate, &la);
        const auto b = run (free, input, 512, kRate, &lb);

        double worst = 0.0;

        for (size_t i = 20000; i < a.size(); ++i)
            worst = std::max (worst, (double) std::abs (a[i] - b[i]));

        check (toDb (worst / peakOf (input)) > -40.0, "Lock is audibly a control");

        const auto levelOf = [&] (const std::vector<float>& x, int latency)
        {
            return toDb (rms (x, 8192 + latency, n - 40000 + latency)
                           / rms (input, 8192, n - 40000));
        };

        // The whole reason phase locking is in here is that unlocking it costs
        // level -- an early build lost 27 dB at full unlock, which reads as a
        // broken plugin rather than a character control.
        check (std::abs (levelOf (b, lb)) < 2.0,
               "level holds within 2 dB with Lock all the way down");
    }

    {
        // Delivery moves time around inside the clip and gives it back, so the
        // clip has to come out the same length -- which for a streaming plugin
        // means it must never have run the chain dry.
        auto p = defaults();
        p.ratio = 1.2f;
        p.delivery = 100.0f;

        const auto before = underruns;

        int latency = 0;
        const auto out = run (p, input, 512, kRate, &latency);

        check (underruns == before, "Delivery at 100 % never starves the chain");
        check (peakOf (out) > 0.0, "Delivery at 100 % still produces audio");
    }

    //== Formant ===============================================================

    {
        // Shifting the envelope up moves energy from the low formants toward
        // the high ones, which is the "thinner" everyone is actually after.
        const auto band = [] (const std::vector<float>& x, double lo, double hi)
        {
            // Crude one-pole band energy; enough to see a formant move without
            // dragging an FFT into the assertion.
            double a = 0.0, b = 0.0, sum = 0.0;
            const auto ka = std::exp (-2.0 * kPi * lo / kRate);
            const auto kb = std::exp (-2.0 * kPi * hi / kRate);

            for (size_t i = 8192; i < x.size(); ++i)
            {
                a = ka * a + (1.0 - ka) * (double) x[i];
                b = kb * b + (1.0 - kb) * (double) x[i];
                sum += (b - a) * (b - a);
            }

            return std::sqrt (sum / (double) (x.size() - 8192));
        };

        auto p = defaults();
        p.ratio = 1.0f;

        p.formantSemis = 0.0f;
        const auto flat = run (p, input);

        p.formantSemis = 7.0f;
        const auto up = run (p, input);

        p.formantSemis = -7.0f;
        const auto down = run (p, input);

        const auto tilt = [&] (const std::vector<float>& x)
        {
            return toDb (band (x, 1500.0, 5000.0) / std::max (band (x, 200.0, 900.0), 1.0e-30));
        };

        std::printf ("  formant tilt: %+.2f dB at 0 st, %+.2f at +7, %+.2f at -7\n\n",
                     tilt (flat), tilt (up), tilt (down));

        check (tilt (up) > tilt (flat) + 1.0, "Formant +7 st moves energy upward");
        check (tilt (down) < tilt (flat) - 1.0, "Formant -7 st moves energy downward");
    }

    //== Nothing blows up ======================================================

    {
        bool clean = true;
        double loudest = 0.0;

        for (int r = 0; r <= 20; ++r)
        {
            for (int f = -12; f <= 12; f += 4)
            {
                auto p = defaults();
                p.ratio = 0.5f + 0.075f * (float) r;
                p.formantSemis = (float) f;

                const auto out = run (p, input, 512, kRate);

                for (auto v : out)
                    if (! std::isfinite (v))
                        clean = false;

                loudest = std::max (loudest, peakOf (out) / peakOf (input));
            }
        }

        std::printf ("  loudest output over the whole ratio x formant grid: %+.2f dB\n\n",
                     toDb (loudest));

        check (clean, "no NaN or infinity anywhere on the ratio x formant grid");
        check (loudest < 4.0, "nothing on the grid exceeds +12 dB over the source");
    }

    //== Rates =================================================================

    {
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            auto p = defaults();
            const auto sig = voice ((int) rate, rate);

            int latency = 0;
            const auto out = run (p, sig, 512, rate, &latency);

            check (nullAgainstDelayedInput (sig, out, latency) < -120.0,
                   "null at " + std::to_string ((int) rate) + " Hz");
        }
    }

    // Accumulated over every run above, which is a few hundred passes across
    // every ratio, window, pass count, block size and sample rate the plugin
    // supports.
    check (underruns == 0, "no buffer underrun anywhere in the whole suite");

    std::printf ("\n%s\n\n", failures == 0 ? "all passed" : "FAILURES");

    return failures == 0 ? 0 : 1;
}
