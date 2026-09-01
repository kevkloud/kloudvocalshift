#pragma once

#include "PluginProcessor.h"
#include "gui/Controls.h"
#include "gui/WarpReadout.h"
#include <memory>

/** The panel.

    Three groups and a reporting strip, in one screenful with no dead space.
    Left is the warp -- the two tempos and whether the second comes from the
    host. Middle is the character of it. Right is output.

    Inside CHARACTER the order is how far, how destroyed, how thin, how rushed,
    which is roughly the order anyone reaches for them. The window selector
    carries its own meaning rather than a caption. The finish is FrostyEQ's and
    KloudFormant's, which is Ableton's: flat, no bevels, thin value arcs, a dark
    well for anything that reports.
*/
class KloudVocalShiftAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit KloudVocalShiftAudioProcessorEditor (KloudVocalShiftAudioProcessor&);
    ~KloudVocalShiftAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& caption) const;
    void styleChooser (juce::ComboBox&);

    KloudVocalShiftAudioProcessor& processorRef;
    kloudvocalshift::gui::KloudVocalShiftLookAndFeel lookAndFeel;

    kloudvocalshift::gui::WarpReadout readout;

    kloudvocalshift::gui::LabelledKnob recorded, playing;
    kloudvocalshift::gui::LabelledKnob amount, lock, formant, delivery;
    kloudvocalshift::gui::LabelledKnob mix, trim;

    kloudvocalshift::gui::SwitchButton followHost, bypass;

    juce::ComboBox windowChooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> windowAttachment;

    kloudvocalshift::gui::LevelMeter inputMeter, outputMeter;

    juce::Rectangle<int> warpSection, characterSection, outputSection;

    bool playingKnobEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KloudVocalShiftAudioProcessorEditor)
};
