#include "PluginEditor.h"

using namespace kloudvocalshift;

namespace
{
    constexpr int kWidth = 640, kHeight = 300;
    constexpr int kMargin = 12, kSectionPad = 8, kCaptionHeight = 16;

    float raw (juce::AudioProcessorValueTreeState& s, const char* id)
    {
        if (auto* v = s.getRawParameterValue (id))
            return v->load (std::memory_order_relaxed);

        return 0.0f;
    }
}

KloudVocalShiftAudioProcessorEditor::KloudVocalShiftAudioProcessorEditor (KloudVocalShiftAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      readout ([&p]
      {
          gui::WarpReadout::Source s;

          s.recorded = [&p] { return raw (p.getApvts(), params::kRecorded); };
          // Not the parameter: with Follow Host on, the tempo actually in use is
          // the host's, and the panel has to agree with the audio.
          s.playing  = [&p] { return p.getPlayingTempo(); };
          s.amount    = [&p] { return raw (p.getApvts(), params::kAmount); };
          s.latencyMs = [&p] { return p.getLatencyMs(); };

          return s;
      }()),
      recorded   (p.getApvts(), params::kRecorded,   "Recorded",  false),
      playing    (p.getApvts(), params::kPlaying,    "Playing",   false),
      amount     (p.getApvts(), params::kAmount,     "Amount",    false),
      formant    (p.getApvts(), params::kFormant,    "Formant",   true),
      mix        (p.getApvts(), params::kMix,        "Mix",       false),
      trim       (p.getApvts(), params::kTrim,       "Trim",      true),
      followHost (p.getApvts(), params::kFollowHost, "FOLLOW HOST"),
      bypass     (p.getApvts(), params::kBypass,     "BYPASS"),
      inputMeter  ("IN",  [&p] { return juce::jmax (p.getInputPeak (0),  p.getInputPeak (1)); }),
      outputMeter ("OUT", [&p] { return juce::jmax (p.getOutputPeak (0), p.getOutputPeak (1)); })
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (readout);

    for (auto* knob : { &recorded, &playing, &amount, &formant, &mix, &trim })
        addAndMakeVisible (*knob);

    for (auto* toggle : { &followHost, &bypass })
        addAndMakeVisible (*toggle);

    windowCaption.setText ("WINDOW", juce::dontSendNotification);
    windowCaption.setJustificationType (juce::Justification::centredLeft);
    windowCaption.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (windowCaption);

    windowChooser.addItemList ({ "Short", "Normal", "Long" }, 1);
    windowChooser.setColour (juce::ComboBox::backgroundColourId, theme::panelDeep);
    windowChooser.setColour (juce::ComboBox::outlineColourId, theme::outline);
    windowChooser.setColour (juce::ComboBox::textColourId, theme::text);
    addAndMakeVisible (windowChooser);

    windowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.getApvts(), params::kWindow, windowChooser);

    passesCaption.setText ("PASSES", juce::dontSendNotification);
    passesCaption.setJustificationType (juce::Justification::centredLeft);
    passesCaption.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (passesCaption);

    passesChooser.addItemList ({ "1", "2", "3" }, 1);
    passesChooser.setColour (juce::ComboBox::backgroundColourId, theme::panelDeep);
    passesChooser.setColour (juce::ComboBox::outlineColourId, theme::outline);
    passesChooser.setColour (juce::ComboBox::textColourId, theme::text);
    addAndMakeVisible (passesChooser);

    passesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.getApvts(), params::kPasses, passesChooser);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    startTimerHz (8);
    timerCallback();

    setSize (kWidth, kHeight);
}

KloudVocalShiftAudioProcessorEditor::~KloudVocalShiftAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void KloudVocalShiftAudioProcessorEditor::timerCallback()
{
    // Playing is still the stored fallback while Follow Host is on, so the knob
    // is greyed rather than hidden -- it is what the plugin falls back to in an
    // offline render, and hiding it would make that fallback invisible.
    const auto shouldBeEnabled = raw (processorRef.getApvts(), params::kFollowHost) < 0.5f;

    if (shouldBeEnabled != playingKnobEnabled)
    {
        playingKnobEnabled = shouldBeEnabled;
        playing.setKnobEnabled (shouldBeEnabled);
    }
}

//==============================================================================
void KloudVocalShiftAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (kMargin);

    bounds.removeFromTop (22);                 // title strip

    auto meters = bounds.removeFromRight (48);
    inputMeter.setBounds  (meters.removeFromTop (meters.getHeight() / 2).reduced (4, 0));
    outputMeter.setBounds (meters.reduced (4, 0));
    bounds.removeFromRight (kMargin);

    warpSection = bounds.removeFromLeft (232);
    bounds.removeFromLeft (kMargin);

    outputSection = bounds.removeFromRight (120);
    bounds.removeFromRight (kMargin);

    characterSection = bounds;

    const auto place = [] (juce::Rectangle<int> cell, gui::LabelledKnob& knob)
    {
        knob.setBounds (cell.withSizeKeepingCentre (
            juce::jmin (cell.getWidth(), 86), knob.getPreferredHeight()));
    };

    {
        auto inner = warpSection.reduced (kSectionPad);
        inner.removeFromTop (kCaptionHeight);

        auto row = inner.removeFromTop (recorded.getPreferredHeight());
        place (row.removeFromLeft (row.getWidth() / 2), recorded);
        place (row, playing);

        inner.removeFromTop (4);
        followHost.setBounds (inner.removeFromTop (20).reduced (24, 0));
        inner.removeFromTop (6);
        readout.setBounds (inner);
    }

    {
        auto inner = characterSection.reduced (kSectionPad);
        inner.removeFromTop (kCaptionHeight);

        auto row = inner.removeFromTop (amount.getPreferredHeight());

        place (row.removeFromLeft (row.getWidth() / 2), amount);
        place (row, formant);

        inner.removeFromTop (10);

        auto footer = inner.removeFromTop (20);
        windowCaption.setBounds (footer.removeFromLeft (52));
        windowChooser.setBounds (footer.removeFromLeft (74));
        footer.removeFromLeft (10);
        passesCaption.setBounds (footer.removeFromLeft (48));
        passesChooser.setBounds (footer.removeFromLeft (52));
    }

    {
        auto inner = outputSection.reduced (kSectionPad);
        inner.removeFromTop (kCaptionHeight);

        auto row = inner.removeFromTop (mix.getPreferredHeight());
        place (row.removeFromLeft (row.getWidth() / 2), mix);
        place (row, trim);

        inner.removeFromTop (10);
        bypass.setBounds (inner.removeFromTop (20).reduced (4, 0));
    }
}

//==============================================================================
void KloudVocalShiftAudioProcessorEditor::drawSection (juce::Graphics& g, juce::Rectangle<int> area,
                                                       const juce::String& caption) const
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (area.toFloat(), theme::corner);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), theme::corner, 1.0f);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    g.drawText (caption, area.reduced (kSectionPad, 6).removeFromTop (kCaptionHeight),
                juce::Justification::topLeft, false);
}

void KloudVocalShiftAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto title = getLocalBounds().reduced (kMargin).removeFromTop (18);

    g.setColour (theme::text);
    g.setFont (theme::labelFont (13.0f));
    g.drawText ("KLOUDVOCALSHIFT", title, juce::Justification::topLeft, false);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    g.drawText ("warp character, in place",
                title.withTrimmedLeft (128), juce::Justification::topLeft, false);

    drawSection (g, warpSection,      "WARP");
    drawSection (g, characterSection, "CHARACTER");
    drawSection (g, outputSection,    "OUTPUT");
}
