#pragma once

#include "PluginProcessor.h"
#include "gui/Controls.h"
#include "gui/WarpReadout.h"
#include <memory>

/** The panel.

    Left is the warp: the two tempos and whether the second one comes from the
    host. Middle is the character of it -- how much of the vocoder's phase to
    use, how many frames to lose, how far to move the formants, and how long a
    window to do it in. Right is output.

    The readout sits under the tempos because the ratio is the thing that
    decides whether any of the rest of the panel is doing anything at all.

    The finish is FrostyEQ's and KloudFormant's, which is Ableton's: flat, no
    bevels, thin value arcs, a dark well for anything that reports rather than
    controls.
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

    KloudVocalShiftAudioProcessor& processorRef;
    kloudvocalshift::gui::KloudVocalShiftLookAndFeel lookAndFeel;

    kloudvocalshift::gui::WarpReadout readout;

    kloudvocalshift::gui::LabelledKnob recorded, playing;
    kloudvocalshift::gui::LabelledKnob amount, formant;
    kloudvocalshift::gui::LabelledKnob mix, trim;

    kloudvocalshift::gui::SwitchButton followHost, bypass;

    juce::ComboBox windowChooser, passesChooser;
    juce::Label windowCaption, passesCaption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> windowAttachment,
                                                                           passesAttachment;

    kloudvocalshift::gui::LevelMeter inputMeter, outputMeter;

    juce::Rectangle<int> warpSection, characterSection, outputSection;

    bool playingKnobEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KloudVocalShiftAudioProcessorEditor)
};
