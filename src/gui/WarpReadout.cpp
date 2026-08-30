#include "WarpReadout.h"

#include <cmath>

namespace kloudvocalshift::gui
{

WarpReadout::WarpReadout (Source s) : source (std::move (s))
{
    startTimerHz (12);
}

void WarpReadout::timerCallback()
{
    const auto r = source.recorded();
    const auto p = source.playing();
    const auto a = source.amount();
    const auto l = source.latencyMs();

    if (r != lastRecorded || p != lastPlaying || a != lastAmount || l != lastLatency)
    {
        lastRecorded = r;
        lastPlaying = p;
        lastAmount = a;
        lastLatency = l;
        repaint();
    }
}

void WarpReadout::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (area.toFloat(), theme::corner);

    const auto recorded = juce::jmax (1.0f, source.recorded());
    const auto playing = source.playing();
    const auto ratio = juce::jlimit (0.25f, 4.0f, playing / recorded);
    const auto amount = juce::jlimit (0.0f, 1.0f, source.amount() * 0.01f);
    const auto applied = 1.0f + amount * (ratio - 1.0f);

    auto inner = area.reduced (12, 8);
    auto headline = inner.removeFromTop (30);

    // Grey when the chain is an identity, accented when it is not. The single
    // most useful thing the panel can tell you at a glance is whether this is
    // doing anything at all.
    g.setColour (applied == 1.0f ? theme::textDim : theme::accent);
    g.setFont (theme::labelFont (23.0f));
    g.drawText (juce::String (applied, 3) + "x", headline, juce::Justification::centredLeft, false);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (11.0f));
    g.drawText (juce::String (juce::roundToInt (recorded)) + " to "
                    + juce::String (juce::roundToInt (playing)) + " BPM",
                headline, juce::Justification::centredRight, false);

    g.setFont (theme::labelFont (10.0f));

    g.drawText (applied == 1.0f
                    ? juce::String ("identity - the output is the input")
                    : juce::String ("warped and unwarped, same length"),
                inner.removeFromTop (14), juce::Justification::centredLeft, false);

    g.drawText (juce::String (source.latencyMs(), 1) + " ms latency",
                inner.removeFromTop (14), juce::Justification::centredLeft, false);
}

} // namespace kloudvocalshift::gui
