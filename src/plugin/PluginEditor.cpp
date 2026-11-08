#include "PluginEditor.h"

MagDrumAudioProcessorEditor::MagDrumAudioProcessorEditor(MagDrumAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p)
{
    setSize(520, 270);
    for (int note = 0; note < 128; ++note)
        noteSelect.addItem("MIDI " + juce::String(note), note + 1);
    noteSelect.setSelectedId(37); // GM bass drum 1, MIDI note 36
    addAndMakeVisible(noteSelect);
    loadButton.addListener(this);
    addAndMakeVisible(loadButton);
    status.setColour(juce::Label::textColourId, juce::Colour(0xffaaa49b));
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);
}

void MagDrumAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101215));
    g.setColour(juce::Colour(0xffff8a42));
    g.setFont(32.0f);
    g.drawText("MagDrum", 24, 20, 460, 48, juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(17.0f);
    g.drawText("Load a WAV sample onto a MIDI drum note", 24, 78, 460, 28, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffaaa49b));
    g.setFont(14.0f);
    g.drawText("Samples are read before playback and saved with the plugin state.", 24, 112, 470, 24, juce::Justification::centredLeft);
    g.drawText("Use GM notes such as 36 kick, 38 snare, 42 closed hi-hat.", 24, 138, 470, 24, juce::Justification::centredLeft);
}

void MagDrumAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    area.removeFromTop(174);
    auto row = area.removeFromTop(34);
    noteSelect.setBounds(row.removeFromLeft(175));
    row.removeFromLeft(10);
    loadButton.setBounds(row.removeFromLeft(180));
    area.removeFromTop(12);
    status.setBounds(area.removeFromTop(28));
}

void MagDrumAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button != &loadButton) return;
    const int note = noteSelect.getSelectedId() - 1;
    chooser = std::make_unique<juce::FileChooser>("Choose a WAV sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, note](const juce::FileChooser& fileChooser)
        {
            if (safe == nullptr) return;
            const auto file = fileChooser.getResult();
            if (file == juce::File()) return;
            if (safe->processor.loadSampleForNote(note, file))
                safe->status.setText("Loaded for MIDI " + juce::String(note) + ": " + file.getFileName(), juce::dontSendNotification);
            else
                safe->status.setText("Could not read sample (max 10 minutes per note).", juce::dontSendNotification);
        });
}
