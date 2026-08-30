#pragma once

#include "Theme.h"
#include <functional>

namespace kloudvocalshift::gui
{

/** The one thing on the panel that reports rather than controls.

    The two tempos, the ratio between them, the ratio actually being applied
    after Amount, and the latency. It exists because the plugin's most
    surprising property is that at equal tempos every control on it does
    nothing, and a panel of knobs that are all up and all inaudible reads as
    broken unless something says why. The latency is there because it is large,
    it moves with Passes, and it is the number that decides whether you can
    reach for this while monitoring a take.
*/
class WarpReadout final : public juce::Component,
                          private juce::Timer
{
public:
    struct Source
    {
        std::function<float()> recorded, playing, amount, latencyMs;
    };

    explicit WarpReadout (Source s);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    Source source;
    float lastRecorded = -1.0f, lastPlaying = -1.0f, lastAmount = -1.0f, lastLatency = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WarpReadout)
};

} // namespace kloudvocalshift::gui
