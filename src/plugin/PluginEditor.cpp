#include "PluginEditor.h"
#include "../core/MagDrumPaths.h"
#include "../core/ScopedFileAccess.h"

MagDrumAudioProcessorEditor::MagDrumAudioProcessorEditor(MagDrumAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p), indexedList("Indexed samples", this)
{
    setSize(560, 500);
    for (int note = 0; note < 128; ++note)
        noteSelect.addItem("MIDI " + juce::String(note), note + 1);
    noteSelect.setSelectedId(37); // GM bass drum 1, MIDI note 36
    noteSelect.onChange = [this] { updateLayerSummary(); };
    addAndMakeVisible(noteSelect);
    velocityRange.setText("1-127");
    velocityRange.setInputRestrictions(7, "0123456789-");
    velocityRange.setTooltip("Inclusive velocity range for this sample layer (1–127).");
    addAndMakeVisible(velocityRange);
    sampleSearch.setTextToShowWhenEmpty("Search indexed samples", juce::Colour(0xff77736c));
    addAndMakeVisible(sampleSearch);
    searchButton.addListener(this);
    addAndMakeVisible(searchButton);
    indexedList.setModel(this);
    indexedList.setRowHeight(30);
    addAndMakeVisible(indexedList);
    layerSummary.setColour(juce::Label::textColourId, juce::Colour(0xffc6c1b7));
    layerSummary.setFont(12.0f);
    layerSummary.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(layerSummary);
    assignButton.addListener(this);
    addAndMakeVisible(assignButton);
    clearButton.addListener(this);
    addAndMakeVisible(clearButton);
    loadButton.addListener(this);
    addAndMakeVisible(loadButton);
    status.setColour(juce::Label::textColourId, juce::Colour(0xffaaa49b));
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);
    const auto dataDirectory = getMagDrumDataDirectory();
    if (dataDirectory == juce::File())
        status.setText("Shared MagDrum storage is unavailable. Check the signed app-group entitlement.", juce::dontSendNotification);
    else if (!dataDirectory.createDirectory().wasOk() && !dataDirectory.isDirectory())
        status.setText("Could not create MagDrum's shared data folder.", juce::dontSendNotification);
    else if (!database.open(dataDirectory.getChildFile("samples.sqlite3")))
        status.setText("Could not open MagDrum's shared sound database.", juce::dontSendNotification);
    else
        indexedSamples = database.searchPaths({}, 100);
    updateLayerSummary();
}

void MagDrumAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101215));
    g.setColour(juce::Colour(0xffff8a42));
    g.setFont(32.0f);
    g.drawText("MagDrum", 24, 20, 460, 48, juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(17.0f);
    g.drawText("Build a velocity-layered drum kit", 24, 78, 460, 28, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffaaa49b));
    g.setFont(14.0f);
    g.drawText("Search the standalone app's sample index or load a file directly.", 24, 112, 500, 24, juce::Justification::centredLeft);
    g.drawText("Assign non-overlapping velocity ranges to each MIDI note.", 24, 138, 500, 24, juce::Justification::centredLeft);
}

void MagDrumAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    area.removeFromTop(174);
    auto row = area.removeFromTop(32);
    sampleSearch.setBounds(row.removeFromLeft(280));row.removeFromLeft(8);searchButton.setBounds(row.removeFromLeft(130));
    area.removeFromTop(5);indexedList.setBounds(area.removeFromTop(105));area.removeFromTop(5);
    layerSummary.setBounds(area.removeFromTop(24));area.removeFromTop(5);
    row=area.removeFromTop(30);noteSelect.setBounds(row.removeFromLeft(120));row.removeFromLeft(7);velocityRange.setBounds(row.removeFromLeft(90));row.removeFromLeft(7);assignButton.setBounds(row.removeFromLeft(130));row.removeFromLeft(7);clearButton.setBounds(row.removeFromLeft(145));
    area.removeFromTop(5);loadButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(5);status.setBounds(area.removeFromTop(24));
}

void MagDrumAudioProcessorEditor::updateLayerSummary()
{
    const int note = noteSelect.getSelectedId() - 1;
    if (note < 0 || note > 127)
    {
        layerSummary.setText("Choose a MIDI note to view its velocity layers.", juce::dontSendNotification);
        return;
    }
    const auto layers = processor.getSampleLayersForNote(note);
    juce::String summary = "MIDI " + juce::String(note) + ": ";
    if (layers.empty())
        summary += "no sample layers assigned";
    else
    {
        for (size_t index = 0; index < layers.size(); ++index)
        {
            if (index > 0)
                summary += "  ·  ";
            summary += juce::String(layers[index].minimumVelocity) + "-"
                + juce::String(layers[index].maximumVelocity) + " "
                + juce::File(layers[index].path).getFileName();
        }
    }
    layerSummary.setTooltip(summary);
    layerSummary.setText(summary, juce::dontSendNotification);
}

int MagDrumAudioProcessorEditor::getNumRows(){return indexedSamples.size();}

void MagDrumAudioProcessorEditor::paintListBoxItem(int row,juce::Graphics& g,int width,int height,bool selected)
{
    if(!juce::isPositiveAndBelow(row,indexedSamples.size()))return;
    g.fillAll(selected?juce::Colour(0xff30343a):juce::Colour(0xff17191c));
    g.setColour(juce::Colour(0xfff0eee9));g.setFont(13.0f);const auto file=indexedSamples.getReference(row);
    g.drawText(file.getFileName(),8,2,width-16,15,juce::Justification::centredLeft,true);
    g.setColour(juce::Colour(0xff8e918f));g.setFont(10.0f);g.drawText(file.getParentDirectory().getFullPathName(),8,17,width-16,height-17,juce::Justification::centredLeft,true);
}

void MagDrumAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &searchButton)
    {
        indexedSamples=database.searchPaths(sampleSearch.getText(),200);
        indexedList.updateContent();indexedList.deselectAllRows();
        status.setText(juce::String(indexedSamples.size())+" indexed samples shown.",juce::dontSendNotification);
        return;
    }
    const int note = noteSelect.getSelectedId() - 1;
    const auto rangeText = velocityRange.getText().trim();
    const int separator = rangeText.indexOfChar('-');
    const int minimumVelocity = separator >= 0 ? rangeText.substring(0, separator).getIntValue() : rangeText.getIntValue();
    const int maximumVelocity = separator >= 0 ? rangeText.substring(separator + 1).getIntValue() : minimumVelocity;
    if (minimumVelocity < 1 || maximumVelocity > 127 || minimumVelocity > maximumVelocity)
    {
        status.setText("Enter a velocity range from 1 to 127, for example 1-63.", juce::dontSendNotification);
        return;
    }
    if(button==&assignButton)
    {
        const int selected=indexedList.getSelectedRow();
        if(!juce::isPositiveAndBelow(selected,indexedSamples.size())){status.setText("Select a sample from the index first.",juce::dontSendNotification);return;}
        const auto file=indexedSamples.getReference(selected);
        status.setText(processor.loadSampleForVelocityRange(note,minimumVelocity,maximumVelocity,file)
            ?"Assigned "+file.getFileName()+" to MIDI "+juce::String(note)+" at velocity "+juce::String(minimumVelocity)+"-"+juce::String(maximumVelocity)
            :"Could not assign the sample. Check for overlapping ranges or the eight-layer limit.",juce::dontSendNotification);
        updateLayerSummary();
        return;
    }
    if(button==&clearButton)
    {
        status.setText(processor.clearSamplesForNote(note)?"Cleared all velocity layers for MIDI "+juce::String(note):"That MIDI note has no assigned layers.",juce::dontSendNotification);
        updateLayerSummary();
        return;
    }
    if(button!=&loadButton)return;
    chooser = std::make_unique<juce::FileChooser>("Choose a WAV sample", juce::File(),
        "*.wav;*.aif;*.aiff;*.flac", true, false, this);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, note, minimumVelocity, maximumVelocity](const juce::FileChooser& fileChooser)
        {
            if (safe == nullptr) return;
            juce::File file;
#if JUCE_IOS
            const auto url = fileChooser.getURLResult();
            if (url.isEmpty()) return;
            const auto selectedFile = juce::File(url.getFileName());
            const auto importDirectory=getMagDrumDataDirectory().getChildFile("Imported Audio");
            if(!importDirectory.createDirectory().wasOk()&&!importDirectory.isDirectory()){safe->status.setText("Could not create shared audio storage.",juce::dontSendNotification);return;}
            const auto importedFile=importDirectory.getNonexistentChildFile(selectedFile.getFileNameWithoutExtension(),selectedFile.getFileExtension(),false);
            juce::String importError;
            if(!MagDrumFiles::copyURLToFile(url,importedFile,{},importError)){safe->status.setText(importError,juce::dontSendNotification);return;}
            file=importedFile;
#else
            file = fileChooser.getResult();
            if (file == juce::File()) return;
#endif
            if (safe->processor.loadSampleForVelocityRange(note, minimumVelocity, maximumVelocity, file))
            {
                safe->status.setText("Loaded for MIDI " + juce::String(note) + " velocity "
                    + juce::String(minimumVelocity) + "-" + juce::String(maximumVelocity) + ": " + file.getFileName(), juce::dontSendNotification);
                safe->updateLayerSummary();
            }
            else
                safe->status.setText("Could not load sample; check the range overlap, layer limit and 10-minute maximum.", juce::dontSendNotification);
        });
}
