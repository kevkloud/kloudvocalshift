#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace kloudvocalshift;

namespace
{
    /** Window is the only parameter that changes the frame size, and so the
        only one that can change the reported latency. */
    /** Window and Passes are the only parameters that change the chain's shape,
        and so the only ones that can change the reported latency. */
    const juce::StringArray kLatencyAffecting { params::kWindow, params::kPasses };
}

KloudVocalShiftAudioProcessor::KloudVocalShiftAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KLOUDVOCALSHIFT", params::create())
{
    recordedParam   = apvts.getRawParameterValue (params::kRecorded);
    playingParam    = apvts.getRawParameterValue (params::kPlaying);
    followHostParam = apvts.getRawParameterValue (params::kFollowHost);
    amountParam     = apvts.getRawParameterValue (params::kAmount);
    passesParam     = apvts.getRawParameterValue (params::kPasses);
    formantParam    = apvts.getRawParameterValue (params::kFormant);
    windowParam     = apvts.getRawParameterValue (params::kWindow);
    mixParam        = apvts.getRawParameterValue (params::kMix);
    trimParam       = apvts.getRawParameterValue (params::kTrim);
    bypassParam     = apvts.getRawParameterValue (params::kBypass);

    for (const auto& id : kLatencyAffecting)
        apvts.addParameterListener (id, this);
}

KloudVocalShiftAudioProcessor::~KloudVocalShiftAudioProcessor()
{
    for (const auto& id : kLatencyAffecting)
        apvts.removeParameterListener (id, this);
}

//==============================================================================
void KloudVocalShiftAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRateForTail.store (sampleRate, std::memory_order_relaxed);

    dsp.prepare (sampleRate, maximumExpectedSamplesPerBlock,
                 juce::jmax (1, getTotalNumInputChannels()));

    dsp.setParams (currentParams());

    reportedLatency.store (dsp.getLatencySamples(), std::memory_order_relaxed);
    latencyMs.store ((float) (1000.0 * dsp.getLatencySamples() / sampleRate),
                     std::memory_order_relaxed);
    setLatencySamples (dsp.getLatencySamples());
}

void KloudVocalShiftAudioProcessor::releaseResources()
{
    dsp.reset();
}

double KloudVocalShiftAudioProcessor::getTailLengthSeconds() const
{
    const auto rate = sampleRateForTail.load (std::memory_order_relaxed);

    return rate > 0.0 ? (double) dsp.getLatencySamples() / rate : 0.0;
}

bool KloudVocalShiftAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
DspCore::Params KloudVocalShiftAudioProcessor::currentParams() noexcept
{
    DspCore::Params p;

    const auto recorded = juce::jmax (1.0f, recordedParam->load (std::memory_order_relaxed));
    auto playing = playingParam->load (std::memory_order_relaxed);

    if (followHostParam->load (std::memory_order_relaxed) > 0.5f)
    {
        // Offline renders and hosts without a transport report nothing; the
        // stored Playing value is the fallback rather than a silent 120.
        if (auto* head = getPlayHead())
            if (auto position = head->getPosition())
                if (auto hostBpm = position->getBpm())
                    playing = (float) *hostBpm;
    }

    playingTempo.store (playing, std::memory_order_relaxed);

    p.ratio        = juce::jlimit (0.25f, 4.0f, playing / recorded);
    p.amount       = amountParam->load (std::memory_order_relaxed);
    p.passes       = 1 + juce::jlimit (0, WarpChain::kMaxPasses - 1,
                             (int) passesParam->load (std::memory_order_relaxed));
    p.formantSemis = formantParam->load (std::memory_order_relaxed);
    p.mixPercent   = mixParam->load (std::memory_order_relaxed);
    p.trimDb       = trimParam->load (std::memory_order_relaxed);
    p.bypass       = bypassParam->load (std::memory_order_relaxed) > 0.5f;
    p.window       = (Window) juce::jlimit (0, 2,
                         (int) windowParam->load (std::memory_order_relaxed));

    currentRatio.store (p.ratio, std::memory_order_relaxed);

    return p;
}

void KloudVocalShiftAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numInputs  = getTotalNumInputChannels();
    const auto numOutputs = getTotalNumOutputChannels();

    for (int ch = numInputs; ch < numOutputs; ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto channels = juce::jmin (numInputs, numOutputs, DspCore::kMaxChannels);

    if (channels <= 0 || numSamples <= 0)
        return;

    for (int ch = 0; ch < channels; ++ch)
        inputPeak[(size_t) ch].store (buffer.getMagnitude (ch, 0, numSamples),
                                      std::memory_order_relaxed);

    if (dsp.setParams (currentParams()))
        latencyNeedsPublishing.store (true, std::memory_order_relaxed);

    dsp.process (buffer.getArrayOfWritePointers(), channels, numSamples);

    for (int ch = 0; ch < channels; ++ch)
        outputPeak[(size_t) ch].store (buffer.getMagnitude (ch, 0, numSamples),
                                       std::memory_order_relaxed);

    // Anything the DSP did not touch (mono in, stereo out) would otherwise still
    // hold the untreated input.
    for (int ch = channels; ch < numOutputs; ++ch)
        buffer.copyFrom (ch, 0, buffer, channels - 1, 0, numSamples);

    if (latencyNeedsPublishing.load (std::memory_order_relaxed))
        triggerAsyncUpdate();
}

//==============================================================================
void KloudVocalShiftAudioProcessor::parameterChanged (const juce::String&, float)
{
    // The frame size only actually changes inside DspCore::setParams, on the
    // audio thread, so the host is told from handleAsyncUpdate once that has
    // happened -- not from here, where the new latency is not yet known.
    latencyNeedsPublishing.store (true, std::memory_order_relaxed);
    triggerAsyncUpdate();
}

void KloudVocalShiftAudioProcessor::handleAsyncUpdate()
{
    const auto latency = dsp.getLatencySamples();

    if (latency != reportedLatency.exchange (latency, std::memory_order_relaxed))
    {
        const auto rate = sampleRateForTail.load (std::memory_order_relaxed);

        latencyMs.store (rate > 0.0 ? (float) (1000.0 * latency / rate) : 0.0f,
                         std::memory_order_relaxed);
        setLatencySamples (latency);
    }

    latencyNeedsPublishing.store (false, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* KloudVocalShiftAudioProcessor::createEditor()
{
    return new KloudVocalShiftAudioProcessorEditor (*this);
}

void KloudVocalShiftAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", params::kStateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KloudVocalShiftAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KloudVocalShiftAudioProcessor();
}
