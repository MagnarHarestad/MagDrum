#include "SampleDatabase.h"
#include "WavScanner.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
int hexDigit(juce::juce_wchar character)
{
    if (character >= '0' && character <= '9') return static_cast<int>(character - '0');
    if (character >= 'a' && character <= 'f') return static_cast<int>(character - 'a' + 10);
    if (character >= 'A' && character <= 'F') return static_cast<int>(character - 'A' + 10);
    return -1;
}

bool decodeAcousticFingerprint(const juce::String& text, std::array<std::uint8_t, 64>& values)
{
    if (text.length() != static_cast<int>(values.size() * 2)) return false;
    for (size_t index = 0; index < values.size(); ++index)
    {
        const int high = hexDigit(text[static_cast<int>(index * 2)]);
        const int low = hexDigit(text[static_cast<int>(index * 2 + 1)]);
        if (high < 0 || low < 0) return false;
        values[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}
}

SampleDatabase::~SampleDatabase()
{
    if (connection != nullptr)
        sqlite3_close(connection);
}

bool SampleDatabase::open(const juce::File& file)
{
    if (connection != nullptr)
        sqlite3_close(connection);
    connection = nullptr;
    const auto path = file.getFullPathName();
    if (sqlite3_open(path.toRawUTF8(), &connection) != SQLITE_OK)
    {
        if (connection != nullptr)
            sqlite3_close(connection);
        connection = nullptr;
        return false;
    }
    sqlite3_busy_timeout(connection, 5000);
    if (sqlite3_exec(connection, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    const char* schema = "CREATE TABLE IF NOT EXISTS samples (path TEXT PRIMARY KEY,name TEXT NOT NULL,duration REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,format TEXT NOT NULL DEFAULT '',bit_depth INTEGER NOT NULL DEFAULT 0,peak REAL NOT NULL DEFAULT 0,rms REAL NOT NULL DEFAULT 0,spectral_centroid REAL NOT NULL DEFAULT 0,transient_strength REAL NOT NULL DEFAULT 0,fundamental_hz REAL NOT NULL DEFAULT 0,pitch_confidence REAL NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT 'Unknown',classification_confidence REAL NOT NULL DEFAULT 0,user_category TEXT NOT NULL DEFAULT '',size_bytes INTEGER NOT NULL DEFAULT 0,modified_time_ms INTEGER NOT NULL DEFAULT 0,acoustic_fingerprint TEXT NOT NULL DEFAULT '',acoustic_fingerprint_version INTEGER NOT NULL DEFAULT 0,analysis_version INTEGER NOT NULL DEFAULT 0)";
    if (sqlite3_exec(connection, schema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    // Upgrade databases created by earlier MagDrum builds without losing indexed paths.
    const char* migrations[] = {
        "ALTER TABLE samples ADD COLUMN format TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE samples ADD COLUMN bit_depth INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN peak REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN rms REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN spectral_centroid REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN transient_strength REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN fingerprint TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE samples ADD COLUMN category TEXT NOT NULL DEFAULT 'Unknown'",
        "ALTER TABLE samples ADD COLUMN classification_confidence REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN user_category TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE samples ADD COLUMN fundamental_hz REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN pitch_confidence REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN size_bytes INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN modified_time_ms INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN acoustic_fingerprint TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE samples ADD COLUMN acoustic_fingerprint_version INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN analysis_version INTEGER NOT NULL DEFAULT 0"
    };
    for (const auto* migration : migrations)
        sqlite3_exec(connection, migration, nullptr, nullptr, nullptr); // duplicate-column means already upgraded
    if (sqlite3_exec(connection, "CREATE INDEX IF NOT EXISTS samples_fingerprint ON samples(fingerprint); CREATE INDEX IF NOT EXISTS samples_duration ON samples(duration)", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    const char* pluginSchema = "CREATE TABLE IF NOT EXISTS schema_info(singleton INTEGER PRIMARY KEY CHECK(singleton=1),version INTEGER NOT NULL); INSERT INTO schema_info(singleton,version) VALUES(1,10) ON CONFLICT(singleton) DO UPDATE SET version=max(version,excluded.version); CREATE TABLE IF NOT EXISTS plugin_instruments(id INTEGER PRIMARY KEY,identifier TEXT NOT NULL UNIQUE,format TEXT NOT NULL,name TEXT NOT NULL,manufacturer TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT '',file TEXT NOT NULL DEFAULT '',version TEXT NOT NULL DEFAULT '',last_scanned TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP); CREATE INDEX IF NOT EXISTS plugin_instruments_name ON plugin_instruments(name COLLATE NOCASE); CREATE TABLE IF NOT EXISTS plugin_kits(id INTEGER PRIMARY KEY,plugin_id INTEGER NOT NULL,name TEXT NOT NULL,plugin_state BLOB,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,UNIQUE(plugin_id,name),FOREIGN KEY(plugin_id) REFERENCES plugin_instruments(id) ON DELETE CASCADE); CREATE TABLE IF NOT EXISTS plugin_sounds(id INTEGER PRIMARY KEY,kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL CHECK(midi_note BETWEEN 0 AND 127),velocity INTEGER NOT NULL CHECK(velocity BETWEEN 1 AND 127),articulation TEXT NOT NULL DEFAULT '',silent INTEGER NOT NULL DEFAULT 0,preview_path TEXT NOT NULL DEFAULT '',duration REAL NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,peak REAL NOT NULL DEFAULT 0,rms REAL NOT NULL DEFAULT 0,spectral_centroid REAL NOT NULL DEFAULT 0,transient_strength REAL NOT NULL DEFAULT 0,fundamental_hz REAL NOT NULL DEFAULT 0,pitch_confidence REAL NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT 'Unknown',classification_confidence REAL NOT NULL DEFAULT 0,acoustic_fingerprint TEXT NOT NULL DEFAULT '',UNIQUE(kit_id,midi_note,velocity,articulation),FOREIGN KEY(kit_id) REFERENCES plugin_kits(id) ON DELETE CASCADE); CREATE INDEX IF NOT EXISTS plugin_sounds_note ON plugin_sounds(midi_note,velocity); CREATE TABLE IF NOT EXISTS plugin_velocity_layer_candidates(kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL CHECK(midi_note BETWEEN 0 AND 127),lower_velocity INTEGER NOT NULL CHECK(lower_velocity BETWEEN 1 AND 127),upper_velocity INTEGER NOT NULL CHECK(upper_velocity BETWEEN 1 AND 127),spectral_distance REAL NOT NULL,algorithm_version INTEGER NOT NULL,PRIMARY KEY(kit_id,midi_note,lower_velocity,upper_velocity),FOREIGN KEY(kit_id) REFERENCES plugin_kits(id) ON DELETE CASCADE)";
    if (sqlite3_exec(connection, pluginSchema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    const char* sourceSchema = "UPDATE schema_info SET version=max(version,10) WHERE singleton=1; CREATE TABLE IF NOT EXISTS sample_sources(id INTEGER PRIMARY KEY,source_type TEXT NOT NULL CHECK(source_type IN ('folder','plugin')),identifier TEXT NOT NULL,display_name TEXT NOT NULL,root_path TEXT NOT NULL DEFAULT '',created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,UNIQUE(source_type,identifier)); CREATE INDEX IF NOT EXISTS sample_sources_name ON sample_sources(display_name COLLATE NOCASE)";
    if (sqlite3_exec(connection, sourceSchema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    sqlite3_exec(connection, "ALTER TABLE samples ADD COLUMN source_id INTEGER REFERENCES sample_sources(id) ON DELETE SET NULL", nullptr, nullptr, nullptr); // duplicate source_id means already upgraded
    sqlite3_stmt* sampleColumns = nullptr;
    if (sqlite3_prepare_v2(connection, "PRAGMA table_info(samples)", -1, &sampleColumns, nullptr) != SQLITE_OK) return false;
    bool hasSourceId = false;
    while (sqlite3_step(sampleColumns) == SQLITE_ROW)
    {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(sampleColumns, 1));
        if (name != nullptr && std::strcmp(name, "source_id") == 0) hasSourceId = true;
    }
    sqlite3_finalize(sampleColumns);
    if (!hasSourceId) return false;
    const char* roundRobinSchema = "UPDATE schema_info SET version=max(version,9) WHERE singleton=1; CREATE TABLE IF NOT EXISTS plugin_round_robin_takes(kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL CHECK(midi_note BETWEEN 0 AND 127),velocity INTEGER NOT NULL CHECK(velocity BETWEEN 1 AND 127),take_index INTEGER NOT NULL CHECK(take_index BETWEEN 1 AND 8),silent INTEGER NOT NULL DEFAULT 0,preview_path TEXT NOT NULL DEFAULT '',acoustic_fingerprint TEXT NOT NULL DEFAULT '',PRIMARY KEY(kit_id,midi_note,velocity,take_index),FOREIGN KEY(kit_id) REFERENCES plugin_kits(id) ON DELETE CASCADE); CREATE TABLE IF NOT EXISTS plugin_round_robin_candidates(kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL CHECK(midi_note BETWEEN 0 AND 127),velocity INTEGER NOT NULL CHECK(velocity BETWEEN 1 AND 127),take_count INTEGER NOT NULL CHECK(take_count>=2),distinct_variants INTEGER NOT NULL CHECK(distinct_variants BETWEEN 2 AND take_count),maximum_spectral_distance REAL NOT NULL,algorithm_version INTEGER NOT NULL,PRIMARY KEY(kit_id,midi_note,velocity),FOREIGN KEY(kit_id) REFERENCES plugin_kits(id) ON DELETE CASCADE)";
    if (sqlite3_exec(connection, roundRobinSchema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    const char* confirmedSchema = "UPDATE schema_info SET version=max(version,11) WHERE singleton=1; CREATE TABLE IF NOT EXISTS plugin_confirmed_velocity_layers(kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL CHECK(midi_note BETWEEN 0 AND 127),minimum_velocity INTEGER NOT NULL CHECK(minimum_velocity BETWEEN 1 AND 127),maximum_velocity INTEGER NOT NULL CHECK(maximum_velocity BETWEEN minimum_velocity AND 127),PRIMARY KEY(kit_id,midi_note,minimum_velocity),FOREIGN KEY(kit_id) REFERENCES plugin_kits(id) ON DELETE CASCADE); CREATE TABLE IF NOT EXISTS plugin_confirmed_round_robin_takes(kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL,velocity INTEGER NOT NULL,take_index INTEGER NOT NULL,PRIMARY KEY(kit_id,midi_note,velocity,take_index),FOREIGN KEY(kit_id,midi_note,velocity,take_index) REFERENCES plugin_round_robin_takes(kit_id,midi_note,velocity,take_index) ON DELETE CASCADE)";
    if (sqlite3_exec(connection, confirmedSchema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN fundamental_hz REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN pitch_confidence REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN acoustic_fingerprint TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
    if (sqlite3_exec(connection, "CREATE TABLE IF NOT EXISTS sample_tags(path TEXT NOT NULL,tag TEXT NOT NULL,PRIMARY KEY(path,tag),FOREIGN KEY(path) REFERENCES samples(path) ON DELETE CASCADE); CREATE INDEX IF NOT EXISTS sample_tags_tag ON sample_tags(tag)", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    if (sqlite3_exec(connection,"CREATE TABLE IF NOT EXISTS mapping_profiles(name TEXT PRIMARY KEY); CREATE TABLE IF NOT EXISTS mapping_entries(profile_name TEXT NOT NULL,instrument INTEGER NOT NULL,articulation INTEGER NOT NULL,note INTEGER NOT NULL CHECK(note BETWEEN 0 AND 127),PRIMARY KEY(profile_name,instrument,articulation),FOREIGN KEY(profile_name) REFERENCES mapping_profiles(name) ON DELETE CASCADE); CREATE TABLE IF NOT EXISTS sample_scan_history(id INTEGER PRIMARY KEY,root_path TEXT NOT NULL,started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,finished_at TEXT,status TEXT NOT NULL DEFAULT 'running',files_considered INTEGER NOT NULL DEFAULT 0,files_skipped INTEGER NOT NULL DEFAULT 0,files_indexed INTEGER NOT NULL DEFAULT 0,errors INTEGER NOT NULL DEFAULT 0)",nullptr,nullptr,nullptr)!=SQLITE_OK)
        return false;
    sqlite3_stmt* roots = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT root_path FROM sample_scan_history GROUP BY root_path ORDER BY length(root_path)", -1, &roots, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(roots) == SQLITE_ROW)
        {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(roots, 0));
            if (value == nullptr) continue;
            const juce::File root(juce::String::fromUTF8(value));
            const auto sourceRootPath = root.getFullPathName();
            const auto sourceId = ensureSampleSource("folder", sourceRootPath, root.getFileName().isNotEmpty() ? root.getFileName() : sourceRootPath, sourceRootPath);
            if (!sourceId || !assignSamplesToSource(root, *sourceId)) { sqlite3_finalize(roots); return false; }
        }
        sqlite3_finalize(roots);
    }
    return true;
}

std::optional<std::int64_t> SampleDatabase::ensureSampleSource(const juce::String& type,
                                                               const juce::String& identifier,
                                                               const juce::String& displayName,
                                                               const juce::String& rootPath)
{
    if (connection == nullptr || (type != "folder" && type != "plugin")
        || identifier.trim().isEmpty() || displayName.trim().isEmpty()) return std::nullopt;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO sample_sources(source_type,identifier,display_name,root_path) VALUES(?,?,?,?) ON CONFLICT(source_type,identifier) DO UPDATE SET display_name=excluded.display_name,root_path=excluded.root_path";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return std::nullopt;
    const auto cleanType = type.trim(), cleanIdentifier = identifier.trim(), cleanName = displayName.trim();
    sqlite3_bind_text(statement, 1, cleanType.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, cleanIdentifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, cleanName.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, rootPath.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const bool inserted = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!inserted || sqlite3_prepare_v2(connection, "SELECT id FROM sample_sources WHERE source_type=? AND identifier=?", -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_text(statement, 1, cleanType.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, cleanIdentifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    std::optional<std::int64_t> result;
    if (sqlite3_step(statement) == SQLITE_ROW) result = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::assignSamplesToSource(const juce::File& root, std::int64_t sourceId)
{
    if (connection == nullptr || sourceId <= 0 || root.getFullPathName().isEmpty()) return false;
    const auto rootPath = root.getFullPathName();
    auto prefix = rootPath;
    if (!prefix.endsWithChar(juce::File::getSeparatorChar())) prefix += juce::File::getSeparatorChar();
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "UPDATE samples SET source_id=? WHERE path=? OR substr(path,1,length(?))=?", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(statement, 1, sourceId);
    sqlite3_bind_text(statement, 2, rootPath.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, prefix.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, prefix.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

juce::Array<SampleSourceRecord> SampleDatabase::getSampleSources(const juce::String& type) const
{
    juce::Array<SampleSourceRecord> result;
    if (connection == nullptr) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = type.trim().isEmpty()
        ? "SELECT id,source_type,identifier,display_name,root_path FROM sample_sources ORDER BY display_name COLLATE NOCASE"
        : "SELECT id,source_type,identifier,display_name,root_path FROM sample_sources WHERE source_type=? ORDER BY display_name COLLATE NOCASE";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    if (!type.trim().isEmpty()) sqlite3_bind_text(statement, 1, type.trim().toRawUTF8(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto text = [statement](int column) { const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column)); return value != nullptr ? juce::String::fromUTF8(value) : juce::String(); };
        result.add({sqlite3_column_int64(statement, 0), text(1), text(2), text(3), text(4)});
    }
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::upsertPluginInstrument(const PluginInstrumentRecord& plugin)
{
    if (connection == nullptr || plugin.identifier.trim().isEmpty() || plugin.name.trim().isEmpty()) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO plugin_instruments(identifier,format,name,manufacturer,category,file,version,last_scanned) VALUES(?,?,?,?,?,?,?,CURRENT_TIMESTAMP) ON CONFLICT(identifier) DO UPDATE SET format=excluded.format,name=excluded.name,manufacturer=excluded.manufacturer,category=excluded.category,file=excluded.file,version=excluded.version,last_scanned=CURRENT_TIMESTAMP";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    const juce::String values[]{plugin.identifier, plugin.format, plugin.name, plugin.manufacturer, plugin.category, plugin.file, plugin.version};
    for (int i = 0; i < 7; ++i) sqlite3_bind_text(statement, i + 1, values[i].toRawUTF8(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

juce::Array<PluginInstrumentRecord> SampleDatabase::getPluginInstruments() const
{
    juce::Array<PluginInstrumentRecord> result;
    if (connection == nullptr) return result;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT identifier,format,name,manufacturer,category,file,version FROM plugin_instruments ORDER BY name COLLATE NOCASE", -1, &statement, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto text = [statement](int column) { const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column)); return value != nullptr ? juce::String::fromUTF8(value) : juce::String(); };
        result.add({text(0), text(1), text(2), text(3), text(4), text(5), text(6)});
    }
    sqlite3_finalize(statement);
    return result;
}

std::optional<std::int64_t> SampleDatabase::savePluginKit(const juce::String& pluginIdentifier, const juce::String& kitName, const juce::MemoryBlock& pluginState)
{
    if (connection == nullptr || pluginIdentifier.trim().isEmpty() || kitName.trim().isEmpty()) return std::nullopt;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return std::nullopt;
    bool ok = sqlite3_prepare_v2(connection, "INSERT INTO plugin_kits(plugin_id,name,plugin_state) SELECT id,?,? FROM plugin_instruments WHERE identifier=? ON CONFLICT(plugin_id,name) DO UPDATE SET plugin_state=excluded.plugin_state,created_at=CURRENT_TIMESTAMP", -1, &statement, nullptr) == SQLITE_OK;
    if (ok)
    {
        const auto name = kitName.trim(), identifier = pluginIdentifier.trim();
        sqlite3_bind_text(statement, 1, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 2, pluginState.getData(), static_cast<int>(pluginState.getSize()), SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(connection) > 0;
    }
    if (statement != nullptr) sqlite3_finalize(statement);
    statement = nullptr;
    std::optional<std::int64_t> result;
    if (ok && sqlite3_prepare_v2(connection, "SELECT k.id FROM plugin_kits k JOIN plugin_instruments p ON p.id=k.plugin_id WHERE p.identifier=? AND k.name=?", -1, &statement, nullptr) == SQLITE_OK)
    {
        const auto name = kitName.trim(), identifier = pluginIdentifier.trim();
        sqlite3_bind_text(statement, 1, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(statement) == SQLITE_ROW) result = sqlite3_column_int64(statement, 0); else ok = false;
    }
    else if (ok) ok = false;
    if (statement != nullptr) sqlite3_finalize(statement);
    sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok ? result : std::nullopt;
}

std::optional<juce::MemoryBlock> SampleDatabase::loadPluginKitState(const juce::String& pluginIdentifier, const juce::String& kitName) const
{
    if (connection == nullptr || pluginIdentifier.trim().isEmpty() || kitName.trim().isEmpty()) return std::nullopt;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT k.plugin_state FROM plugin_kits k JOIN plugin_instruments p ON p.id=k.plugin_id WHERE p.identifier=? AND k.name=?";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return std::nullopt;
    const auto identifier = pluginIdentifier.trim(), name = kitName.trim();
    sqlite3_bind_text(statement, 1, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    std::optional<juce::MemoryBlock> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        juce::MemoryBlock state;
        const auto* data = sqlite3_column_blob(statement, 0);
        const int size = sqlite3_column_bytes(statement, 0);
        if (data != nullptr && size > 0) state.append(data, static_cast<size_t>(size));
        result = std::move(state);
    }
    sqlite3_finalize(statement);
    return result;
}

std::optional<std::int64_t> SampleDatabase::getPluginKitId(const juce::String& pluginIdentifier, const juce::String& kitName) const
{
    if (connection == nullptr || pluginIdentifier.trim().isEmpty() || kitName.trim().isEmpty()) return std::nullopt;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT k.id FROM plugin_kits k JOIN plugin_instruments p ON p.id=k.plugin_id WHERE p.identifier=? AND k.name=?";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return std::nullopt;
    const auto identifier = pluginIdentifier.trim(), name = kitName.trim();
    sqlite3_bind_text(statement, 1, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const auto result = sqlite3_step(statement) == SQLITE_ROW
        ? std::optional<std::int64_t>(sqlite3_column_int64(statement, 0)) : std::nullopt;
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::upsertPluginSound(std::int64_t kitId, const PluginSoundRecord& sound)
{
    if (connection == nullptr || kitId <= 0 || sound.midiNote < 0 || sound.midiNote > 127 || sound.velocity < 1 || sound.velocity > 127 || sound.duration < 0.0 || sound.sampleRate < 0.0 || sound.channels < 0 || sound.peak < 0.0 || sound.rms < 0.0 || sound.fundamentalHz < 0.0 || sound.pitchConfidence < 0.0 || sound.pitchConfidence > 1.0 || sound.confidence < 0.0 || sound.confidence > 1.0) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO plugin_sounds(kit_id,midi_note,velocity,articulation,silent,preview_path,duration,sample_rate,channels,peak,rms,spectral_centroid,transient_strength,fundamental_hz,pitch_confidence,fingerprint,category,classification_confidence,acoustic_fingerprint) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(kit_id,midi_note,velocity,articulation) DO UPDATE SET silent=excluded.silent,preview_path=excluded.preview_path,duration=excluded.duration,sample_rate=excluded.sample_rate,channels=excluded.channels,peak=excluded.peak,rms=excluded.rms,spectral_centroid=excluded.spectral_centroid,transient_strength=excluded.transient_strength,fundamental_hz=excluded.fundamental_hz,pitch_confidence=excluded.pitch_confidence,fingerprint=excluded.fingerprint,category=excluded.category,classification_confidence=excluded.classification_confidence,acoustic_fingerprint=excluded.acoustic_fingerprint";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(statement, 1, kitId); sqlite3_bind_int(statement, 2, sound.midiNote); sqlite3_bind_int(statement, 3, sound.velocity);
    sqlite3_bind_text(statement, 4, sound.articulation.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_int(statement, 5, sound.silent ? 1 : 0);
    sqlite3_bind_text(statement, 6, sound.previewPath.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_double(statement, 7, sound.duration);
    sqlite3_bind_double(statement, 8, sound.sampleRate); sqlite3_bind_int(statement, 9, sound.channels); sqlite3_bind_double(statement, 10, sound.peak);
    sqlite3_bind_double(statement, 11, sound.rms); sqlite3_bind_double(statement, 12, sound.spectralCentroid); sqlite3_bind_double(statement, 13, sound.transientStrength);
    sqlite3_bind_double(statement, 14, sound.fundamentalHz); sqlite3_bind_double(statement, 15, sound.pitchConfidence);
    sqlite3_bind_text(statement, 16, sound.fingerprint.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(statement, 17, sound.category.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_double(statement, 18, sound.confidence); sqlite3_bind_text(statement, 19, sound.acousticFingerprint.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool SampleDatabase::replacePluginVelocityLayerCandidates(
    std::int64_t kitId, const std::vector<PluginVelocityLayerBoundary>& candidates)
{
    if (connection == nullptr || kitId <= 0) return false;
    for (const auto& candidate : candidates)
        if (candidate.midiNote < 0 || candidate.midiNote > 127 || candidate.lowerVelocity < 1
            || candidate.upperVelocity > 127 || candidate.lowerVelocity >= candidate.upperVelocity
            || !std::isfinite(candidate.spectralDistance) || candidate.spectralDistance < 0.0
            || candidate.spectralDistance > 1.0 || candidate.algorithmVersion < 1)
            return false;

    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    bool ok = true;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "DELETE FROM plugin_velocity_layer_candidates WHERE kit_id=?", -1, &statement, nullptr) != SQLITE_OK)
        ok = false;
    if (ok)
    {
        sqlite3_bind_int64(statement, 1, kitId);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    if (statement != nullptr) sqlite3_finalize(statement);
    statement = nullptr;
    if (ok && sqlite3_prepare_v2(connection, "INSERT INTO plugin_velocity_layer_candidates(kit_id,midi_note,lower_velocity,upper_velocity,spectral_distance,algorithm_version) VALUES(?,?,?,?,?,?)", -1, &statement, nullptr) != SQLITE_OK)
        ok = false;
    if (ok)
        for (const auto& candidate : candidates)
        {
            sqlite3_bind_int64(statement, 1, kitId);
            sqlite3_bind_int(statement, 2, candidate.midiNote);
            sqlite3_bind_int(statement, 3, candidate.lowerVelocity);
            sqlite3_bind_int(statement, 4, candidate.upperVelocity);
            sqlite3_bind_double(statement, 5, candidate.spectralDistance);
            sqlite3_bind_int(statement, 6, candidate.algorithmVersion);
            if (sqlite3_step(statement) != SQLITE_DONE) { ok = false; break; }
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
        }
    if (statement != nullptr) sqlite3_finalize(statement);
    const int transactionResult = sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && transactionResult == SQLITE_OK;
}

std::vector<PluginVelocityLayerBoundary> SampleDatabase::getPluginVelocityLayerCandidates(std::int64_t kitId) const
{
    std::vector<PluginVelocityLayerBoundary> result;
    if (connection == nullptr || kitId <= 0) return result;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT midi_note,lower_velocity,upper_velocity,spectral_distance,algorithm_version FROM plugin_velocity_layer_candidates WHERE kit_id=? ORDER BY midi_note,lower_velocity", -1, &statement, nullptr) != SQLITE_OK)
        return result;
    sqlite3_bind_int64(statement, 1, kitId);
    while (sqlite3_step(statement) == SQLITE_ROW)
        result.push_back({sqlite3_column_int(statement, 0), sqlite3_column_int(statement, 1),
                          sqlite3_column_int(statement, 2), sqlite3_column_double(statement, 3),
                          sqlite3_column_int(statement, 4)});
    sqlite3_finalize(statement);
    return result;
}

std::vector<PluginVelocityLayerBoundary> SampleDatabase::getPluginVelocityLayerCandidates(
    const juce::String& pluginIdentifier, const juce::String& kitName) const
{
    std::vector<PluginVelocityLayerBoundary> result;
    if (connection == nullptr || pluginIdentifier.trim().isEmpty() || kitName.trim().isEmpty()) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT c.midi_note,c.lower_velocity,c.upper_velocity,c.spectral_distance,c.algorithm_version FROM plugin_velocity_layer_candidates c JOIN plugin_kits k ON k.id=c.kit_id JOIN plugin_instruments p ON p.id=k.plugin_id WHERE p.identifier=? AND k.name=? ORDER BY c.midi_note,c.lower_velocity";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    const auto identifier = pluginIdentifier.trim(), name = kitName.trim();
    sqlite3_bind_text(statement, 1, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW)
        result.push_back({sqlite3_column_int(statement, 0), sqlite3_column_int(statement, 1),
                          sqlite3_column_int(statement, 2), sqlite3_column_double(statement, 3),
                          sqlite3_column_int(statement, 4)});
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::replaceConfirmedPluginVelocityLayers(
    std::int64_t kitId, const std::vector<PluginConfirmedVelocityLayerRange>& ranges)
{
    if (connection == nullptr || kitId <= 0) return false;
    auto sorted = ranges;
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right)
    {
        return left.midiNote < right.midiNote
            || (left.midiNote == right.midiNote && left.minimumVelocity < right.minimumVelocity);
    });
    for (size_t index = 0; index < sorted.size(); ++index)
    {
        const auto& range = sorted[index];
        if (range.midiNote < 0 || range.midiNote > 127 || range.minimumVelocity < 1
            || range.maximumVelocity > 127 || range.minimumVelocity > range.maximumVelocity)
            return false;
        if (index > 0 && sorted[index - 1].midiNote == range.midiNote
            && sorted[index - 1].maximumVelocity >= range.minimumVelocity)
            return false;
    }

    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    bool ok = true;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "DELETE FROM plugin_confirmed_velocity_layers WHERE kit_id=?", -1, &statement, nullptr) != SQLITE_OK)
        ok = false;
    if (ok)
    {
        sqlite3_bind_int64(statement, 1, kitId);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    if (statement != nullptr) sqlite3_finalize(statement);
    statement = nullptr;
    if (ok && sqlite3_prepare_v2(connection, "INSERT INTO plugin_confirmed_velocity_layers(kit_id,midi_note,minimum_velocity,maximum_velocity) VALUES(?,?,?,?)", -1, &statement, nullptr) != SQLITE_OK)
        ok = false;
    if (ok)
        for (const auto& range : sorted)
        {
            sqlite3_bind_int64(statement, 1, kitId);
            sqlite3_bind_int(statement, 2, range.midiNote);
            sqlite3_bind_int(statement, 3, range.minimumVelocity);
            sqlite3_bind_int(statement, 4, range.maximumVelocity);
            if (sqlite3_step(statement) != SQLITE_DONE) { ok = false; break; }
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
        }
    if (statement != nullptr) sqlite3_finalize(statement);
    const int transactionResult = sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && transactionResult == SQLITE_OK;
}

std::vector<PluginConfirmedVelocityLayerRange> SampleDatabase::getConfirmedPluginVelocityLayers(
    const juce::String& pluginIdentifier, const juce::String& kitName) const
{
    std::vector<PluginConfirmedVelocityLayerRange> result;
    if (connection == nullptr || pluginIdentifier.trim().isEmpty() || kitName.trim().isEmpty()) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT r.midi_note,r.minimum_velocity,r.maximum_velocity FROM plugin_confirmed_velocity_layers r JOIN plugin_kits k ON k.id=r.kit_id JOIN plugin_instruments p ON p.id=k.plugin_id WHERE p.identifier=? AND k.name=? ORDER BY r.midi_note,r.minimum_velocity";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    const auto identifier = pluginIdentifier.trim(), name = kitName.trim();
    sqlite3_bind_text(statement, 1, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW)
        result.push_back({sqlite3_column_int(statement, 0), sqlite3_column_int(statement, 1),
                          sqlite3_column_int(statement, 2)});
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::replacePluginRoundRobinData(
    std::int64_t kitId, const std::vector<PluginRoundRobinTake>& takes,
    const std::vector<PluginRoundRobinCandidate>& candidates)
{
    if (connection == nullptr || kitId <= 0) return false;
    for (const auto& take : takes)
        if (take.midiNote < 0 || take.midiNote > 127 || take.velocity < 1 || take.velocity > 127
            || take.takeIndex < 1 || take.takeIndex > 8
            || (!take.acousticFingerprint.isEmpty() && take.acousticFingerprint.length() != 128))
            return false;
    for (const auto& candidate : candidates)
        if (candidate.midiNote < 0 || candidate.midiNote > 127 || candidate.velocity < 1
            || candidate.velocity > 127 || candidate.takeCount < 2 || candidate.distinctVariants < 2
            || candidate.distinctVariants > candidate.takeCount || !std::isfinite(candidate.maximumSpectralDistance)
            || candidate.maximumSpectralDistance < 0.0 || candidate.maximumSpectralDistance > 1.0
            || candidate.algorithmVersion < 1)
            return false;

    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    bool ok = true;
    sqlite3_stmt* statement = nullptr;
    for (const char* sql : {"DELETE FROM plugin_round_robin_candidates WHERE kit_id=?",
                            "DELETE FROM plugin_round_robin_takes WHERE kit_id=?"})
    {
        if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) { ok = false; break; }
        sqlite3_bind_int64(statement, 1, kitId);
        ok = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        statement = nullptr;
        if (!ok) break;
    }
    if (ok && sqlite3_prepare_v2(connection, "INSERT INTO plugin_round_robin_takes(kit_id,midi_note,velocity,take_index,silent,preview_path,acoustic_fingerprint) VALUES(?,?,?,?,?,?,?)", -1, &statement, nullptr) != SQLITE_OK)
        ok = false;
    if (ok)
        for (const auto& take : takes)
        {
            sqlite3_bind_int64(statement, 1, kitId);
            sqlite3_bind_int(statement, 2, take.midiNote);
            sqlite3_bind_int(statement, 3, take.velocity);
            sqlite3_bind_int(statement, 4, take.takeIndex);
            sqlite3_bind_int(statement, 5, take.silent ? 1 : 0);
            sqlite3_bind_text(statement, 6, take.previewPath.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 7, take.acousticFingerprint.toRawUTF8(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(statement) != SQLITE_DONE) { ok = false; break; }
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
        }
    if (statement != nullptr) sqlite3_finalize(statement);
    statement = nullptr;
    if (ok && sqlite3_prepare_v2(connection, "INSERT INTO plugin_round_robin_candidates(kit_id,midi_note,velocity,take_count,distinct_variants,maximum_spectral_distance,algorithm_version) VALUES(?,?,?,?,?,?,?)", -1, &statement, nullptr) != SQLITE_OK)
        ok = false;
    if (ok)
        for (const auto& candidate : candidates)
        {
            sqlite3_bind_int64(statement, 1, kitId);
            sqlite3_bind_int(statement, 2, candidate.midiNote);
            sqlite3_bind_int(statement, 3, candidate.velocity);
            sqlite3_bind_int(statement, 4, candidate.takeCount);
            sqlite3_bind_int(statement, 5, candidate.distinctVariants);
            sqlite3_bind_double(statement, 6, candidate.maximumSpectralDistance);
            sqlite3_bind_int(statement, 7, candidate.algorithmVersion);
            if (sqlite3_step(statement) != SQLITE_DONE) { ok = false; break; }
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
        }
    if (statement != nullptr) sqlite3_finalize(statement);
    const int transactionResult = sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && transactionResult == SQLITE_OK;
}

std::vector<PluginRoundRobinCandidate> SampleDatabase::getPluginRoundRobinCandidates(
    const juce::String& pluginIdentifier, const juce::String& kitName) const
{
    std::vector<PluginRoundRobinCandidate> result;
    if (connection == nullptr || pluginIdentifier.trim().isEmpty() || kitName.trim().isEmpty()) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT c.midi_note,c.velocity,c.take_count,c.distinct_variants,c.maximum_spectral_distance,c.algorithm_version FROM plugin_round_robin_candidates c JOIN plugin_kits k ON k.id=c.kit_id JOIN plugin_instruments p ON p.id=k.plugin_id WHERE p.identifier=? AND k.name=? ORDER BY c.midi_note,c.velocity";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    const auto identifier = pluginIdentifier.trim(), name = kitName.trim();
    sqlite3_bind_text(statement, 1, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW)
        result.push_back({sqlite3_column_int(statement, 0), sqlite3_column_int(statement, 1),
                          sqlite3_column_int(statement, 2), sqlite3_column_int(statement, 3),
                          sqlite3_column_double(statement, 4), sqlite3_column_int(statement, 5)});
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::confirmPluginRoundRobinGroup(std::int64_t kitId, int midiNote, int velocity)
{
    if (connection == nullptr || kitId <= 0 || midiNote < 0 || midiNote > 127 || velocity < 1 || velocity > 127)
        return false;
    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    sqlite3_stmt* statement = nullptr;
    bool ok = sqlite3_prepare_v2(connection,
        "DELETE FROM plugin_confirmed_round_robin_takes WHERE kit_id=? AND midi_note=? AND velocity=?",
        -1, &statement, nullptr) == SQLITE_OK;
    if (ok)
    {
        sqlite3_bind_int64(statement, 1, kitId);
        sqlite3_bind_int(statement, 2, midiNote);
        sqlite3_bind_int(statement, 3, velocity);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    if (statement != nullptr) sqlite3_finalize(statement);
    statement = nullptr;
    if (ok)
        ok = sqlite3_prepare_v2(connection,
            "INSERT INTO plugin_confirmed_round_robin_takes(kit_id,midi_note,velocity,take_index) SELECT t.kit_id,t.midi_note,t.velocity,t.take_index FROM plugin_round_robin_takes t WHERE t.kit_id=? AND t.midi_note=? AND t.velocity=? AND t.silent=0 AND t.preview_path<>'' AND EXISTS(SELECT 1 FROM plugin_round_robin_candidates c WHERE c.kit_id=t.kit_id AND c.midi_note=t.midi_note AND c.velocity=t.velocity)",
            -1, &statement, nullptr) == SQLITE_OK;
    if (ok)
    {
        sqlite3_bind_int64(statement, 1, kitId);
        sqlite3_bind_int(statement, 2, midiNote);
        sqlite3_bind_int(statement, 3, velocity);
        ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(connection) >= 2;
    }
    if (statement != nullptr) sqlite3_finalize(statement);
    const int transactionResult = sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && transactionResult == SQLITE_OK;
}

bool SampleDatabase::removeConfirmedPluginRoundRobinGroup(std::int64_t kitId, int midiNote, int velocity)
{
    if (connection == nullptr || kitId <= 0 || midiNote < 0 || midiNote > 127 || velocity < 1 || velocity > 127)
        return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection,
        "DELETE FROM plugin_confirmed_round_robin_takes WHERE kit_id=? AND midi_note=? AND velocity=?",
        -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(statement, 1, kitId);
    sqlite3_bind_int(statement, 2, midiNote);
    sqlite3_bind_int(statement, 3, velocity);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

std::vector<PluginConfirmedRoundRobinGroup> SampleDatabase::getConfirmedPluginRoundRobinGroups(
    const juce::String& pluginIdentifier, const juce::String& kitName) const
{
    std::vector<PluginConfirmedRoundRobinGroup> result;
    if (connection == nullptr || pluginIdentifier.trim().isEmpty() || kitName.trim().isEmpty()) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT c.midi_note,c.velocity,c.take_index FROM plugin_confirmed_round_robin_takes c JOIN plugin_kits k ON k.id=c.kit_id JOIN plugin_instruments p ON p.id=k.plugin_id WHERE p.identifier=? AND k.name=? ORDER BY c.midi_note,c.velocity,c.take_index";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    const auto identifier = pluginIdentifier.trim(), name = kitName.trim();
    sqlite3_bind_text(statement, 1, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const int note = sqlite3_column_int(statement, 0), velocity = sqlite3_column_int(statement, 1);
        if (result.empty() || result.back().midiNote != note || result.back().velocity != velocity)
            result.push_back({note, velocity, {}});
        result.back().takeIndices.push_back(sqlite3_column_int(statement, 2));
    }
    sqlite3_finalize(statement);
    return result;
}

juce::Array<PluginSoundBrowseRecord> SampleDatabase::getPluginSoundBrowseRecords() const
{
    juce::Array<PluginSoundBrowseRecord> result;
    if (connection == nullptr) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT s.preview_path AS preview_path,p.identifier AS plugin_identifier,p.name AS plugin_name,k.name AS kit_name,s.midi_note AS midi_note,s.velocity AS velocity,s.articulation AS articulation,1 AS take_index FROM plugin_sounds s JOIN plugin_kits k ON k.id=s.kit_id JOIN plugin_instruments p ON p.id=k.plugin_id WHERE s.silent=0 AND s.preview_path<>'' UNION ALL SELECT t.preview_path,p.identifier,p.name,k.name,t.midi_note,t.velocity,'' AS articulation,t.take_index FROM plugin_round_robin_takes t JOIN plugin_kits k ON k.id=t.kit_id JOIN plugin_instruments p ON p.id=k.plugin_id WHERE t.silent=0 AND t.preview_path<>'' AND t.take_index>1 ORDER BY plugin_name COLLATE NOCASE,kit_name COLLATE NOCASE,midi_note,velocity,take_index";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto text = [statement](int column) { const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column)); return value != nullptr ? juce::String::fromUTF8(value) : juce::String(); };
        PluginSoundBrowseRecord record;
        record.previewFile = juce::File(text(0)); record.pluginIdentifier = text(1); record.pluginName = text(2); record.kitName = text(3);
        record.midiNote = sqlite3_column_int(statement, 4); record.velocity = sqlite3_column_int(statement, 5); record.articulation = text(6); record.takeIndex = sqlite3_column_int(statement, 7);
        result.add(std::move(record));
    }
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::insertSample(const ScannedSample& sample, std::int64_t sourceId)
{
    if (connection == nullptr) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO samples(path,name,duration,channels,sample_rate,format,bit_depth,peak,rms,spectral_centroid,transient_strength,fundamental_hz,pitch_confidence,fingerprint,category,classification_confidence,size_bytes,modified_time_ms,acoustic_fingerprint,acoustic_fingerprint_version,analysis_version,source_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET name=excluded.name,duration=excluded.duration,channels=excluded.channels,sample_rate=excluded.sample_rate,format=excluded.format,bit_depth=excluded.bit_depth,peak=excluded.peak,rms=excluded.rms,spectral_centroid=excluded.spectral_centroid,transient_strength=excluded.transient_strength,fundamental_hz=excluded.fundamental_hz,pitch_confidence=excluded.pitch_confidence,fingerprint=excluded.fingerprint,category=CASE WHEN samples.user_category='' THEN excluded.category ELSE samples.user_category END,classification_confidence=excluded.classification_confidence,size_bytes=excluded.size_bytes,modified_time_ms=excluded.modified_time_ms,acoustic_fingerprint=excluded.acoustic_fingerprint,acoustic_fingerprint_version=excluded.acoustic_fingerprint_version,analysis_version=excluded.analysis_version,source_id=COALESCE(excluded.source_id,samples.source_id)";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    const auto path = sample.file.getFullPathName(); const auto name = sample.file.getFileName();
    sqlite3_bind_text(statement,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,name.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_double(statement,3,sample.durationSeconds); sqlite3_bind_int(statement,4,sample.channels); sqlite3_bind_double(statement,5,sample.sampleRate);
    sqlite3_bind_text(statement,6,sample.format.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(statement,7,sample.bitDepth);
    sqlite3_bind_double(statement,8,sample.peak); sqlite3_bind_double(statement,9,sample.rms); sqlite3_bind_double(statement,10,sample.spectralCentroidHz);
    sqlite3_bind_double(statement,11,sample.transientStrength); sqlite3_bind_double(statement,12,sample.fundamentalHz); sqlite3_bind_double(statement,13,sample.pitchConfidence);
    sqlite3_bind_text(statement,14,sample.fingerprint.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(statement,15,sample.category.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_double(statement,16,sample.classificationConfidence);
    sqlite3_bind_int64(statement,17,sample.fileSizeBytes); sqlite3_bind_int64(statement,18,sample.modifiedTimeMillis);
    sqlite3_bind_text(statement,19,sample.acousticFingerprint.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(statement,20,sample.acousticFingerprintVersion);
    sqlite3_bind_int(statement,21,sample.analysisVersion);
    if (sourceId > 0) sqlite3_bind_int64(statement,22,sourceId); else sqlite3_bind_null(statement,22);
    const bool ok=sqlite3_step(statement)==SQLITE_DONE; sqlite3_finalize(statement); return ok;
}

bool SampleDatabase::isSampleUnchanged(const juce::File& file, std::int64_t sizeBytes,
                                       std::int64_t modifiedTimeMillis) const
{
    if (connection == nullptr || sizeBytes <= 0 || modifiedTimeMillis <= 0) return false;
    sqlite3_stmt* query = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT size_bytes,modified_time_ms,acoustic_fingerprint,acoustic_fingerprint_version,analysis_version FROM samples WHERE path=?", -1, &query, nullptr) != SQLITE_OK)
        return false;
    const auto path = file.getFullPathName();
    sqlite3_bind_text(query, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const bool unchanged = sqlite3_step(query) == SQLITE_ROW
        && sqlite3_column_int64(query, 0) == sizeBytes
        && sqlite3_column_int64(query, 1) == modifiedTimeMillis
        && sqlite3_column_text(query, 2) != nullptr
        && sqlite3_column_bytes(query, 2) == 128
        && sqlite3_column_int(query, 3) == 1
        && sqlite3_column_int(query, 4) == currentSampleAnalysisVersion;
    sqlite3_finalize(query);
    return unchanged;
}

std::optional<std::int64_t> SampleDatabase::beginSampleScan(const juce::File& root)
{
    if (connection == nullptr || !root.isDirectory()) return std::nullopt;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "INSERT INTO sample_scan_history(root_path) VALUES(?)", -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    const auto path = root.getFullPathName();
    sqlite3_bind_text(statement, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok) return std::nullopt;
    return sqlite3_last_insert_rowid(connection);
}

bool SampleDatabase::finishSampleScan(std::int64_t scanId, const juce::String& status,
                                      std::int64_t filesConsidered, std::int64_t filesSkipped,
                                      std::int64_t filesIndexed, std::int64_t errors)
{
    if (connection == nullptr || scanId <= 0
        || (status != "completed" && status != "cancelled" && status != "failed")
        || filesConsidered < 0 || filesSkipped < 0 || filesIndexed < 0 || errors < 0)
        return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "UPDATE sample_scan_history SET finished_at=CURRENT_TIMESTAMP,status=?,files_considered=?,files_skipped=?,files_indexed=?,errors=? WHERE id=? AND status='running'";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, status.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, filesConsidered);
    sqlite3_bind_int64(statement, 3, filesSkipped);
    sqlite3_bind_int64(statement, 4, filesIndexed);
    sqlite3_bind_int64(statement, 5, errors);
    sqlite3_bind_int64(statement, 6, scanId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(connection) == 1;
    sqlite3_finalize(statement);
    return ok;
}

std::int64_t SampleDatabase::markRunningSampleScansInterrupted()
{
    if (connection == nullptr) return 0;
    if (sqlite3_exec(connection, "UPDATE sample_scan_history SET status='interrupted',finished_at=CURRENT_TIMESTAMP WHERE status='running'", nullptr, nullptr, nullptr) != SQLITE_OK)
        return 0;
    return sqlite3_changes(connection);
}

juce::Array<SampleScanRecord> SampleDatabase::getRecentSampleScans(int limit) const
{
    juce::Array<SampleScanRecord> result;
    if (connection == nullptr) return result;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT id,root_path,started_at,coalesce(finished_at,''),status,files_considered,files_skipped,files_indexed,errors FROM sample_scan_history ORDER BY id DESC LIMIT ?", -1, &statement, nullptr) != SQLITE_OK)
        return result;
    sqlite3_bind_int(statement, 1, juce::jlimit(1, 200, limit));
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto text = [statement](int column)
        {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
            return value != nullptr ? juce::String::fromUTF8(value) : juce::String();
        };
        result.add({sqlite3_column_int64(statement, 0), text(1), text(2), text(3), text(4),
                    sqlite3_column_int64(statement, 5), sqlite3_column_int64(statement, 6),
                    sqlite3_column_int64(statement, 7), sqlite3_column_int64(statement, 8)});
    }
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::setUserCategory(const juce::File& file, const juce::String& category)
{
    if (connection == nullptr) return false;
    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "UPDATE samples SET user_category=?,category=? WHERE path=?", -1, &statement, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    const auto value=category.trim(); const auto path=file.getFullPathName();
    sqlite3_bind_text(statement,1,value.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,value.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,3,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    bool ok=sqlite3_step(statement)==SQLITE_DONE && sqlite3_changes(connection)>0; sqlite3_finalize(statement);
    if (ok && sqlite3_prepare_v2(connection, "UPDATE plugin_sounds SET category=? WHERE preview_path=?", -1, &statement, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(statement, 1, value.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
    }
    else if (ok) ok = false;
    const int transactionResult = sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && transactionResult == SQLITE_OK;
}

juce::String SampleDatabase::getCategory(const juce::File& file) const
{
    if(connection==nullptr)return "Unknown";sqlite3_stmt* q=nullptr;
    if(sqlite3_prepare_v2(connection,"SELECT category FROM samples WHERE path=?",-1,&q,nullptr)!=SQLITE_OK)return "Unknown";
    const auto path=file.getFullPathName();sqlite3_bind_text(q,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    juce::String value="Unknown";if(sqlite3_step(q)==SQLITE_ROW){const auto* v=reinterpret_cast<const char*>(sqlite3_column_text(q,0));if(v)value=juce::String::fromUTF8(v);}sqlite3_finalize(q);return value;
}

bool SampleDatabase::setUserTags(const juce::File& file, const juce::StringArray& tags)
{
    if (connection == nullptr) return false;
    const auto path=file.getFullPathName();
    if (sqlite3_exec(connection,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK) return false;
    sqlite3_stmt* remove=nullptr;
    if(sqlite3_prepare_v2(connection,"DELETE FROM sample_tags WHERE path=?",-1,&remove,nullptr)!=SQLITE_OK){sqlite3_exec(connection,"ROLLBACK",nullptr,nullptr,nullptr);return false;}
    sqlite3_bind_text(remove,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    bool ok=sqlite3_step(remove)==SQLITE_DONE; sqlite3_finalize(remove);
    sqlite3_stmt* add=nullptr;
    if(ok && sqlite3_prepare_v2(connection,"INSERT OR IGNORE INTO sample_tags(path,tag) SELECT ?,? WHERE EXISTS(SELECT 1 FROM samples WHERE path=?)",-1,&add,nullptr)==SQLITE_OK){
      for(const auto& raw:tags){auto tag=raw.trim();if(tag.isEmpty())continue;sqlite3_reset(add);sqlite3_clear_bindings(add);sqlite3_bind_text(add,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(add,2,tag.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(add,3,path.toRawUTF8(),-1,SQLITE_TRANSIENT);if(sqlite3_step(add)!=SQLITE_DONE){ok=false;break;}}
      sqlite3_finalize(add);
    } else if(ok) ok=false;
    sqlite3_exec(connection,ok?"COMMIT":"ROLLBACK",nullptr,nullptr,nullptr);
    return ok;
}

juce::StringArray SampleDatabase::getUserTags(const juce::File& file) const
{
    juce::StringArray tags;if(connection==nullptr)return tags;sqlite3_stmt* q=nullptr;
    if(sqlite3_prepare_v2(connection,"SELECT tag FROM sample_tags WHERE path=? ORDER BY tag COLLATE NOCASE",-1,&q,nullptr)!=SQLITE_OK)return tags;
    const auto path=file.getFullPathName();sqlite3_bind_text(q,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    while(sqlite3_step(q)==SQLITE_ROW){const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(q,0));if(value)tags.add(juce::String::fromUTF8(value));}sqlite3_finalize(q);return tags;
}

juce::Array<juce::File> SampleDatabase::findSimilar(const juce::File& file,int limit) const
{
    juce::Array<juce::File> result;if(connection==nullptr||limit<=0)return result;
    sqlite3_stmt* q=nullptr;
    const char* sql="SELECT path FROM samples WHERE path<>? ORDER BY (abs(duration-?)/max(0.05,?)+abs(peak-?)+abs(rms-?)/max(0.02,?)+abs(spectral_centroid-?)/max(500.0,?)+abs(transient_strength-?)/max(0.05,?)) ASC LIMIT ?";
    if(sqlite3_prepare_v2(connection,sql,-1,&q,nullptr)!=SQLITE_OK)return result;
    sqlite3_stmt* source=nullptr;
    if(sqlite3_prepare_v2(connection,"SELECT duration,peak,rms,spectral_centroid,transient_strength FROM samples WHERE path=?",-1,&source,nullptr)!=SQLITE_OK){sqlite3_finalize(q);return result;}
    const auto path=file.getFullPathName();sqlite3_bind_text(source,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    if(sqlite3_step(source)!=SQLITE_ROW){sqlite3_finalize(source);sqlite3_finalize(q);return result;}
    const double d=sqlite3_column_double(source,0),p=sqlite3_column_double(source,1),r=sqlite3_column_double(source,2),c=sqlite3_column_double(source,3),t=sqlite3_column_double(source,4);sqlite3_finalize(source);
    sqlite3_bind_text(q,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_double(q,2,d);sqlite3_bind_double(q,3,d);sqlite3_bind_double(q,4,p);sqlite3_bind_double(q,5,r);sqlite3_bind_double(q,6,r);sqlite3_bind_double(q,7,c);sqlite3_bind_double(q,8,c);sqlite3_bind_double(q,9,t);sqlite3_bind_double(q,10,t);sqlite3_bind_int(q,11,limit);
    while(sqlite3_step(q)==SQLITE_ROW){const auto* v=reinterpret_cast<const char*>(sqlite3_column_text(q,0));if(v)result.add(juce::File(juce::String::fromUTF8(v)));}sqlite3_finalize(q);return result;
}

juce::Array<juce::File> SampleDatabase::findExactDuplicates(const juce::File& file,int limit) const
{
    juce::Array<juce::File> result;
    if(connection==nullptr||limit<=0)return result;
    sqlite3_stmt* query=nullptr;
    const char* sql="SELECT path FROM samples WHERE fingerprint<>'' AND fingerprint=(SELECT fingerprint FROM samples WHERE path=?) AND path<>? ORDER BY path COLLATE NOCASE LIMIT ?";
    if(sqlite3_prepare_v2(connection,sql,-1,&query,nullptr)!=SQLITE_OK)return result;
    const auto path=file.getFullPathName();
    sqlite3_bind_text(query,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(query,2,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(query,3,juce::jlimit(1,1000,limit));
    while(sqlite3_step(query)==SQLITE_ROW)
    {
        const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(query,0));
        if(value!=nullptr)result.add(juce::File(juce::String::fromUTF8(value)));
    }
    sqlite3_finalize(query);
    return result;
}

std::vector<PerceptualDuplicateMatch> SampleDatabase::findPerceptualDuplicates(const juce::File& file,
                                                                              double minimumSimilarity,
                                                                              int limit) const
{
    std::vector<PerceptualDuplicateMatch> matches;
    if (connection == nullptr || limit <= 0 || !std::isfinite(minimumSimilarity)) return matches;
    minimumSimilarity = juce::jlimit(0.0, 1.0, minimumSimilarity);
    const auto path = file.getFullPathName();
    sqlite3_stmt* source = nullptr;
    const char* select = "SELECT duration,peak,rms,spectral_centroid,transient_strength,fundamental_hz,pitch_confidence,fingerprint,acoustic_fingerprint FROM samples WHERE path=?";
    if (sqlite3_prepare_v2(connection, select, -1, &source, nullptr) != SQLITE_OK) return matches;
    sqlite3_bind_text(source, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(source) != SQLITE_ROW) { sqlite3_finalize(source); return matches; }
    const double duration = sqlite3_column_double(source, 0);
    const double peak = sqlite3_column_double(source, 1);
    const double rms = sqlite3_column_double(source, 2);
    const double centroid = sqlite3_column_double(source, 3);
    const double transient = sqlite3_column_double(source, 4);
    const double pitch = sqlite3_column_double(source, 5);
    const double pitchConfidence = sqlite3_column_double(source, 6);
    const auto* fingerprintText = reinterpret_cast<const char*>(sqlite3_column_text(source, 7));
    const juce::String fingerprint = fingerprintText != nullptr ? juce::String::fromUTF8(fingerprintText) : juce::String();
    const auto* acousticText = reinterpret_cast<const char*>(sqlite3_column_text(source, 8));
    const juce::String acousticFingerprint = acousticText != nullptr ? juce::String::fromUTF8(acousticText) : juce::String();
    sqlite3_finalize(source);
    std::array<std::uint8_t, 64> sourceAcoustic{};
    if (duration <= 0.0 || rms <= 1.0e-5 || !decodeAcousticFingerprint(acousticFingerprint, sourceAcoustic)) return matches;

    struct Candidate { PerceptualDuplicateMatch match; };
    std::vector<Candidate> candidates;
    sqlite3_stmt* rows = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT path,duration,peak,rms,spectral_centroid,transient_strength,fundamental_hz,pitch_confidence,fingerprint,acoustic_fingerprint FROM samples WHERE path<>? AND duration BETWEEN ? AND ?", -1, &rows, nullptr) != SQLITE_OK)
        return matches;
    sqlite3_bind_text(rows, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(rows, 2, duration * 0.95);
    sqlite3_bind_double(rows, 3, duration * 1.05);
    const double sourceCrest = peak > 1.0e-9 ? rms / peak : 0.0;
    const double sourceTransient = peak > 1.0e-9 ? transient / peak : 0.0;
    while (sqlite3_step(rows) == SQLITE_ROW)
    {
        const auto* candidatePath = reinterpret_cast<const char*>(sqlite3_column_text(rows, 0));
        const auto candidateDuration = sqlite3_column_double(rows, 1);
        const auto candidatePeak = sqlite3_column_double(rows, 2);
        const auto candidateRms = sqlite3_column_double(rows, 3);
        const auto candidateCentroid = sqlite3_column_double(rows, 4);
        const auto candidateTransientStrength = sqlite3_column_double(rows, 5);
        const auto candidatePitch = sqlite3_column_double(rows, 6);
        const auto candidatePitchConfidence = sqlite3_column_double(rows, 7);
        const auto* candidateFingerprintText = reinterpret_cast<const char*>(sqlite3_column_text(rows, 8));
        const juce::String candidateFingerprint = candidateFingerprintText != nullptr ? juce::String::fromUTF8(candidateFingerprintText) : juce::String();
        const auto* candidateAcousticText = reinterpret_cast<const char*>(sqlite3_column_text(rows, 9));
        const juce::String candidateAcousticFingerprint = candidateAcousticText != nullptr ? juce::String::fromUTF8(candidateAcousticText) : juce::String();
        if (candidatePath == nullptr || candidateDuration <= 0.0 || (!fingerprint.isEmpty() && fingerprint == candidateFingerprint)) continue;
        if (candidateRms <= 1.0e-5) continue;
        const double durationError = std::abs(candidateDuration - duration) / duration;
        if (durationError > 0.05) continue;
        std::array<std::uint8_t, 64> candidateAcoustic{};
        if (!decodeAcousticFingerprint(candidateAcousticFingerprint, candidateAcoustic)) continue;
        double acousticError = 0.0;
        for (size_t index = 0; index < sourceAcoustic.size(); ++index)
            acousticError += std::abs(static_cast<int>(sourceAcoustic[index]) - static_cast<int>(candidateAcoustic[index])) / 255.0;
        acousticError /= static_cast<double>(sourceAcoustic.size());
        const double acousticSimilarity = 1.0 - acousticError;
        if (acousticSimilarity < 0.88) continue;
        const double candidateCrest = candidatePeak > 1.0e-9 ? candidateRms / candidatePeak : 0.0;
        const double candidateTransient = candidatePeak > 1.0e-9 ? candidateTransientStrength / candidatePeak : 0.0;
        const double centroidError = std::abs(candidateCentroid - centroid) / juce::jmax(300.0, std::abs(centroid));
        const double crestError = std::abs(candidateCrest - sourceCrest);
        const double transientError = std::abs(candidateTransient - sourceTransient);
        const bool hasPitch = pitchConfidence >= 0.65 && candidatePitchConfidence >= 0.65 && pitch > 0.0 && candidatePitch > 0.0;
        const double pitchError = hasPitch ? std::abs(std::log2(candidatePitch / pitch)) : 0.0;
        if (centroidError > 0.25 || crestError > 0.15 || transientError > 0.20 || (hasPitch && pitchError > 0.08)) continue;
        const double featureSimilarity = juce::jlimit(0.0, 1.0, 1.0 - (0.25 * durationError / 0.05
            + 0.25 * centroidError / 0.25 + 0.20 * crestError / 0.15
            + 0.20 * transientError / 0.20 + (hasPitch ? 0.10 * pitchError / 0.08 : 0.0)));
        const double similarity = juce::jlimit(0.0, 1.0, 0.70 * acousticSimilarity + 0.30 * featureSimilarity);
        if (similarity >= minimumSimilarity)
            candidates.push_back({{juce::File(juce::String::fromUTF8(candidatePath)), similarity}});
    }
    sqlite3_finalize(rows);
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right)
                     { return left.match.similarity > right.match.similarity; });
    if (candidates.size() > static_cast<std::size_t>(limit)) candidates.resize(static_cast<std::size_t>(limit));
    matches.reserve(candidates.size());
    for (auto& candidate : candidates) matches.push_back(std::move(candidate.match));
    return matches;
}

bool SampleDatabase::saveMappingProfile(const juce::String& name,const MidiMappingProfile& profile)
{
    if(connection==nullptr||name.trim().isEmpty())return false;
    if(sqlite3_exec(connection,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK)return false;
    sqlite3_stmt* p=nullptr;sqlite3_stmt* remove=nullptr;sqlite3_stmt* add=nullptr;bool ok=true;
    if(sqlite3_prepare_v2(connection,"INSERT INTO mapping_profiles(name) VALUES(?) ON CONFLICT(name) DO NOTHING",-1,&p,nullptr)!=SQLITE_OK)ok=false;
    if(ok){auto n=name.trim();sqlite3_bind_text(p,1,n.toRawUTF8(),-1,SQLITE_TRANSIENT);ok=sqlite3_step(p)==SQLITE_DONE;}if(p)sqlite3_finalize(p);
    if(ok&&sqlite3_prepare_v2(connection,"DELETE FROM mapping_entries WHERE profile_name=?",-1,&remove,nullptr)==SQLITE_OK){auto n=name.trim();sqlite3_bind_text(remove,1,n.toRawUTF8(),-1,SQLITE_TRANSIENT);ok=sqlite3_step(remove)==SQLITE_DONE;}else if(ok)ok=false;if(remove)sqlite3_finalize(remove);
    if(ok&&sqlite3_prepare_v2(connection,"INSERT INTO mapping_entries(profile_name,instrument,articulation,note) VALUES(?,?,?,?)",-1,&add,nullptr)==SQLITE_OK){
      const auto n=name.trim();for(const auto& entry:profile.entries()){sqlite3_reset(add);sqlite3_clear_bindings(add);sqlite3_bind_text(add,1,n.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(add,2,static_cast<int>(entry.instrument));sqlite3_bind_int(add,3,static_cast<int>(entry.articulation));sqlite3_bind_int(add,4,entry.note);if(sqlite3_step(add)!=SQLITE_DONE){ok=false;break;}}
    }else if(ok)ok=false;if(add)sqlite3_finalize(add);
    sqlite3_exec(connection,ok?"COMMIT":"ROLLBACK",nullptr,nullptr,nullptr);return ok;
}

std::optional<MidiMappingProfile> SampleDatabase::loadMappingProfile(const juce::String& name) const
{
    if(connection==nullptr)return std::nullopt;sqlite3_stmt* q=nullptr;
    if(sqlite3_prepare_v2(connection,"SELECT e.instrument,e.articulation,e.note FROM mapping_profiles p JOIN mapping_entries e ON e.profile_name=p.name WHERE p.name=? ORDER BY e.instrument,e.articulation",-1,&q,nullptr)!=SQLITE_OK)return std::nullopt;
    const auto n=name.trim();sqlite3_bind_text(q,1,n.toRawUTF8(),-1,SQLITE_TRANSIENT);std::vector<MidiNoteMapping> entries;
    while(sqlite3_step(q)==SQLITE_ROW){const int i=sqlite3_column_int(q,0),a=sqlite3_column_int(q,1),note=sqlite3_column_int(q,2);if(i<0||i>static_cast<int>(DrumInstrument::Percussion)||a<0||a>static_cast<int>(DrumArticulation::Edge)||note<0||note>127){sqlite3_finalize(q);return std::nullopt;}entries.push_back({static_cast<DrumInstrument>(i),static_cast<DrumArticulation>(a),static_cast<std::uint8_t>(note)});}
    sqlite3_finalize(q);if(entries.empty())return std::nullopt;
    try{return MidiMappingProfile(std::move(entries));}catch(...){return std::nullopt;}
}

bool SampleDatabase::insertSample(const juce::File& file, double duration, int channels, double rate)
{
    if (connection == nullptr)
        return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT OR IGNORE INTO samples(path,name,duration,channels,sample_rate) VALUES(?,?,?,?,?)";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK)
        return false;
    const auto path = file.getFullPathName();
    const auto name = file.getFileName();
    sqlite3_bind_text(statement, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(statement, 3, duration);
    sqlite3_bind_int(statement, 4, channels);
    sqlite3_bind_double(statement, 5, rate);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

juce::Array<juce::File> SampleDatabase::getAllPaths() const
{
    juce::Array<juce::File> files;
    if (connection == nullptr)
        return files;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT path FROM samples ORDER BY name COLLATE NOCASE", -1, &statement, nullptr) != SQLITE_OK)
        return files;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        if (value != nullptr)
            files.add(juce::File(juce::String::fromUTF8(value)));
    }
    sqlite3_finalize(statement);
    return files;
}

std::optional<juce::File> SampleDatabase::getFirstSampleForCategory(const juce::String& category) const
{
    if (connection == nullptr || category.trim().isEmpty()) return std::nullopt;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT path FROM samples WHERE COALESCE(NULLIF(user_category,''),category)=? ORDER BY name COLLATE NOCASE LIMIT 100";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return std::nullopt;
    const auto trimmed = category.trim();
    sqlite3_bind_text(statement, 1, trimmed.toRawUTF8(), -1, SQLITE_TRANSIENT);
    std::optional<juce::File> result;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        if (value == nullptr) continue;
        juce::File file(juce::String::fromUTF8(value));
        if (file.existsAsFile()) { result = file; break; }
    }
    sqlite3_finalize(statement);
    return result;
}

juce::Array<SampleBrowseRecord> SampleDatabase::getSampleBrowseRecords() const
{
    juce::Array<SampleBrowseRecord> records;
    if (connection == nullptr) return records;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT s.path,COALESCE(NULLIF(s.user_category,''),s.category),s.duration,s.spectral_centroid,COALESCE(src.source_type,''),COALESCE(src.display_name,''),COALESCE(src.identifier,''),s.classification_confidence,(s.user_category<>'') FROM samples s LEFT JOIN sample_sources src ON src.id=s.source_id ORDER BY s.name COLLATE NOCASE";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return records;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* path = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        const auto* category = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (path == nullptr) continue;
        SampleBrowseRecord record;
        record.file = juce::File(juce::String::fromUTF8(path));
        record.category = category != nullptr ? juce::String::fromUTF8(category) : juce::String("Unknown");
        const auto* sourceType = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
        const auto* sourceName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
        const auto* sourceIdentifier = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
        record.sourceType = sourceType != nullptr ? juce::String::fromUTF8(sourceType) : juce::String();
        record.sourceName = sourceName != nullptr ? juce::String::fromUTF8(sourceName) : juce::String();
        record.sourceIdentifier = sourceIdentifier != nullptr ? juce::String::fromUTF8(sourceIdentifier) : juce::String();
        record.durationSeconds = sqlite3_column_double(statement, 2);
        record.brightnessHz = sqlite3_column_double(statement, 3);
        record.classificationConfidence = sqlite3_column_double(statement, 7);
        record.manuallyClassified = sqlite3_column_int(statement, 8) != 0;
        records.add(std::move(record));
    }
    sqlite3_finalize(statement);
    return records;
}

juce::Array<SampleBrowseRecord> SampleDatabase::searchSampleBrowseRecords(const juce::String& query,
                                                                           const juce::String& category,
                                                                           int limit, int offset) const
{
    juce::Array<SampleBrowseRecord> records;
    if (connection == nullptr || limit <= 0) return records;

    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT s.path,COALESCE(NULLIF(s.user_category,''),s.category),s.duration,s.spectral_centroid,COALESCE(src.source_type,''),COALESCE(src.display_name,''),COALESCE(src.identifier,''),s.classification_confidence,(s.user_category<>'') "
                      "FROM samples s LEFT JOIN sample_sources src ON src.id=s.source_id "
                      "WHERE (?1='' OR instr(lower(s.name),lower(?2))>0 OR instr(lower(s.path),lower(?3))>0) "
                      "AND (?4='' OR COALESCE(NULLIF(s.user_category,''),s.category)=?5 COLLATE NOCASE) "
                      "ORDER BY s.name COLLATE NOCASE,s.path COLLATE NOCASE LIMIT ?6 OFFSET ?7";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return records;

    const auto trimmedQuery = query.trim();
    const auto trimmedCategory = category.trim();
    sqlite3_bind_text(statement, 1, trimmedQuery.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, trimmedQuery.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, trimmedQuery.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, trimmedCategory.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, trimmedCategory.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 6, juce::jmin(limit, 1000));
    sqlite3_bind_int(statement, 7, juce::jmax(0, offset));

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* path = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        if (path == nullptr) continue;
        SampleBrowseRecord record;
        record.file = juce::File(juce::String::fromUTF8(path));
        const auto* categoryText = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        record.category = categoryText != nullptr ? juce::String::fromUTF8(categoryText) : juce::String("Unknown");
        const auto* sourceType = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
        const auto* sourceName = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
        const auto* sourceIdentifier = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
        record.sourceType = sourceType != nullptr ? juce::String::fromUTF8(sourceType) : juce::String();
        record.sourceName = sourceName != nullptr ? juce::String::fromUTF8(sourceName) : juce::String();
        record.sourceIdentifier = sourceIdentifier != nullptr ? juce::String::fromUTF8(sourceIdentifier) : juce::String();
        record.durationSeconds = sqlite3_column_double(statement, 2);
        record.brightnessHz = sqlite3_column_double(statement, 3);
        record.classificationConfidence = sqlite3_column_double(statement, 7);
        record.manuallyClassified = sqlite3_column_int(statement, 8) != 0;
        records.add(std::move(record));
    }
    sqlite3_finalize(statement);
    return records;
}

SampleBrowsePage SampleDatabase::searchSampleBrowseRecords(const SampleBrowseFilter& filter,
                                                            int limit, int offset) const
{
    SampleBrowsePage page;
    if (connection == nullptr || limit <= 0) return page;

    const char* ordering = "s.name COLLATE NOCASE,s.path COLLATE NOCASE";
    switch (filter.sort)
    {
        case 2: ordering = "s.duration ASC,s.name COLLATE NOCASE,s.path COLLATE NOCASE"; break;
        case 3: ordering = "s.duration DESC,s.name COLLATE NOCASE,s.path COLLATE NOCASE"; break;
        case 4: ordering = "s.spectral_centroid DESC,s.name COLLATE NOCASE,s.path COLLATE NOCASE"; break;
        case 5: ordering = "s.spectral_centroid ASC,s.name COLLATE NOCASE,s.path COLLATE NOCASE"; break;
        default: break;
    }
    const auto sql = juce::String(
        "SELECT s.path,COALESCE(NULLIF(s.user_category,''),s.category),s.duration,s.spectral_centroid,"
        "COALESCE(src.source_type,''),COALESCE(src.display_name,''),COALESCE(src.identifier,''),"
        "s.classification_confidence,(s.user_category<>''),COALESCE(p.identifier,''),COALESCE(p.name,''),"
        "COALESCE(k.name,''),COALESCE(ps.midi_note,rr.midi_note,-1),COALESCE(ps.velocity,rr.velocity,0),"
        "COALESCE(ps.articulation,''),COALESCE(rr.take_index,1) "
        "FROM samples s LEFT JOIN sample_sources src ON src.id=s.source_id "
        "LEFT JOIN plugin_sounds ps ON ps.preview_path=s.path "
        "LEFT JOIN (SELECT preview_path,min(kit_id) kit_id,min(midi_note) midi_note,min(velocity) velocity,min(take_index) take_index "
        "FROM plugin_round_robin_takes WHERE silent=0 AND preview_path<>'' GROUP BY preview_path) rr ON rr.preview_path=s.path "
        "LEFT JOIN plugin_kits k ON k.id=COALESCE(ps.kit_id,rr.kit_id) "
        "LEFT JOIN plugin_instruments p ON p.id=k.plugin_id "
        "WHERE (?1='' OR instr(lower(s.name),lower(?1))>0 OR instr(lower(s.path),lower(?1))>0 "
        "OR instr(lower(p.name),lower(?1))>0 OR instr(lower(k.name),lower(?1))>0 "
        "OR EXISTS(SELECT 1 FROM sample_tags st WHERE st.path=s.path AND instr(lower(st.tag),lower(?1))>0)) "
        "AND (?2='' OR COALESCE(NULLIF(s.user_category,''),s.category)=?2 COLLATE NOCASE) "
        "AND (?3='' OR src.source_type=?3) AND (?4='' OR src.identifier=?4) "
        "AND (?5='' OR p.name=?5 COLLATE NOCASE) AND (?6='' OR k.name=?6 COLLATE NOCASE) "
        "AND (?7=0 OR (s.user_category='' AND (s.category='Unknown' OR s.classification_confidence<0.75))) "
        "AND (?8=1 OR (?8=2 AND s.duration<0.1) OR (?8=3 AND s.duration>=0.1 AND s.duration<0.5) "
        "OR (?8=4 AND s.duration>=0.5 AND s.duration<2.0) OR (?8=5 AND s.duration>=2.0) "
        "OR (?8=6 AND s.spectral_centroid>0 AND s.spectral_centroid<1000) "
        "OR (?8=7 AND s.spectral_centroid>=1000 AND s.spectral_centroid<3000) "
        "OR (?8=8 AND s.spectral_centroid>=3000)) ORDER BY ") + ordering
        + " LIMIT ?9 OFFSET ?10";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, sql.toRawUTF8(), -1, &statement, nullptr) != SQLITE_OK) return page;
    const auto bindText = [statement](int index, const juce::String& value)
    {
        const auto trimmed = value.trim();
        sqlite3_bind_text(statement, index, trimmed.toRawUTF8(), -1, SQLITE_TRANSIENT);
    };
    bindText(1, filter.query); bindText(2, filter.category); bindText(3, filter.sourceType);
    bindText(4, filter.sourceIdentifier); bindText(5, filter.pluginName); bindText(6, filter.kitName);
    sqlite3_bind_int(statement, 7, filter.needsReview ? 1 : 0);
    sqlite3_bind_int(statement, 8, juce::jlimit(1, 8, filter.feature));
    const int pageLimit = juce::jlimit(1, 1000, limit);
    sqlite3_bind_int(statement, 9, pageLimit + 1);
    sqlite3_bind_int(statement, 10, juce::jmax(0, offset));

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        if (page.records.size() == pageLimit) { page.hasMore = true; break; }
        const auto text = [statement](int column)
        {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
            return value != nullptr ? juce::String::fromUTF8(value) : juce::String();
        };
        SampleBrowseRecord record;
        record.file = juce::File(text(0)); record.category = text(1);
        record.durationSeconds = sqlite3_column_double(statement, 2);
        record.brightnessHz = sqlite3_column_double(statement, 3);
        record.sourceType = text(4); record.sourceName = text(5); record.sourceIdentifier = text(6);
        record.classificationConfidence = sqlite3_column_double(statement, 7);
        record.manuallyClassified = sqlite3_column_int(statement, 8) != 0;
        record.pluginIdentifier = text(9); record.pluginName = text(10); record.kitName = text(11);
        record.midiNote = sqlite3_column_int(statement, 12); record.velocity = sqlite3_column_int(statement, 13);
        record.articulation = text(14); record.takeIndex = sqlite3_column_int(statement, 15);
        page.records.add(std::move(record));
    }
    sqlite3_finalize(statement);
    return page;
}

juce::Array<juce::File> SampleDatabase::searchPaths(const juce::String& query,int limit) const
{
    juce::Array<juce::File> files;if(connection==nullptr||limit<=0)return files;sqlite3_stmt* statement=nullptr;
    if(sqlite3_prepare_v2(connection,"SELECT path FROM samples WHERE instr(lower(name),lower(?))>0 OR instr(lower(path),lower(?))>0 ORDER BY name COLLATE NOCASE LIMIT ?",-1,&statement,nullptr)!=SQLITE_OK)return files;
    const auto term=query.trim();sqlite3_bind_text(statement,1,term.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(statement,2,term.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(statement,3,juce::jmin(limit,1000));
    while(sqlite3_step(statement)==SQLITE_ROW){const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(statement,0));if(value)files.add(juce::File(juce::String::fromUTF8(value)));}sqlite3_finalize(statement);return files;
}
