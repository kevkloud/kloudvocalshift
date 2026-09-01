// Offline profiler. Drives the DSP core with no host and no audio device, and
// prints the tables the README quotes.
//
//   ./build/measure profile     what a warp of each ratio costs the pulse
//   ./build/measure windows     the same, per window length
//   ./build/measure formant     envelope shift versus spectral tilt

#include "dsp/DspCore.h"
#include "../../tests/Signal.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace kloudvocalshift;
using namespace kvstest;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kFrom = 8192;

    int lastLatency = 0;

    std::vector<float> run (DspCore::Params p, std::vector<float> input)
    {
        DspCore core;
        core.prepare (kRate, 512, 1);
        core.setParams (p);

        lastLatency = core.getLatencySamples();

        for (int i = 0; i < (int) input.size(); i += 512)
        {
            const auto n = std::min (512, (int) input.size() - i);
            auto* channel = input.data() + i;

            core.process (&channel, 1, n);
        }

        return input;
    }

    const char* windowName (Window w)
    {
        switch (w)
        {
            case Window::shortWindow: return "Short";
            case Window::longWindow:  return "Long";
            default:                  return "Normal";
        }
    }
}

int main (int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "profile";

    const int n = (int) kRate * 2;
    const auto input = voice (n, kRate);
    const int kTo = (int) kRate * 2 - 40000;
    const auto dryCrest = crestDb (input, kFrom, kTo);
    const auto dryRms = rms (input, kFrom, kTo);

    std::printf ("\nsource: 120 Hz synthetic voice, crest %.2f dB\n\n", dryCrest);

    if (mode == "profile")
    {
        std::printf ("  100 BPM to   ratio    crest dB   pulse lost   level\n");

        for (const auto target : { 70, 80, 83, 90, 100, 110, 120, 130, 140 })
        {
            DspCore::Params p;
            p.ratio = (float) target / 100.0f;

            const auto out = run (p, input);

            std::printf ("  %-12d %-8.3f %-10.2f %-12.2f %+.2f dB\n",
                         target, p.ratio, crestDb (out, kFrom + lastLatency, kTo + lastLatency),
                         crestDb (out, kFrom + lastLatency, kTo + lastLatency) - dryCrest,
                         toDb (rms (out, kFrom + lastLatency, kTo + lastLatency) / dryRms));
        }
    }
    else if (mode == "windows")
    {
        std::printf ("  window   samples   crest dB at 1.2x   pulse lost   latency   + Delivery\n");

        for (const auto w : { Window::shortWindow, Window::normal, Window::longWindow })
        {
            DspCore core;
            core.prepare (kRate, 512, 1);

            DspCore::Params p;
            p.ratio = 1.2f;
            p.window = w;
            core.setParams (p);

            const auto plain = core.getLatencySamples();

            p.delivery = 100.0f;
            core.setParams (p);
            const auto withDelivery = core.getLatencySamples();

            p.delivery = 0.0f;
            const auto out = run (p, input);

            std::printf ("  %-8s %-9d %-18.2f %-12.2f %-9.1f %.1f\n",
                         windowName (w), core.getWindow(),
                         crestDb (out, kFrom + lastLatency, kTo + lastLatency),
                         crestDb (out, kFrom + lastLatency, kTo + lastLatency) - dryCrest,
                         1000.0 * (double) plain / kRate,
                         1000.0 * (double) withDelivery / kRate);
        }
    }
    else if (mode == "formant")
    {
        std::printf ("  shift st   level\n");

        for (int st = -12; st <= 12; st += 2)
        {
            DspCore::Params p;
            p.ratio = 1.0f;
            p.formantSemis = (float) st;

            const auto out = run (p, input);

            std::printf ("  %+-10d %+.2f dB\n", st, toDb (rms (out, kFrom + lastLatency, kTo + lastLatency) / dryRms));
        }
    }
    else
    {
        std::printf ("unknown mode '%s'\n", mode.c_str());
        return 1;
    }

    std::printf ("\n");
    return 0;
}
