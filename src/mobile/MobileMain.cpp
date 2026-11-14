#include <JuceHeader.h>
#include "ui/MagDrumLookAndFeel.h"
#include "ui/MagDrumLocalization.h"
#include "core/MagDrumPaths.h"
#include "core/MidiPatternIO.h"
#include "core/ScopedFileAccess.h"
#include "core/SampleDatabase.h"
#include "core/WavScanner.h"

namespace
{
class MobileMainComponent final : public juce::AudioAppComponent,
                                 private juce::ListBoxModel,
                                 private juce::Button::Listener,
                                 private juce::Timer
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
            const auto databaseFile = dataDirectory.getChildFile("samples.sqlite3");
            databaseReady = database.open(databaseFile);
            browseDatabaseReady = databaseReady && browseDatabase.open(databaseFile);
            if (databaseReady)
            {
                importDirectory = dataDirectory.getChildFile("Imported Libraries");
                importDirectory.createDirectory();
            }
        }

        title.setText("MagDrum", juce::dontSendNotification);
        title.setFont(juce::Font(30.0f, juce::Font::bold));
        addAndMakeVisible(title);
        languageButton.setButtonText("☰");
        languageButton.onClick = [this] { magdrum::ui::showApplicationMenu(languageButton, [this] { applyLanguage(); }); };
        addAndMakeVisible(languageButton);
        importFolder.addListener(this); addAndMakeVisible(importFolder);
        cancelImport.addListener(this); cancelImport.setEnabled(false); addAndMakeVisible(cancelImport);
        play.addListener(this); addAndMakeVisible(play);
        stop.addListener(this); addAndMakeVisible(stop);
        importMidi.addListener(this); addAndMakeVisible(importMidi);
        exportMidi.addListener(this); addAndMakeVisible(exportMidi);
        addAndMakeVisible(midiFallback);
        profileName.setText("General MIDI"); addAndMakeVisible(profileName);
        savedProfiles.onChange = [this] { loadMappingProfile(); }; addAndMakeVisible(savedProfiles);
        loadProfile.addListener(this); addAndMakeVisible(loadProfile);
        saveProfile.addListener(this); addAndMakeVisible(saveProfile);
        const char* instruments[] { "Kick", "Snare", "Hi-hat", "Tom", "Ride", "Crash", "Clap", "Rimshot", "Percussion" };
        for (int index = 0; index < 9; ++index) mappingInstrument.addItem(instruments[index], index + 1);
        mappingInstrument.setSelectedId(1); addAndMakeVisible(mappingInstrument);
        const char* articulations[] { "Default", "Center", "Rimshot", "Side stick", "Closed", "Open", "Pedal", "Low", "Mid", "High", "Bell", "Bow", "Edge" };
        for (int index = 0; index < 13; ++index) mappingArticulation.addItem(articulations[index], index + 1);
        mappingArticulation.setSelectedId(1); addAndMakeVisible(mappingArticulation);
        mappingNote.setInputRestrictions(3, "0123456789"); addAndMakeVisible(mappingNote);
        setMapping.addListener(this); addAndMakeVisible(setMapping);
        removeMapping.addListener(this); addAndMakeVisible(removeMapping);
        if (databaseReady) refreshSavedProfiles();
        search.onTextChange = [this] { refresh(); }; addAndMakeVisible(search);
        loadMore.onClick = [this] { loadMoreSamples(); };
        loadMore.setEnabled(false);
        addAndMakeVisible(loadMore);
        const char* categories[] { "All types", "Kick", "Snare", "Closed hi-hat", "Open hi-hat", "Pedal hi-hat", "Tom", "Ride", "Crash", "Clap", "Rimshot", "Percussion", "Unknown" };
        for (int index = 0; index < 13; ++index) category.addItem(categories[index], index + 1);
        category.setSelectedId(1); category.onChange = [this] { refresh(); }; addAndMakeVisible(category);
        addAndMakeVisible(rows); rows.setRowHeight(56);
        status.setColour(juce::Label::textColourId, magdrum::ui::Palette::Muted);
        importFolder.setEnabled(databaseReady);
        importMidi.setEnabled(databaseReady);
        exportMidi.setEnabled(databaseReady);
        addAndMakeVisible(status);
        applyLanguage();
        refresh();
        setAudioChannels(0, 2);
    }

    ~MobileMainComponent() override
    {
        stopTimer();
        importCancellation.store(true);
        workers.removeAllJobs(true, -1);
        browseWorkers.removeAllJobs(true, -1);
        transport.stop(); transport.setSource(nullptr); source.reset(); shutdownAudio();
    }

    void timerCallback() override
    {
        stopTimer();
        requestBrowsePage(0);
    }

    void prepareToPlay(int blockSize, double sampleRate) override { transport.prepareToPlay(blockSize, sampleRate); }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override { transport.getNextAudioBlock(info); }
    void releaseResources() override { transport.releaseResources(); }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(magdrum::ui::Palette::Background);
        graphics.setColour(magdrum::ui::Palette::Accent);
        graphics.fillRoundedRectangle(12.0f, 13.0f, 7.0f, 36.0f, 3.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        auto header = area.removeFromTop(48);
        languageButton.setBounds(header.removeFromRight(48));
        title.setBounds(header); area.removeFromTop(8);
        if (area.getWidth() < 640)
        {
            auto row = area.removeFromTop(44);
            importFolder.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2)); row.removeFromLeft(6); cancelImport.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(44);
            play.setBounds(row.removeFromLeft((row.getWidth() - 6) * 3 / 4)); row.removeFromLeft(6); stop.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(44);
            importMidi.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2)); row.removeFromLeft(6); exportMidi.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(36); midiFallback.setBounds(row);
            area.removeFromTop(8); row = area.removeFromTop(44);
            search.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2)); row.removeFromLeft(6); category.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(44);
            savedProfiles.setBounds(row.removeFromLeft((row.getWidth() - 6) * 2 / 3)); row.removeFromLeft(6); loadProfile.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(44);
            profileName.setBounds(row.removeFromLeft((row.getWidth() - 6) * 2 / 3)); row.removeFromLeft(6); saveProfile.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(44);
            mappingInstrument.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2)); row.removeFromLeft(6); mappingArticulation.setBounds(row);
            area.removeFromTop(6); row = area.removeFromTop(44);
            mappingNote.setBounds(row.removeFromLeft((row.getWidth() - 12) / 3)); row.removeFromLeft(6);
            setMapping.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2)); row.removeFromLeft(6); removeMapping.setBounds(row);
        }
        else
        {
            auto row = area.removeFromTop(42);
            importFolder.setBounds(row.removeFromLeft(180)); row.removeFromLeft(6);
            cancelImport.setBounds(row.removeFromLeft(90)); row.removeFromLeft(6);
            play.setBounds(row.removeFromLeft(125)); row.removeFromLeft(6); stop.setBounds(row.removeFromLeft(75));
            area.removeFromTop(6); row = area.removeFromTop(42);
            importMidi.setBounds(row.removeFromLeft(140)); row.removeFromLeft(6); exportMidi.setBounds(row.removeFromLeft(140)); row.removeFromLeft(8); midiFallback.setBounds(row.removeFromLeft(280));
            area.removeFromTop(8); row = area.removeFromTop(42);
            search.setBounds(row.removeFromLeft(400)); row.removeFromLeft(8); category.setBounds(row.removeFromLeft(180));
            area.removeFromTop(6); row = area.removeFromTop(42);
            savedProfiles.setBounds(row.removeFromLeft(250)); row.removeFromLeft(6); loadProfile.setBounds(row.removeFromLeft(120)); row.removeFromLeft(8);
            profileName.setBounds(row.removeFromLeft(240)); row.removeFromLeft(6); saveProfile.setBounds(row.removeFromLeft(120));
            area.removeFromTop(6); row = area.removeFromTop(42);
            mappingInstrument.setBounds(row.removeFromLeft(150)); row.removeFromLeft(6); mappingArticulation.setBounds(row.removeFromLeft(150)); row.removeFromLeft(6);
            mappingNote.setBounds(row.removeFromLeft(130)); row.removeFromLeft(6); setMapping.setBounds(row.removeFromLeft(125)); row.removeFromLeft(6); removeMapping.setBounds(row.removeFromLeft(125));
        }
        area.removeFromTop(6);
        auto footer = area.removeFromBottom(38);
        loadMore.setBounds(footer.removeFromRight(92)); footer.removeFromRight(6);
        status.setBounds(footer);
        rows.setBounds(area);
    }

private:
    void applyLanguage()
    {
        using magdrum::ui::localisedText;
        importFolder.setButtonText(localisedText("Import audio files"));
        cancelImport.setButtonText(localisedText("Cancel"));
        play.setButtonText(localisedText("Play selected"));
        stop.setButtonText(localisedText("Stop"));
        importMidi.setButtonText(localisedText("Import MIDI"));
        exportMidi.setButtonText(localisedText("Export MIDI"));
        midiFallback.setButtonText(localisedText("Allow articulation fallback"));
        midiFallback.setTooltip(localisedText("When enabled, unsupported articulations use a mapping for the same drum instrument. Every substitution is listed after export."));
        profileName.setTextToShowWhenEmpty(localisedText("Mapping profile name"), magdrum::ui::Palette::Muted);
        savedProfiles.setTextWhenNothingSelected(localisedText("Saved profiles"));
        loadProfile.setButtonText(localisedText("Load profile"));
        saveProfile.setButtonText(localisedText("Save profile"));
        mappingNote.setTextToShowWhenEmpty(localisedText("MIDI note 0–127"), magdrum::ui::Palette::Muted);
        setMapping.setButtonText(localisedText("Set mapping"));
        removeMapping.setButtonText(localisedText("Remove mapping"));
        search.setTextToShowWhenEmpty(localisedText("Search indexed samples"), magdrum::ui::Palette::Muted);
        loadMore.setButtonText(localisedText("More samples"));

        static constexpr const char* instruments[] { "Kick", "Snare", "Hi-hat", "Tom", "Ride", "Crash", "Clap", "Rimshot", "Percussion" };
        static constexpr const char* articulations[] { "Default", "Center", "Rimshot", "Side stick", "Closed", "Open", "Pedal", "Low", "Mid", "High", "Bell", "Bow", "Edge" };
        static constexpr const char* categories[] { "All categories", "Kick", "Snare", "Closed hi-hat", "Open hi-hat", "Pedal hi-hat", "Tom", "Ride", "Crash", "Clap", "Rimshot", "Percussion", "Unknown" };
        const auto instrumentId = mappingInstrument.getSelectedId();
        const auto articulationId = mappingArticulation.getSelectedId();
        const auto categoryId = category.getSelectedId();
        mappingInstrument.clear(juce::dontSendNotification);
        mappingArticulation.clear(juce::dontSendNotification);
        category.clear(juce::dontSendNotification);
        for (int i = 0; i < std::size(instruments); ++i) mappingInstrument.addItem(localisedText(instruments[i]), i + 1);
        for (int i = 0; i < std::size(articulations); ++i) mappingArticulation.addItem(localisedText(articulations[i]), i + 1);
        for (int i = 0; i < std::size(categories); ++i) category.addItem(localisedText(categories[i]), i + 1);
        mappingInstrument.setSelectedId(instrumentId, juce::dontSendNotification);
        mappingArticulation.setSelectedId(articulationId, juce::dontSendNotification);
        category.setSelectedId(categoryId, juce::dontSendNotification);
        languageButton.setTooltip(localisedText("Language"));
        status.setText(databaseReady ? (browseDatabaseReady ? localisedText("Import audio files or search your library.")
                                                             : localisedText("The sample index is unavailable for browsing."))
            : dataDirectory == juce::File() ? localisedText("Shared MagDrum storage is unavailable. Check the signed app-group entitlement.")
                                             : localisedText("Could not open the MagDrum sound database."), juce::dontSendNotification);
    }

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
        graphics.fillAll(selected ? magdrum::ui::Palette::Selection : magdrum::ui::Palette::Panel);
        const auto& record = visible.getReference(row);
        graphics.setColour(magdrum::ui::Palette::Text); graphics.setFont(16.0f);
        graphics.drawText(record.file.getFileName(), 12, 5, width - 24, 24, juce::Justification::centredLeft, true);
        graphics.setColour(magdrum::ui::Palette::Muted); graphics.setFont(12.0f);
        auto details = magdrum::ui::localisedText(record.category) + "  ·  " + juce::String(record.durationSeconds, 2) + " s  ·  "
            + record.file.getParentDirectory().getFileName();
        if (!record.classificationTags.isEmpty())
        {
            juce::StringArray translatedTags;
            for (const auto& tag : record.classificationTags) translatedTags.add(magdrum::ui::localisedText(tag));
            details += "  ·  " + translatedTags.joinIntoString(", ");
        }
        graphics.drawText(details, 12, 29, width - 24, height - 30,
            juce::Justification::centredLeft, true);
    }

    void buttonClicked(juce::Button* button) override
    {
        if (button == &importFolder)
        {
            chooser = std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Choose audio files to import"), juce::File(),
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
            status.setText(magdrum::ui::localisedText("Cancelling the current scan…"), juce::dontSendNotification);
        }
        else if (button == &play) playSelected();
        else if (button == &stop) transport.stop();
        else if (button == &importMidi) chooseMidiImport();
        else if (button == &exportMidi) chooseMidiExport();
        else if (button == &loadProfile) loadMappingProfile();
        else if (button == &saveProfile) saveMappingProfile();
        else if (button == &setMapping) editMapping(true);
        else if (button == &removeMapping) editMapping(false);
    }

    void refreshSavedProfiles(const juce::String& selectName = {})
    {
        savedProfiles.clear(juce::dontSendNotification);
        const auto names = database.getMappingProfileNames();
        int itemId = 1;
        for (const auto& name : names)
        {
            savedProfiles.addItem(name, itemId);
            if (name == selectName) savedProfiles.setSelectedId(itemId, juce::dontSendNotification);
            ++itemId;
        }
    }

    void loadMappingProfile()
    {
        auto name = savedProfiles.getText().trim();
        if (name.isEmpty()) name = profileName.getText().trim();
        auto stored = database.loadMappingProfile(name);
        if (stored)
        {
            mappingProfile = std::move(*stored);
            profileName.setText(name, juce::dontSendNotification);
            status.setText(magdrum::ui::localisedText("Mapping profile loaded: %1").replace("%1", name), juce::dontSendNotification);
        }
        else if (name.equalsIgnoreCase("General MIDI"))
        {
            mappingProfile = MidiMappingProfile::generalMidi();
            profileName.setText("General MIDI", juce::dontSendNotification);
            status.setText(magdrum::ui::localisedText("Built-in General MIDI mapping loaded."), juce::dontSendNotification);
        }
        else
            status.setText(magdrum::ui::localisedText("Mapping profile not found. Save this profile name to create it."), juce::dontSendNotification);
    }

    void saveMappingProfile()
    {
        const auto name = profileName.getText().trim();
        if (name.isEmpty())
        {
            status.setText(magdrum::ui::localisedText("Enter a name for this mapping profile."), juce::dontSendNotification);
            return;
        }
        if (!database.saveMappingProfile(name, mappingProfile))
        {
            status.setText(magdrum::ui::localisedText("Could not save the mapping profile."), juce::dontSendNotification);
            return;
        }
        refreshSavedProfiles(name);
        status.setText(magdrum::ui::localisedText("Mapping profile saved: %1").replace("%1", name), juce::dontSendNotification);
    }

    void editMapping(bool set)
    {
        const auto instrument = static_cast<DrumInstrument>(mappingInstrument.getSelectedId() - 1);
        const auto articulation = static_cast<DrumArticulation>(mappingArticulation.getSelectedId() - 1);
        if (set)
        {
            const int note = mappingNote.getText().getIntValue();
            if (mappingNote.getText().trim().isEmpty() || note < 0 || note > 127)
            {
                status.setText(magdrum::ui::localisedText("Enter a MIDI note from 0 to 127."), juce::dontSendNotification);
                return;
            }
            if (!mappingProfile.setMapping({ instrument, articulation, static_cast<std::uint8_t>(note) }))
            {
                status.setText(magdrum::ui::localisedText("The MIDI mapping could not be updated."), juce::dontSendNotification);
                return;
            }
        }
        else if (!mappingProfile.removeMapping(instrument, articulation))
        {
            status.setText(magdrum::ui::localisedText("That instrument and articulation have no mapping to remove."), juce::dontSendNotification);
            return;
        }
        const auto name = profileName.getText().trim();
        if (name.isEmpty() || !database.saveMappingProfile(name, mappingProfile))
        {
            status.setText(magdrum::ui::localisedText("Mapping changed in memory, but the profile could not be saved. Enter a profile name and save it."), juce::dontSendNotification);
            return;
        }
        refreshSavedProfiles(name);
        status.setText(magdrum::ui::localisedText(set ? "MIDI mapping updated and saved." : "MIDI mapping removed and saved."), juce::dontSendNotification);
    }

    void beginImport(juce::Array<juce::URL> files)
    {
        if (importRunning || !dataDirectory.isDirectory() || files.isEmpty()) return;
        importRunning = true; importCancellation.store(false);
        importFolder.setEnabled(false); cancelImport.setEnabled(true);
        status.setText(magdrum::ui::localisedText("Copying and indexing audio files…"), juce::dontSendNotification);
        workers.addJob(new FileImportJob(*this, std::move(files), dataDirectory, importDirectory, importCancellation), true);
    }

    void finishImport(const WavScanner::Stats& stats, std::int64_t indexed, const juce::String& error)
    {
        importRunning = false; importFolder.setEnabled(true); cancelImport.setEnabled(false);
        if (error.isNotEmpty()) status.setText(magdrum::ui::localisedText(error), juce::dontSendNotification);
        else
        {
            const auto message = stats.cancelled
                ? magdrum::ui::localisedText("Import cancelled. %1 indexed, %2 unchanged, %3 errors.")
                : magdrum::ui::localisedText("Import complete. %1 indexed, %2 unchanged, %3 errors.");
            status.setText(message.replace("%1", juce::String(indexed))
                .replace("%2", juce::String(stats.filesSkippedUnchanged))
                .replace("%3", juce::String(stats.errors)), juce::dontSendNotification);
        }
        refresh();
    }

    void refresh()
    {
        stopTimer();
        ++browseGeneration;
        browsePagePending = false;
        visible.clear();
        hasMoreSamples = false;
        loadMore.setEnabled(false);
        rows.updateContent(); rows.repaint();
        startTimer(150);
    }

    void loadMoreSamples()
    {
        if (hasMoreSamples && !browsePagePending)
            requestBrowsePage(visible.size());
    }

    class BrowsePageJob final : public juce::ThreadPoolJob
    {
    public:
        BrowsePageJob(MobileMainComponent& ownerIn, SampleDatabase& databaseIn,
                      juce::String queryIn, juce::String categoryIn,
                      int offsetIn, std::uint64_t generationIn)
            : ThreadPoolJob("MagDrum mobile browse page"), owner(&ownerIn), database(databaseIn),
              query(std::move(queryIn)), selectedCategory(std::move(categoryIn)),
              offset(offsetIn), generation(generationIn) {}

        JobStatus runJob() override
        {
            const auto page = database.searchSampleBrowseRecords(query, selectedCategory,
                samplePageSize + 1, offset);
            const auto safe = owner;
            const auto requestGeneration = generation;
            const auto requestOffset = offset;
            juce::MessageManager::callAsync([safe, requestGeneration, requestOffset, page]
            {
                if (safe != nullptr)
                    safe->finishBrowsePage(requestGeneration, requestOffset, page);
            });
            return jobHasFinished;
        }

    private:
        static constexpr int samplePageSize = 250;
        juce::Component::SafePointer<MobileMainComponent> owner;
        SampleDatabase& database;
        juce::String query, selectedCategory;
        int offset;
        std::uint64_t generation;
    };

    void requestBrowsePage(int offset)
    {
        if (browsePagePending || !browseDatabaseReady)
            return;
        const auto selectedCategory = category.getSelectedId() > 1 ? category.getText() : juce::String();
        browsePagePending = true;
        loadMore.setEnabled(false);
        browseWorkers.addJob(new BrowsePageJob(*this, browseDatabase, search.getText().trim(),
            selectedCategory, offset, browseGeneration), true);
    }

    void finishBrowsePage(std::uint64_t generation, int offset,
                          const juce::Array<SampleBrowseRecord>& page)
    {
        if (generation != browseGeneration || offset != visible.size())
            return;
        browsePagePending = false;
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
            status.setText(magdrum::ui::localisedText("Select a sound before playback."), juce::dontSendNotification);
            return;
        }
        const auto file = visible.getReference(index).file;
        auto* reader = formats.createReaderFor(file);
        if (reader == nullptr)
        {
            status.setText(magdrum::ui::localisedText("This sound is missing or cannot be decoded."), juce::dontSendNotification);
            return;
        }
        transport.stop(); transport.setSource(nullptr); source.reset(new juce::AudioFormatReaderSource(reader, true));
        transport.setSource(source.get(), 0, nullptr, reader->sampleRate); transport.start();
    }

    void chooseMidiImport()
    {
        chooser = std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Import a Standard MIDI File"), juce::File(), "*.mid;*.midi", true, false, this);
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
                    safe->status.setText(magdrum::ui::localisedText(copyError), juce::dontSendNotification);
                    return;
                }
                MidiPattern imported;
                const auto report = MidiPatternIO{}.read(importTemp, safe->mappingProfile, imported);
                importTemp.deleteFile();
                if (!report.ok) { safe->status.setText(magdrum::ui::localisedText(report.error), juce::dontSendNotification); return; }
                safe->pattern = std::move(imported);
                const auto message = magdrum::ui::localisedText("MIDI imported: %1 semantic events, %2 unmapped note entries, %3 ambiguous assignments.");
                safe->status.setText(message.replace("%1", juce::String(static_cast<int>(safe->pattern.events.size())))
                    .replace("%2", juce::String(static_cast<int>(report.unmappedNotes.size())))
                    .replace("%3", juce::String(static_cast<int>(report.ambiguousNotes.size()))), juce::dontSendNotification);
            });
    }

    void chooseMidiExport()
    {
        if (pattern.events.empty()) { status.setText(magdrum::ui::localisedText("Import a MIDI pattern before exporting."), juce::dontSendNotification); return; }
        chooser = std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Export a Standard MIDI File"),
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
                std::vector<MidiFallbackUse> fallbacks;
                juce::String copyError;
                const bool written = MidiPatternIO{}.write(exportTemp, safe->pattern, safe->mappingProfile,
                    safe->midiFallback.getToggleState(), &unsupported, &fallbacks);
                const bool copied = written && MagDrumFiles::copyFileToURL(exportTemp, destination, copyError);
                exportTemp.deleteFile();
                if (!copied)
                {
                    safe->status.setText(magdrum::ui::localisedText(copyError.isNotEmpty() ? copyError : "MIDI export failed."), juce::dontSendNotification);
                    return;
                }
                const auto message = magdrum::ui::localisedText("MIDI exported; %1 fallback mapping(s), %2 unsupported event(s) omitted.");
                safe->status.setText(message.replace("%1", juce::String(static_cast<int>(fallbacks.size())))
                    .replace("%2", juce::String(static_cast<int>(unsupported.size()))), juce::dontSendNotification);
                if (!fallbacks.empty() || !unsupported.empty())
                {
                    juce::String report;
                    for (const auto& fallback : fallbacks)
                        report += magdrum::ui::localisedText("Fallback: %1 mapped to %2 at MIDI note %3.")
                            .replace("%1", safe->describeMapping(fallback.requestedEvent.instrument, fallback.requestedEvent.articulation))
                            .replace("%2", safe->describeMapping(fallback.requestedEvent.instrument, fallback.mappedArticulation))
                            .replace("%3", juce::String(fallback.midiNote)) + "\n";
                    for (const auto& event : unsupported)
                        report += magdrum::ui::localisedText("Omitted: %1, velocity %2.")
                            .replace("%1", safe->describeMapping(event.instrument, event.articulation))
                            .replace("%2", juce::String(event.velocity)) + "\n";
                    juce::AlertWindow::showMessageBoxAsync(unsupported.empty() ? juce::MessageBoxIconType::InfoIcon
                        : juce::MessageBoxIconType::WarningIcon, magdrum::ui::localisedText("MIDI export mapping report"),
                        report, magdrum::ui::localisedText("Close"));
                }
            });
    }

    static juce::String describeMapping(DrumInstrument instrument, DrumArticulation articulation)
    {
        static constexpr const char* instruments[] { "Kick", "Snare", "Hi-hat", "Tom", "Ride", "Crash", "Clap", "Rimshot", "Percussion" };
        static constexpr const char* articulations[] { "Default", "Center", "Rimshot", "Side stick", "Closed", "Open", "Pedal", "Low", "Mid", "High", "Bell", "Bow", "Edge" };
        const auto instrumentIndex = static_cast<size_t>(instrument);
        const auto articulationIndex = static_cast<size_t>(articulation);
        if (instrumentIndex >= std::size(instruments) || articulationIndex >= std::size(articulations))
            return magdrum::ui::localisedText("Unknown mapping");
        return magdrum::ui::localisedText(instruments[instrumentIndex]) + " / "
            + magdrum::ui::localisedText(articulations[articulationIndex]);
    }

    static constexpr int samplePageSize = 250;
    juce::Label title, status;
    juce::TextButton languageButton, importFolder, cancelImport, play, stop, importMidi, exportMidi, loadMore;
    juce::ToggleButton midiFallback;
    juce::TextButton loadProfile, saveProfile, setMapping, removeMapping;
    juce::TextEditor search, profileName, mappingNote;
    juce::ComboBox category, savedProfiles, mappingInstrument, mappingArticulation;
    juce::ListBox rows;
    juce::AudioFormatManager formats;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    SampleDatabase database;
    SampleDatabase browseDatabase;
    MidiMappingProfile mappingProfile = MidiMappingProfile::generalMidi();
    MidiPattern pattern;
    juce::File dataDirectory, importDirectory;
    juce::Array<SampleBrowseRecord> visible;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool workers { 1 };
    juce::ThreadPool browseWorkers { 1 };
    std::atomic<bool> importCancellation { false };
    std::uint64_t browseGeneration = 0;
    bool importRunning = false;
    bool databaseReady = false;
    bool browseDatabaseReady = false;
    bool browsePagePending = false;
    bool hasMoreSamples = false;
};

class MobileApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "MagDrum"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String&) override { magdrum::ui::initialiseMagDrumLocalization(); juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel); window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    void systemRequestedQuit() override { quit(); }

private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window() : DocumentWindow("MagDrum", magdrum::ui::Palette::Background, allButtons)
        {
            setUsingNativeTitleBar(true); setContentOwned(new MobileMainComponent(), true);
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto displayArea = display != nullptr ? display->userArea : juce::Rectangle<int>(760, 900);
            centreWithSize(juce::jmin(760, displayArea.getWidth()), juce::jmin(900, displayArea.getHeight()));
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    magdrum::ui::LookAndFeel lookAndFeel;
    std::unique_ptr<Window> window;
};
}

START_JUCE_APPLICATION(MobileApplication)
