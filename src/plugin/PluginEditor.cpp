#include "PluginEditor.h"
#include "../core/MagDrumPaths.h"
#include "../core/ScopedFileAccess.h"

MagDrumAudioProcessorEditor::MagDrumAudioProcessorEditor(MagDrumAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p), indexedList("Indexed samples", this)
{
    setSize(560, 440);
    for (int note = 0; note < 128; ++note)
        noteSelect.addItem("MIDI " + juce::String(note), note + 1);
    noteSelect.setSelectedId(37); // GM bass drum 1, MIDI note 36
    addAndMakeVisible(noteSelect);
    sampleSearch.setTextToShowWhenEmpty("Search indexed samples", juce::Colour(0xff77736c));
    addAndMakeVisible(sampleSearch);
    searchButton.addListener(this);
    addAndMakeVisible(searchButton);
    indexedList.setModel(this);
    indexedList.setRowHeight(30);
    addAndMakeVisible(indexedList);
    assignButton.addListener(this);
    addAndMakeVisible(assignButton);
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
    g.drawText("Search the standalone app's sample index or load a file directly.", 24, 112, 500, 24, juce::Justification::centredLeft);
    g.drawText("Choose a MIDI note, select a sample, and assign it to the kit.", 24, 138, 500, 24, juce::Justification::centredLeft);
}

void MagDrumAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    area.removeFromTop(174);
    auto row = area.removeFromTop(32);
    sampleSearch.setBounds(row.removeFromLeft(280));row.removeFromLeft(8);searchButton.setBounds(row.removeFromLeft(130));
    area.removeFromTop(6);indexedList.setBounds(area.removeFromTop(155));area.removeFromTop(7);
    row=area.removeFromTop(32);noteSelect.setBounds(row.removeFromLeft(145));row.removeFromLeft(7);assignButton.setBounds(row.removeFromLeft(140));row.removeFromLeft(7);loadButton.setBounds(row.removeFromLeft(170));
    area.removeFromTop(6);status.setBounds(area.removeFromTop(26));
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
    if(button==&assignButton)
    {
        const int selected=indexedList.getSelectedRow();
        if(!juce::isPositiveAndBelow(selected,indexedSamples.size())){status.setText("Select a sample from the index first.",juce::dontSendNotification);return;}
        const auto file=indexedSamples.getReference(selected);
        status.setText(processor.loadSampleForNote(note,file)?"Assigned "+file.getFileName()+" to MIDI "+juce::String(note):"Could not load indexed sample.",juce::dontSendNotification);
        return;
    }
    if(button!=&loadButton)return;
    chooser = std::make_unique<juce::FileChooser>("Choose a WAV sample", juce::File(),
        "*.wav;*.aif;*.aiff;*.flac", true, false, this);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, note](const juce::FileChooser& fileChooser)
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
            if (safe->processor.loadSampleForNote(note, file))
                safe->status.setText("Loaded for MIDI " + juce::String(note) + ": " + file.getFileName(), juce::dontSendNotification);
            else
                safe->status.setText("Could not read sample (max 10 minutes per note).", juce::dontSendNotification);
        });
}
