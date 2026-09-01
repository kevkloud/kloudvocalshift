#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "params/ParameterLayout.h"
#include "dsp/DspCore.h"
#include <array>
#include <atomic>

//==============================================================================
/** The host-facing wrapper. Everything that touches audio lives in
    kloudvocalshift::DspCore, which has no JUCE dependency; this class moves
    parameter values into it, reads the host tempo, reports latency and keeps
    the editor supplied.
*/
class KloudVocalShiftAudioProcessor final : public juce::AudioProcessor,
                                            private juce::AudioProcessorValueTreeState::Listener,
                                            private juce::AsyncUpdater
{
public:
    KloudVocalShiftAudioProcessor();
    ~KloudVocalShiftAudioProcessor() override;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }

    /** The overlap-add still holds a window of signal when the input stops. */
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Ours ==================================================================
    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }

    /** What the panel reads out. Published by the audio thread, sampled by the
        editor on a timer; never pushed the other way. */
    float getPlayingTempo() const noexcept { return playingTempo.load (std::memory_order_relaxed); }
    float getRatio() const noexcept        { return currentRatio.load (std::memory_order_relaxed); }

    /** What the host has been told, in milliseconds. On the panel because it is
        large, it changes with Passes, and a producer needs to know it before
        they reach for the plugin on a live-monitored take. */
    float getLatencyMs() const noexcept    { return latencyMs.load (std::memory_order_relaxed); }

    float getInputPeak  (int ch) const noexcept { return read (inputPeak,  ch); }
    float getOutputPeak (int ch) const noexcept { return read (outputPeak, ch); }

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;

    kloudvocalshift::DspCore::Params currentParams() noexcept;

    static float read (const std::array<std::atomic<float>, 2>& a, int ch) noexcept
    {
        return a[(size_t) juce::jlimit (0, 1, ch)].load (std::memory_order_relaxed);
    }

    // Recorded in prepareToPlay rather than read back through getSampleRate(),
    // which is only populated once the wrapper has called
    // setRateAndBufferSizeDetails -- a step prepareToPlay does not perform.
    std::atomic<double> sampleRateForTail { 0.0 };

    juce::AudioProcessorValueTreeState apvts;

    // Resolved once. A string lookup per block on the audio thread is a hash
    // per parameter per buffer, for no reason.
    std::atomic<float>* recordedParam   = nullptr;
    std::atomic<float>* playingParam    = nullptr;
    std::atomic<float>* followHostParam = nullptr;
    std::atomic<float>* amountParam     = nullptr;
    std::atomic<float>* lockParam       = nullptr;
    std::atomic<float>* deliveryParam   = nullptr;
    std::atomic<float>* formantParam    = nullptr;
    std::atomic<float>* windowParam     = nullptr;
    std::atomic<float>* mixParam        = nullptr;
    std::atomic<float>* trimParam       = nullptr;
    std::atomic<float>* bypassParam     = nullptr;

    kloudvocalshift::DspCore dsp;

    std::atomic<float> playingTempo { 120.0f };
    std::atomic<float> currentRatio { 1.0f };
    std::atomic<float> latencyMs { 0.0f };

    // Latency is only pushed to the host when it actually changes. Automating
    // Window otherwise floods the host with setLatencySamples calls.
    std::atomic<int> reportedLatency { -1 };
    std::atomic<bool> latencyNeedsPublishing { false };

    std::array<std::atomic<float>, 2> inputPeak  { };
    std::array<std::atomic<float>, 2> outputPeak { };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KloudVocalShiftAudioProcessor)
};
