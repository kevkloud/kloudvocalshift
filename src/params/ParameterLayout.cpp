#include "ParameterLayout.h"

namespace kloudvocalshift::params
{

namespace
{
    juce::String percent (float v, int)
    {
        return juce::String (juce::roundToInt (v));
    }

    juce::String bpm (float v, int)
    {
        return juce::String (v, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout create()
{
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The two tempos, rather than one ratio knob, because that is the shape the
    // workflow already has: you know what you tracked at and you know what the
    // song is. The ratio is arithmetic, and arithmetic is the computer's job.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kRecorded, kVersionHint }, "Recorded",
        juce::NormalisableRange<float> { 40.0f, 220.0f }, 100.0f,
        Attributes {}.withLabel ("BPM").withStringFromValueFunction (bpm)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kPlaying, kVersionHint }, "Playing",
        juce::NormalisableRange<float> { 40.0f, 220.0f }, 120.0f,
        Attributes {}.withLabel ("BPM").withStringFromValueFunction (bpm)));

    // On by default: the tempo the song is at is a fact the host already knows,
    // and having to keep a second copy of it in sync by hand is the kind of
    // thing you only discover you got wrong after the bounce.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kFollowHost, kVersionHint }, "Follow Host", true));

    // Amount interpolates the ratio toward 1 rather than crossfading toward the
    // dry signal. Half a warp is a real thing -- it is what warping to 110
    // instead of 120 sounds like -- whereas half the wet signal is the original
    // with a blurred copy underneath it, which is a chorus. At 0 the chain is a
    // bit-exact identity whatever the two tempos say.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kAmount, kVersionHint }, "Amount",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        Attributes {}.withLabel ("%").withStringFromValueFunction (percent)));

    // Passes is stepped and changes the latency, so it is a selector rather than
    // a knob: it is a decision, not something to ride.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kPasses, kVersionHint }, "Passes",
        juce::StringArray { "1", "2", "3" }, 0));

    // Continuous rather than stepped: a snap interval is not exactly
    // representable in float, so NormalisableRange rounds the 0 default a few
    // ULPs off zero and the plugin stops being transparent at its default.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kFormant, kVersionHint }, "Formant",
        juce::NormalisableRange<float> { -12.0f, 12.0f }, 0.0f,
        Attributes {}.withLabel ("st")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return (v > 0.0f ? "+" : "") + juce::String (v, 2);
                     })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kWindow, kVersionHint }, "Window",
        juce::StringArray { "Short", "Normal", "Long" }, 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kMix, kVersionHint }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        Attributes {}.withLabel ("%").withStringFromValueFunction (percent)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTrim, kVersionHint }, "Trim",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        Attributes {}.withLabel ("dB")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return (v > 0.0f ? "+" : "") + juce::String (v, 1);
                     })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kBypass, kVersionHint }, "Bypass", false));

    return layout;
}

} // namespace kloudvocalshift::params
