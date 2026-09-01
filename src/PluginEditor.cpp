#include "PluginEditor.h"

using namespace kloudvocalshift;

namespace
{
    constexpr int kWidth = 700, kHeight = 208;
    constexpr int kMargin = 12, kGap = 10, kSectionPad = 8;
    constexpr int kHeader = 20, kReadout = 26, kCaption = 13, kFooter = 19;
    constexpr int kWarpWidth = 158, kCharacterWidth = 288, kMeters = 40;

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

          s.recorded  = [&p] { return raw (p.getApvts(), params::kRecorded); };
          // Not the parameter: with Follow Host on, the tempo in use is the
          // host's, and the panel has to agree with the audio.
          s.playing   = [&p] { return p.getPlayingTempo(); };
          s.amount    = [&p] { return raw (p.getApvts(), params::kAmount); };
          s.latencyMs = [&p] { return p.getLatencyMs(); };

          return s;
      }()),
      recorded   (p.getApvts(), params::kRecorded,   "Recorded",  false),
      playing    (p.getApvts(), params::kPlaying,    "Playing",   false),
      amount     (p.getApvts(), params::kAmount,     "Amount",    false),
      lock       (p.getApvts(), params::kLock,       "Lock",      false),
      formant    (p.getApvts(), params::kFormant,    "Formant",   true),
      delivery   (p.getApvts(), params::kDelivery,   "Delivery",  false),
      mix        (p.getApvts(), params::kMix,        "Mix",       false),
      trim       (p.getApvts(), params::kTrim,       "Trim",      true),
      followHost (p.getApvts(), params::kFollowHost, "FOLLOW HOST"),
      bypass     (p.getApvts(), params::kBypass,     "BYPASS"),
      inputMeter  ("IN",  [&p] { return juce::jmax (p.getInputPeak (0),  p.getInputPeak (1)); }),
      outputMeter ("OUT", [&p] { return juce::jmax (p.getOutputPeak (0), p.getOutputPeak (1)); })
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (readout);

    for (auto* knob : { &recorded, &playing, &amount, &lock, &formant, &delivery, &mix, &trim })
    {
        knob->setKnobDiameter (40);
        addAndMakeVisible (*knob);
    }

    for (auto* toggle : { &followHost, &bypass })
        addAndMakeVisible (*toggle);

    // The items say what the control is, so it needs no caption of its own.
    windowChooser.addItemList ({ "Short window", "Normal window", "Long window" }, 1);
    styleChooser (windowChooser);

    windowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.getApvts(), params::kWindow, windowChooser);

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

void KloudVocalShiftAudioProcessorEditor::styleChooser (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, theme::panelDeep);
    box.setColour (juce::ComboBox::outlineColourId, theme::outline);
    box.setColour (juce::ComboBox::textColourId, theme::text);
    box.setColour (juce::ComboBox::arrowColourId, theme::textDim);
    addAndMakeVisible (box);
}

//==============================================================================
void KloudVocalShiftAudioProcessorEditor::timerCallback()
{
    // Playing stays the stored fallback while Follow Host is on -- it is what an
    // offline render uses when the host reports no tempo -- so the knob is
    // greyed rather than hidden.
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

    bounds.removeFromTop (kHeader);
    readout.setBounds (bounds.removeFromTop (kReadout));
    bounds.removeFromTop (kGap);

    auto meters = bounds.removeFromRight (kMeters);
    inputMeter.setBounds  (meters.removeFromTop (meters.getHeight() / 2).reduced (3, 0));
    outputMeter.setBounds (meters.reduced (3, 0));
    bounds.removeFromRight (kGap);

    warpSection = bounds.removeFromLeft (kWarpWidth);
    bounds.removeFromLeft (kGap);
    characterSection = bounds.removeFromLeft (kCharacterWidth);
    bounds.removeFromLeft (kGap);
    outputSection = bounds;

    // Every section is a row of knobs over a single footer control, so one
    // function lays out all three rather than three near-identical blocks.
    const auto layOut = [] (juce::Rectangle<int> section,
                            std::initializer_list<gui::LabelledKnob*> knobs,
                            juce::Component& footerControl)
    {
        auto inner = section.reduced (kSectionPad);
        inner.removeFromTop (kCaption);

        auto row = inner.removeFromTop ((*knobs.begin())->getPreferredHeight());
        const auto cell = row.getWidth() / (int) knobs.size();

        for (auto* knob : knobs)
            knob->setBounds (row.removeFromLeft (cell));

        footerControl.setBounds (inner.removeFromBottom (kFooter));
    };

    layOut (warpSection,      { &recorded, &playing },                    followHost);
    layOut (characterSection, { &amount, &lock, &formant, &delivery },    windowChooser);
    layOut (outputSection,    { &mix, &trim },                            bypass);
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
    g.drawText (caption, area.reduced (kSectionPad, 5).removeFromTop (kCaption),
                juce::Justification::topLeft, false);
}

void KloudVocalShiftAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto title = getLocalBounds().reduced (kMargin).removeFromTop (kHeader);

    g.setColour (theme::text);
    g.setFont (theme::labelFont (13.0f));
    g.drawText ("KLOUDVOCALSHIFT", title, juce::Justification::topLeft, false);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    g.drawText ("warp character, in place",
                title.withTrimmedLeft (126), juce::Justification::topLeft, false);
    g.drawText ("offline / mixing - not for monitoring a take",
                title, juce::Justification::topRight, false);

    drawSection (g, warpSection,      "WARP");
    drawSection (g, characterSection, "CHARACTER");
    drawSection (g, outputSection,    "OUTPUT");
}
