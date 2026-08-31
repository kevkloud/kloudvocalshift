// Renders the panel to a PNG with no host and no window server.
//
//   ./build/panel out.png
//
// The editor is a pure JUCE component, so it can be built, laid out and
// snapshotted offline. That makes a layout change something you can look at in
// a second rather than something you have to load a DAW to see -- and it is the
// only way to check the panel at all on a machine without screen recording
// permission.

#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File out (juce::File::getCurrentWorkingDirectory()
                              .getChildFile (argc > 1 ? argv[1] : "panel.png"));

    KloudVocalShiftAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
        return 1;

    // No dispatch loop is needed: everything on the panel paints from a
    // getter rather than from state a timer has to arrive and fill in, so the
    // first paint is already the steady-state one.
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    juce::PNGImageFormat png;
    out.deleteFile();

    if (auto stream = out.createOutputStream())
        png.writeImageToStream (image, *stream);

    juce::Logger::writeToLog ("wrote " + out.getFullPathName());

    return 0;
}
