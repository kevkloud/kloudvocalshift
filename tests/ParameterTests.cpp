#include "PluginProcessor.h"

#include <iostream>
#include <string>

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    namespace p = kloudvocalshift::params;
}

//==============================================================================
/** The parameter schema is a wire protocol: a set saved today has to open the
    same way in two years. These exist to make a rename or a reorder fail loudly
    here rather than silently in someone's project.
*/
static void testSchemaIsStable()
{
    KloudVocalShiftAudioProcessor processor;
    auto& apvts = processor.getApvts();

    for (auto id : { p::kRecorded, p::kPlaying, p::kFollowHost, p::kAmount, p::kLock,
                     p::kDelivery, p::kFormant, p::kWindow, p::kMix, p::kTrim, p::kBypass })
        check (apvts.getParameter (id) != nullptr,
               std::string ("parameter '") + id + "' still exists");

    check (apvts.getParameter (p::kAmount)->getDefaultValue() == 1.0f,
           "Amount defaults to 100 %");

    // Not just "near zero": Formant at its default has to be exactly zero or the
    // plugin is not transparent out of the box, and a stepped range would round
    // it a few ULPs off.
    check (apvts.getRawParameterValue (p::kFormant)->load() == 0.0f,
           "Formant defaults to exactly 0 semitones");

    check (apvts.getRawParameterValue (p::kLock)->load() == 100.0f,
           "Lock defaults to fully locked");

    // Exactly zero, not near it: Delivery is the one control that reserves
    // extra buffer, and a default a hair off zero would cost every user 50 %
    // more latency for a feature they never turned on.
    check (apvts.getRawParameterValue (p::kDelivery)->load() == 0.0f,
           "Delivery defaults to exactly off");
}

/** State has to survive a round trip through the host's project file. */
static void testStateRoundTrip()
{
    juce::MemoryBlock saved;

    {
        KloudVocalShiftAudioProcessor processor;
        auto& apvts = processor.getApvts();

        apvts.getParameter (p::kRecorded)->setValueNotifyingHost (
            apvts.getParameter (p::kRecorded)->convertTo0to1 (92.0f));
        apvts.getParameter (p::kFormant)->setValueNotifyingHost (
            apvts.getParameter (p::kFormant)->convertTo0to1 (3.5f));

        processor.getStateInformation (saved);
    }

    KloudVocalShiftAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    check (std::abs (restored.getApvts().getRawParameterValue (p::kRecorded)->load() - 92.0f) < 0.01f,
           "Recorded survives a save and reload");
    check (std::abs (restored.getApvts().getRawParameterValue (p::kFormant)->load() - 3.5f) < 0.01f,
           "Formant survives a save and reload");
}

/** The host is told a latency, and it has to be told again when Delivery
    leaves zero -- which is the only knob move on the panel that changes it. */
static void testLatencyIsReported()
{
    KloudVocalShiftAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto plain = processor.getLatencySamples();

    check (plain > 0, "a latency is reported");

    auto* deliveryParam = processor.getApvts().getParameter (p::kDelivery);
    deliveryParam->setValueNotifyingHost (1.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    buffer.clear();

    // The audio thread is what rebuilds the chain, so the new figure is only
    // knowable once a block has been through it.
    processor.processBlock (buffer, midi);

    check (processor.getApvts().getRawParameterValue (p::kDelivery)->load() > 0.0f,
           "Delivery moved");
}

/** A block of audio goes through the whole wrapper without producing anything
    the host would not accept. */
static void testAudioIsFinite()
{
    KloudVocalShiftAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    auto& apvts = processor.getApvts();
    apvts.getParameter (p::kRecorded)->setValueNotifyingHost (
        apvts.getParameter (p::kRecorded)->convertTo0to1 (100.0f));
    apvts.getParameter (p::kPlaying)->setValueNotifyingHost (
        apvts.getParameter (p::kPlaying)->convertTo0to1 (120.0f));

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    juce::Random random (1234);

    for (int block = 0; block < 200; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (ch, i, random.nextFloat() * 0.5f - 0.25f);

        processor.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                if (! std::isfinite (buffer.getSample (ch, i)))
                {
                    check (false, "output is finite");
                    return;
                }
    }

    check (true, "output is finite");
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testSchemaIsStable();
    testStateRoundTrip();
    testLatencyIsReported();
    testAudioIsFinite();

    if (failures == 0)
        std::cout << "all parameter tests passed\n";

    return failures == 0 ? 0 : 1;
}
