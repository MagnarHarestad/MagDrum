#include <JuceHeader.h>
#include "core/MagDrumPaths.h"
#include "core/MidiPatternIO.h"
#include "core/ScopedFileAccess.h"
#include "core/SampleDatabase.h"
#include "core/WavScanner.h"

namespace
{
class MobileMainComponent final : public juce::AudioAppComponent,
                                 private juce::ListBoxModel,
                                 private juce::Button::Listener
{
public:
    MobileMainComponent()
        : rows("MagDrum library", this), dataDirectory(getMagDrumDataDirectory())
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const auto displayArea = display != nullptr ? display->userArea : juce::Rectangle<int>(760, 900);
        setSize(juce::jmin(760, displayArea.getWidth()), juce::jmin(900, displayArea.getHeight()));
        formats.registerBasicFormats();
        if (dataDirectory != juce::File()
            && (dataDirectory.createDirectory().wasOk() || dataDirectory.isDirectory()))
        {
            databaseReady = database.open(dataDirectory.getChildFile("samples.sqlite3"));
            if (databaseReady)
            {
                importDirectory = dataDirectory.getChildFile("Imported Libraries");
                importDirectory.createDirectory();
            }
        }

        title.setText("MagDrum", juce::dontSendNotification);
        title.setFont(juce::Font(30.0f, juce::Font::bold));
        addAndMakeVisible(title);
        importFolder.setButtonText("Import audio files"); importFolder.addListener(this); addAndMakeVisible(importFolder);
        cancelImport.setButtonText("Cancel"); cancelImport.addListener(this); cancelImport.setEnabled(false); addAndMakeVisible(cancelImport);
        play.setButtonText("Play selected"); play.addListener(this); addAndMakeVisible(play);
        stop.setButtonText("Stop"); stop.addListener(this); addAndMakeVisible(stop);
        importMidi.setButtonText("Import MIDI"); importMidi.addListener(this); addAndMakeVisible(importMidi);
        exportMidi.setButtonText("Export MIDI"); exportMidi.addListener(this); addAndMakeVisible(exportMidi);
        search.setTextToShowWhenEmpty("Search samples", juce::Colour(0xff77736c));
        search.onTextChange = [this] { refresh(); }; addAndMakeVisible(search);
        loadMore.setButtonText("More");
        loadMore.onClick = [this] { loadMoreSamples(); };
        loadMore.setEnabled(false);
        addAndMakeVisible(loadMore);
        const char* categories[] { "All types", "Kick", "Snare", "Closed hi-hat", "Open hi-hat", "Pedal hi-hat", "Tom", "Ride", "Crash", "Clap", "Rimshot", "Percussion", "Unknown" };
        for (int index = 0; index < 13; ++index) category.addItem(categories[index], index + 1);
        category.setSelectedId(1); category.onChange = [this] { refresh(); }; addAndMakeVisible(category);
        addAndMakeVisible(rows); rows.setRowHeight(56);
        status.setColour(juce::Label::textColourId, juce::Colour(0xffaaa49b));
        status.setText(databaseReady ? "Import audio files or search your library."
            : dataDirectory == juce::File() ? "Shared MagDrum storage is unavailable. Check the signed app-group entitlement."
                                             : "Could not open the MagDrum sound database.", juce::dontSendNotification);
        importFolder.setEnabled(databaseReady);
        importMidi.setEnabled(databaseReady);
        exportMidi.setEnabled(databaseReady);
        addAndMakeVisible(status);
        refresh();
        setAudioChannels(0, 2);
    }

    ~MobileMainComponent() override
    {
        importCancellation.store(true);
        workers.removeAllJobs(true, -1);
        transport.stop(); transport.setSource(nullptr); source.reset(); shutdownAudio();
    }

    void prepareToPlay(int blockSize, double sampleRate) override { transport.prepareToPlay(blockSize, sampleRate); }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override { transport.getNextAudioBlock(info); }
    void releaseResources() override { transport.releaseResources(); }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff101215));
        graphics.setColour(juce::Colour(0xffff8a42));
        graphics.fillRoundedRectangle(12.0f, 13.0f, 7.0f, 36.0f, 3.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        title.setBounds(area.removeFromTop(48)); area.removeFromTop(8);
        if (area.getWidth() < 640)
        {
            auto row = area.removeFromTop(44);
            importFolder.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2)); row.removeFromLeft(6); cancelImport.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(44);
            play.setBounds(row.removeFromLeft((row.getWidth() - 6) * 3 / 4)); row.removeFromLeft(6); stop.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(44);
            importMidi.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2)); row.removeFromLeft(6); exportMidi.setBounds(row);
            area.removeFromTop(8); row = area.removeFromTop(44);
            search.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2)); row.removeFromLeft(6); category.setBounds(row);
        }
        else
        {
            auto row = area.removeFromTop(42);
            importFolder.setBounds(row.removeFromLeft(180)); row.removeFromLeft(6);
            cancelImport.setBounds(row.removeFromLeft(90)); row.removeFromLeft(6);
            play.setBounds(row.removeFromLeft(125)); row.removeFromLeft(6); stop.setBounds(row.removeFromLeft(75));
            area.removeFromTop(6); row = area.removeFromTop(42);
            importMidi.setBounds(row.removeFromLeft(140)); row.removeFromLeft(6); exportMidi.setBounds(row.removeFromLeft(140));
            area.removeFromTop(8); row = area.removeFromTop(42);
            search.setBounds(row.removeFromLeft(400)); row.removeFromLeft(8); category.setBounds(row.removeFromLeft(180));
        }
        area.removeFromTop(6);
        auto footer = area.removeFromBottom(38);
        loadMore.setBounds(footer.removeFromRight(92)); footer.removeFromRight(6);
        status.setBounds(footer);
        rows.setBounds(area);
    }

private:
    class FileImportJob final : public juce::ThreadPoolJob
    {
    public:
        FileImportJob(MobileMainComponent& ownerIn, juce::Array<juce::URL> selectedFilesIn,
                        juce::File dataDirectoryIn, juce::File importDirectoryIn,
                        std::atomic<bool>& cancellationIn)
            : ThreadPoolJob("MagDrum mobile library import"), owner(&ownerIn),
              selectedFiles(std::move(selectedFilesIn)), dataDirectory(std::move(dataDirectoryIn)),
              importDirectory(std::move(importDirectoryIn)), cancellation(cancellationIn) {}

        JobStatus runJob() override
        {
            juce::String error;
            WavScanner::Stats stats;
            std::int64_t indexed = 0;
            if (selectedFiles.isEmpty()) error = "No audio files were selected.";
            else if (!importDirectory.createDirectory().wasOk() && !importDirectory.isDirectory()) error = "Could not create shared audio storage.";

            juce::StringArray sourceUrls;
            juce::StringPairArray fileNameCounts;
            for (const auto& url : selectedFiles)
            {
                sourceUrls.add(url.toString(true));
                const auto key = url.getFileName().toLowerCase();
                fileNameCounts.set(key, juce::String(fileNameCounts.getValue(key, "0").getIntValue() + 1));
            }
            sourceUrls.sort(true);
            const auto sourceIdentifier = juce::SHA256(sourceUrls.joinIntoString("\n").toUTF8()).toHexString();
            const auto importedFolder = importDirectory.getChildFile(sourceIdentifier);
            if (error.isEmpty() && !importedFolder.createDirectory().wasOk() && !importedFolder.isDirectory())
                error = "Could not create this imported library's folder.";

            if (error.isEmpty())
            {
                for (const auto& url : selectedFiles)
                {
                    if (shouldExit() || cancellation.load()) { stats.cancelled = true; break; }
                    const auto fileName = url.getFileName();
                    const auto extension = juce::File(fileName).getFileExtension().toLowerCase();
                    const auto baseName = juce::File(fileName).getFileNameWithoutExtension();
                    auto destinationName = fileName;
                    if (fileNameCounts.getValue(fileName.toLowerCase(), "0").getIntValue() > 1)
                    {
                        const auto shortId = juce::SHA256(url.toString(true).toUTF8()).toHexString().substring(0, 8);
                        destinationName = baseName + "-" + shortId + extension;
                    }
                    juce::String copyError;
                    if (!MagDrumFiles::copyURLToFile(url, importedFolder.getChildFile(destinationName),
                            [this] { return shouldExit() || cancellation.load(); }, copyError))
                    {
                        if (cancellation.load() || shouldExit()) stats.cancelled = true;
                        else ++stats.errors;
                    }
                    if (stats.cancelled) break;
                }
            }

            SampleDatabase scanDatabase;
            if (error.isEmpty() && !stats.cancelled && !scanDatabase.open(dataDirectory.getChildFile("samples.sqlite3")))
                error = "Could not open the MagDrum sound database.";
            const auto sourceId = error.isEmpty() && !stats.cancelled
                ? scanDatabase.ensureSampleSource("folder", sourceIdentifier,
                    "Imported audio (" + juce::String(selectedFiles.size()) + " files)", sourceUrls.joinIntoString("\n"))
                : std::optional<std::int64_t>();
            if (error.isEmpty() && !stats.cancelled && !sourceId) error = "Could not save this library's source record.";

            if (error.isEmpty() && !stats.cancelled)
            {
                const auto scanId = scanDatabase.beginSampleScan(importedFolder);
                if (!scanId) error = "Could not create the library scan record.";
                else
                {
                    const auto results = WavScanner{}.scan(importedFolder,
                        [this] { return shouldExit() || cancellation.load(); }, {}, &stats);
                    for (const auto& sample : results)
                        if (scanDatabase.insertSample(sample, *sourceId)) ++indexed; else ++stats.errors;
                    if (!scanDatabase.assignSamplesToSource(importedFolder, *sourceId)) ++stats.errors;
                    if (!scanDatabase.finishSampleScan(*scanId, stats.cancelled ? "cancelled" : "completed",
                            stats.filesConsidered, stats.filesSkippedUnchanged, indexed, stats.errors))
                        error = "The scan finished, but its history could not be saved.";
                }
            }

            auto safe = owner;
            juce::MessageManager::callAsync([safe, stats, indexed, error]
            {
                if (safe != nullptr) safe->finishImport(stats, indexed, error);
            });
            return jobHasFinished;
        }

    private:
        juce::Component::SafePointer<MobileMainComponent> owner;
        juce::Array<juce::URL> selectedFiles;
        juce::File dataDirectory, importDirectory;
        std::atomic<bool>& cancellation;
    };

    int getNumRows() override { return visible.size(); }

    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
    {
        if (!juce::isPositiveAndBelow(row, visible.size())) return;
        graphics.fillAll(selected ? juce::Colour(0xff30343a) : juce::Colour(0xff17191c));
        const auto& record = visible.getReference(row);
        graphics.setColour(juce::Colour(0xfff0eee9)); graphics.setFont(16.0f);
        graphics.drawText(record.file.getFileName(), 12, 5, width - 24, 24, juce::Justification::centredLeft, true);
        graphics.setColour(juce::Colour(0xffaaa49b)); graphics.setFont(12.0f);
        auto details = record.category + "  ·  " + juce::String(record.durationSeconds, 2) + " s  ·  "
            + record.file.getParentDirectory().getFileName();
        if (!record.classificationTags.isEmpty()) details += "  ·  " + record.classificationTags.joinIntoString(", ");
        graphics.drawText(details, 12, 29, width - 24, height - 30,
            juce::Justification::centredLeft, true);
    }

    void buttonClicked(juce::Button* button) override
    {
        if (button == &importFolder)
        {
            chooser = std::make_unique<juce::FileChooser>("Choose audio files to import", juce::File(),
                "*.wav;*.wave;*.aif;*.aiff;*.flac", true, false, this);
            juce::Component::SafePointer<MobileMainComponent> safe(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
                [safe](const juce::FileChooser& fileChooser)
                {
                    if (safe == nullptr) return;
                    auto files = fileChooser.getURLResults();
                    if (!files.isEmpty()) safe->beginImport(std::move(files));
                });
        }
        else if (button == &cancelImport)
        {
            importCancellation.store(true);
            status.setText("Cancelling the current scan…", juce::dontSendNotification);
        }
        else if (button == &play) playSelected();
        else if (button == &stop) transport.stop();
        else if (button == &importMidi) chooseMidiImport();
        else if (button == &exportMidi) chooseMidiExport();
    }

    void beginImport(juce::Array<juce::URL> files)
    {
        if (importRunning || !dataDirectory.isDirectory() || files.isEmpty()) return;
        importRunning = true; importCancellation.store(false);
        importFolder.setEnabled(false); cancelImport.setEnabled(true);
        status.setText("Copying and indexing audio files…", juce::dontSendNotification);
        workers.addJob(new FileImportJob(*this, std::move(files), dataDirectory, importDirectory, importCancellation), true);
    }

    void finishImport(const WavScanner::Stats& stats, std::int64_t indexed, const juce::String& error)
    {
        importRunning = false; importFolder.setEnabled(true); cancelImport.setEnabled(false);
        if (error.isNotEmpty()) status.setText(error, juce::dontSendNotification);
        else status.setText((stats.cancelled ? "Import cancelled. " : "Import complete. ")
            + juce::String(indexed) + " indexed, " + juce::String(stats.filesSkippedUnchanged)
            + " unchanged, " + juce::String(stats.errors) + " errors.", juce::dontSendNotification);
        refresh();
    }

    void refresh()
    {
        visible.clear();
        const auto query = search.getText().trim();
        const auto selectedCategory = category.getSelectedId() > 1 ? category.getText() : juce::String();
        const auto page = database.searchSampleBrowseRecords(query, selectedCategory, samplePageSize + 1, 0);
        for (int index = 0; index < page.size() && index < samplePageSize; ++index)
            visible.add(page.getReference(index));
        hasMoreSamples = page.size() > samplePageSize;
        loadMore.setEnabled(hasMoreSamples);
        rows.updateContent(); rows.repaint();
    }

    void loadMoreSamples()
    {
        if (!hasMoreSamples) return;
        const auto query = search.getText().trim();
        const auto selectedCategory = category.getSelectedId() > 1 ? category.getText() : juce::String();
        const auto page = database.searchSampleBrowseRecords(query, selectedCategory,
            samplePageSize + 1, visible.size());
        for (int index = 0; index < page.size() && index < samplePageSize; ++index)
            visible.add(page.getReference(index));
        hasMoreSamples = page.size() > samplePageSize;
        loadMore.setEnabled(hasMoreSamples);
        rows.updateContent(); rows.repaint();
    }

    void playSelected()
    {
        const int index = rows.getSelectedRow();
        if (!juce::isPositiveAndBelow(index, visible.size()))
        {
            status.setText("Select a sound before playback.", juce::dontSendNotification);
            return;
        }
        const auto file = visible.getReference(index).file;
        auto* reader = formats.createReaderFor(file);
        if (reader == nullptr)
        {
            status.setText("This sound is missing or cannot be decoded.", juce::dontSendNotification);
            return;
        }
        transport.stop(); transport.setSource(nullptr); source.reset(new juce::AudioFormatReaderSource(reader, true));
        transport.setSource(source.get(), 0, nullptr, reader->sampleRate); transport.start();
    }

    void chooseMidiImport()
    {
        chooser = std::make_unique<juce::FileChooser>("Import a Standard MIDI File", juce::File(), "*.mid;*.midi", true, false, this);
        juce::Component::SafePointer<MobileMainComponent> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser& fileChooser)
            {
                if (safe == nullptr) return;
                const auto url = fileChooser.getURLResult();
                if (url.isEmpty()) return;
                const auto importTemp = safe->dataDirectory.getChildFile("MIDI Imports")
                    .getNonexistentChildFile("MagDrum-Imported", ".mid", false);
                juce::String copyError;
                if (!MagDrumFiles::copyURLToFile(url, importTemp, {}, copyError))
                {
                    safe->status.setText(copyError, juce::dontSendNotification);
                    return;
                }
                MidiPattern imported;
                const auto report = MidiPatternIO{}.read(importTemp, safe->mappingProfile, imported);
                importTemp.deleteFile();
                if (!report.ok) { safe->status.setText(report.error, juce::dontSendNotification); return; }
                safe->pattern = std::move(imported);
                safe->status.setText("MIDI imported: " + juce::String(static_cast<int>(safe->pattern.events.size()))
                    + " semantic events, " + juce::String(static_cast<int>(report.unmappedNotes.size()))
                    + " unmapped note entries, " + juce::String(static_cast<int>(report.ambiguousNotes.size()))
                    + " ambiguous assignments.", juce::dontSendNotification);
            });
    }

    void chooseMidiExport()
    {
        if (pattern.events.empty()) { status.setText("Import a MIDI pattern before exporting.", juce::dontSendNotification); return; }
        chooser = std::make_unique<juce::FileChooser>("Export a Standard MIDI File",
            dataDirectory.getChildFile("MagDrum.mid"), "*.mid", true, false, this);
        juce::Component::SafePointer<MobileMainComponent> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser& fileChooser)
            {
                if (safe == nullptr) return;
                const auto destination = fileChooser.getURLResult();
                if (destination.isEmpty()) return;
                const auto exportTemp = safe->dataDirectory.getChildFile("MIDI Exports")
                    .getNonexistentChildFile("MagDrum-Export", ".mid", false);
                std::vector<DrumEvent> unsupported;
                juce::String copyError;
                const bool written = MidiPatternIO{}.write(exportTemp, safe->pattern, safe->mappingProfile, false, &unsupported);
                const bool copied = written && MagDrumFiles::copyFileToURL(exportTemp, destination, copyError);
                exportTemp.deleteFile();
                safe->status.setText(copied ? "MIDI exported; " + juce::String(static_cast<int>(unsupported.size()))
                    + " events need a target mapping." : (copyError.isNotEmpty() ? copyError : "MIDI export failed."), juce::dontSendNotification);
            });
    }

    static constexpr int samplePageSize = 250;
    juce::Label title, status;
    juce::TextButton importFolder, cancelImport, play, stop, importMidi, exportMidi, loadMore;
    juce::TextEditor search;
    juce::ComboBox category;
    juce::ListBox rows;
    juce::AudioFormatManager formats;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    SampleDatabase database;
    MidiMappingProfile mappingProfile = MidiMappingProfile::generalMidi();
    MidiPattern pattern;
    juce::File dataDirectory, importDirectory;
    juce::Array<SampleBrowseRecord> visible;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool workers { 1 };
    std::atomic<bool> importCancellation { false };
    bool importRunning = false;
    bool databaseReady = false;
    bool hasMoreSamples = false;
};

class MobileApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "MagDrum"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window() : DocumentWindow("MagDrum", juce::Colour(0xff101215), allButtons)
        {
            setUsingNativeTitleBar(true); setContentOwned(new MobileMainComponent(), true);
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto displayArea = display != nullptr ? display->userArea : juce::Rectangle<int>(760, 900);
            centreWithSize(juce::jmin(760, displayArea.getWidth()), juce::jmin(900, displayArea.getHeight()));
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
}

START_JUCE_APPLICATION(MobileApplication)
