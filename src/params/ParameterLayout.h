#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace kloudvocalshift::params
{

//==============================================================================
// Parameter IDs.
//
// Permanent and append-only. Once a set has been saved, automation lanes and
// stored state are keyed by these exact strings; renaming or reordering one
// silently loses that setting in every existing project. Treat a change here
// the way you would treat a wire-protocol change.
//==============================================================================

inline constexpr auto kRecorded   = "recorded_bpm";
inline constexpr auto kPlaying    = "playing_bpm";
inline constexpr auto kFollowHost = "follow_host";
inline constexpr auto kAmount     = "amount";
inline constexpr auto kPasses     = "passes";
inline constexpr auto kFormant    = "formant";
inline constexpr auto kWindow     = "window";
inline constexpr auto kMix        = "mix";
inline constexpr auto kTrim       = "trim";
inline constexpr auto kBypass     = "bypass";

inline constexpr int kVersionHint  = 1;
inline constexpr int kStateVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout create();

} // namespace kloudvocalshift::params
