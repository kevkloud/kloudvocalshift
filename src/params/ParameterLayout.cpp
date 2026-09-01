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

    // Lock is how rigidly each harmonic is held together. 100 % is identity
    // phase locking, which is what Live does and what keeps the level up. Down
    // at 0 it is the naive phase vocoder every bin propagated on its own, which
    // is the hollow underwater sound of a bad time-stretch -- and is a thing
    // people want on purpose.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kLock, kVersionHint }, "Lock",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        Attributes {}.withLabel ("%").withStringFromValueFunction (percent)));

    // Delivery shortens syllables and lengthens the gaps between them, which is
    // what a performance sounds like when it was sung to a slower click. It
    // defaults to off: it is the one control here that is not reproducing
    // anything a warp does to the signal, and it is the one that reserves extra
    // buffer and so changes the reported latency the moment it leaves zero.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kDelivery, kVersionHint }, "Delivery",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f,
        Attributes {}.withLabel ("%").withStringFromValueFunction (percent)));

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
