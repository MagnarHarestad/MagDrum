#include "SampleDatabase.h"
#include "WavScanner.h"
#include <array>
#include <algorithm>
#include <cmath>

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
    const char* schema = "CREATE TABLE IF NOT EXISTS samples (path TEXT PRIMARY KEY,name TEXT NOT NULL,duration REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,format TEXT NOT NULL DEFAULT '',bit_depth INTEGER NOT NULL DEFAULT 0,peak REAL NOT NULL DEFAULT 0,rms REAL NOT NULL DEFAULT 0,spectral_centroid REAL NOT NULL DEFAULT 0,transient_strength REAL NOT NULL DEFAULT 0,fundamental_hz REAL NOT NULL DEFAULT 0,pitch_confidence REAL NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT 'Unknown',classification_confidence REAL NOT NULL DEFAULT 0,user_category TEXT NOT NULL DEFAULT '',size_bytes INTEGER NOT NULL DEFAULT 0,modified_time_ms INTEGER NOT NULL DEFAULT 0,acoustic_fingerprint TEXT NOT NULL DEFAULT '',acoustic_fingerprint_version INTEGER NOT NULL DEFAULT 0)";
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
        "ALTER TABLE samples ADD COLUMN acoustic_fingerprint_version INTEGER NOT NULL DEFAULT 0"
    };
    for (const auto* migration : migrations)
        sqlite3_exec(connection, migration, nullptr, nullptr, nullptr); // duplicate-column means already upgraded
    if (sqlite3_exec(connection, "CREATE INDEX IF NOT EXISTS samples_fingerprint ON samples(fingerprint); CREATE INDEX IF NOT EXISTS samples_duration ON samples(duration)", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    const char* pluginSchema = "CREATE TABLE IF NOT EXISTS schema_info(singleton INTEGER PRIMARY KEY CHECK(singleton=1),version INTEGER NOT NULL); INSERT INTO schema_info(singleton,version) VALUES(1,6) ON CONFLICT(singleton) DO UPDATE SET version=max(version,excluded.version); CREATE TABLE IF NOT EXISTS plugin_instruments(id INTEGER PRIMARY KEY,identifier TEXT NOT NULL UNIQUE,format TEXT NOT NULL,name TEXT NOT NULL,manufacturer TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT '',file TEXT NOT NULL DEFAULT '',version TEXT NOT NULL DEFAULT '',last_scanned TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP); CREATE INDEX IF NOT EXISTS plugin_instruments_name ON plugin_instruments(name COLLATE NOCASE); CREATE TABLE IF NOT EXISTS plugin_kits(id INTEGER PRIMARY KEY,plugin_id INTEGER NOT NULL,name TEXT NOT NULL,plugin_state BLOB,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,UNIQUE(plugin_id,name),FOREIGN KEY(plugin_id) REFERENCES plugin_instruments(id) ON DELETE CASCADE); CREATE TABLE IF NOT EXISTS plugin_sounds(id INTEGER PRIMARY KEY,kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL CHECK(midi_note BETWEEN 0 AND 127),velocity INTEGER NOT NULL CHECK(velocity BETWEEN 1 AND 127),articulation TEXT NOT NULL DEFAULT '',silent INTEGER NOT NULL DEFAULT 0,preview_path TEXT NOT NULL DEFAULT '',duration REAL NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,peak REAL NOT NULL DEFAULT 0,rms REAL NOT NULL DEFAULT 0,spectral_centroid REAL NOT NULL DEFAULT 0,transient_strength REAL NOT NULL DEFAULT 0,fundamental_hz REAL NOT NULL DEFAULT 0,pitch_confidence REAL NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT 'Unknown',classification_confidence REAL NOT NULL DEFAULT 0,UNIQUE(kit_id,midi_note,velocity,articulation),FOREIGN KEY(kit_id) REFERENCES plugin_kits(id) ON DELETE CASCADE); CREATE INDEX IF NOT EXISTS plugin_sounds_note ON plugin_sounds(midi_note,velocity)";
    if (sqlite3_exec(connection, pluginSchema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN fundamental_hz REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN pitch_confidence REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    if (sqlite3_exec(connection, "CREATE TABLE IF NOT EXISTS sample_tags(path TEXT NOT NULL,tag TEXT NOT NULL,PRIMARY KEY(path,tag),FOREIGN KEY(path) REFERENCES samples(path) ON DELETE CASCADE); CREATE INDEX IF NOT EXISTS sample_tags_tag ON sample_tags(tag)", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    if (sqlite3_exec(connection,"CREATE TABLE IF NOT EXISTS mapping_profiles(name TEXT PRIMARY KEY); CREATE TABLE IF NOT EXISTS mapping_entries(profile_name TEXT NOT NULL,instrument INTEGER NOT NULL,articulation INTEGER NOT NULL,note INTEGER NOT NULL CHECK(note BETWEEN 0 AND 127),PRIMARY KEY(profile_name,instrument,articulation),FOREIGN KEY(profile_name) REFERENCES mapping_profiles(name) ON DELETE CASCADE); CREATE TABLE IF NOT EXISTS sample_scan_history(id INTEGER PRIMARY KEY,root_path TEXT NOT NULL,started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,finished_at TEXT,status TEXT NOT NULL DEFAULT 'running',files_considered INTEGER NOT NULL DEFAULT 0,files_skipped INTEGER NOT NULL DEFAULT 0,files_indexed INTEGER NOT NULL DEFAULT 0,errors INTEGER NOT NULL DEFAULT 0)",nullptr,nullptr,nullptr)!=SQLITE_OK)
        return false;
    return true;
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

bool SampleDatabase::upsertPluginSound(std::int64_t kitId, const PluginSoundRecord& sound)
{
    if (connection == nullptr || kitId <= 0 || sound.midiNote < 0 || sound.midiNote > 127 || sound.velocity < 1 || sound.velocity > 127 || sound.duration < 0.0 || sound.sampleRate < 0.0 || sound.channels < 0 || sound.peak < 0.0 || sound.rms < 0.0 || sound.fundamentalHz < 0.0 || sound.pitchConfidence < 0.0 || sound.pitchConfidence > 1.0 || sound.confidence < 0.0 || sound.confidence > 1.0) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO plugin_sounds(kit_id,midi_note,velocity,articulation,silent,preview_path,duration,sample_rate,channels,peak,rms,spectral_centroid,transient_strength,fundamental_hz,pitch_confidence,fingerprint,category,classification_confidence) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(kit_id,midi_note,velocity,articulation) DO UPDATE SET silent=excluded.silent,preview_path=excluded.preview_path,duration=excluded.duration,sample_rate=excluded.sample_rate,channels=excluded.channels,peak=excluded.peak,rms=excluded.rms,spectral_centroid=excluded.spectral_centroid,transient_strength=excluded.transient_strength,fundamental_hz=excluded.fundamental_hz,pitch_confidence=excluded.pitch_confidence,fingerprint=excluded.fingerprint,category=excluded.category,classification_confidence=excluded.classification_confidence";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(statement, 1, kitId); sqlite3_bind_int(statement, 2, sound.midiNote); sqlite3_bind_int(statement, 3, sound.velocity);
    sqlite3_bind_text(statement, 4, sound.articulation.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_int(statement, 5, sound.silent ? 1 : 0);
    sqlite3_bind_text(statement, 6, sound.previewPath.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_double(statement, 7, sound.duration);
    sqlite3_bind_double(statement, 8, sound.sampleRate); sqlite3_bind_int(statement, 9, sound.channels); sqlite3_bind_double(statement, 10, sound.peak);
    sqlite3_bind_double(statement, 11, sound.rms); sqlite3_bind_double(statement, 12, sound.spectralCentroid); sqlite3_bind_double(statement, 13, sound.transientStrength);
    sqlite3_bind_double(statement, 14, sound.fundamentalHz); sqlite3_bind_double(statement, 15, sound.pitchConfidence);
    sqlite3_bind_text(statement, 16, sound.fingerprint.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(statement, 17, sound.category.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_double(statement, 18, sound.confidence);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

juce::Array<PluginSoundBrowseRecord> SampleDatabase::getPluginSoundBrowseRecords() const
{
    juce::Array<PluginSoundBrowseRecord> result;
    if (connection == nullptr) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT s.preview_path,p.identifier,p.name,k.name,s.midi_note,s.velocity,s.articulation FROM plugin_sounds s JOIN plugin_kits k ON k.id=s.kit_id JOIN plugin_instruments p ON p.id=k.plugin_id WHERE s.silent=0 AND s.preview_path<>'' ORDER BY p.name COLLATE NOCASE,k.name COLLATE NOCASE,s.midi_note,s.velocity";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto text = [statement](int column) { const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column)); return value != nullptr ? juce::String::fromUTF8(value) : juce::String(); };
        PluginSoundBrowseRecord record;
        record.previewFile = juce::File(text(0)); record.pluginIdentifier = text(1); record.pluginName = text(2); record.kitName = text(3);
        record.midiNote = sqlite3_column_int(statement, 4); record.velocity = sqlite3_column_int(statement, 5); record.articulation = text(6);
        result.add(std::move(record));
    }
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::insertSample(const ScannedSample& sample)
{
    if (connection == nullptr) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO samples(path,name,duration,channels,sample_rate,format,bit_depth,peak,rms,spectral_centroid,transient_strength,fundamental_hz,pitch_confidence,fingerprint,category,classification_confidence,size_bytes,modified_time_ms,acoustic_fingerprint,acoustic_fingerprint_version) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET name=excluded.name,duration=excluded.duration,channels=excluded.channels,sample_rate=excluded.sample_rate,format=excluded.format,bit_depth=excluded.bit_depth,peak=excluded.peak,rms=excluded.rms,spectral_centroid=excluded.spectral_centroid,transient_strength=excluded.transient_strength,fundamental_hz=excluded.fundamental_hz,pitch_confidence=excluded.pitch_confidence,fingerprint=excluded.fingerprint,category=CASE WHEN samples.user_category='' THEN excluded.category ELSE samples.user_category END,classification_confidence=excluded.classification_confidence,size_bytes=excluded.size_bytes,modified_time_ms=excluded.modified_time_ms,acoustic_fingerprint=excluded.acoustic_fingerprint,acoustic_fingerprint_version=excluded.acoustic_fingerprint_version";
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
    const bool ok=sqlite3_step(statement)==SQLITE_DONE; sqlite3_finalize(statement); return ok;
}

bool SampleDatabase::isSampleUnchanged(const juce::File& file, std::int64_t sizeBytes,
                                       std::int64_t modifiedTimeMillis) const
{
    if (connection == nullptr || sizeBytes <= 0 || modifiedTimeMillis <= 0) return false;
    sqlite3_stmt* query = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT size_bytes,modified_time_ms,acoustic_fingerprint,acoustic_fingerprint_version FROM samples WHERE path=?", -1, &query, nullptr) != SQLITE_OK)
        return false;
    const auto path = file.getFullPathName();
    sqlite3_bind_text(query, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const bool unchanged = sqlite3_step(query) == SQLITE_ROW
        && sqlite3_column_int64(query, 0) == sizeBytes
        && sqlite3_column_int64(query, 1) == modifiedTimeMillis
        && sqlite3_column_text(query, 2) != nullptr
        && sqlite3_column_bytes(query, 2) == 128
        && sqlite3_column_int(query, 3) == 1;
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
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "UPDATE samples SET user_category=?,category=? WHERE path=?", -1, &statement, nullptr) != SQLITE_OK) return false;
    const auto value=category.trim(); const auto path=file.getFullPathName();
    sqlite3_bind_text(statement,1,value.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,value.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,3,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    const bool ok=sqlite3_step(statement)==SQLITE_DONE && sqlite3_changes(connection)>0; sqlite3_finalize(statement); return ok;
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
    const char* sql = "SELECT path,COALESCE(NULLIF(user_category,''),category),duration,spectral_centroid FROM samples ORDER BY name COLLATE NOCASE";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return records;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* path = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        const auto* category = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (path == nullptr) continue;
        SampleBrowseRecord record;
        record.file = juce::File(juce::String::fromUTF8(path));
        record.category = category != nullptr ? juce::String::fromUTF8(category) : juce::String("Unknown");
        record.durationSeconds = sqlite3_column_double(statement, 2);
        record.brightnessHz = sqlite3_column_double(statement, 3);
        records.add(std::move(record));
    }
    sqlite3_finalize(statement);
    return records;
}

juce::Array<juce::File> SampleDatabase::searchPaths(const juce::String& query,int limit) const
{
    juce::Array<juce::File> files;if(connection==nullptr||limit<=0)return files;sqlite3_stmt* statement=nullptr;
    if(sqlite3_prepare_v2(connection,"SELECT path FROM samples WHERE instr(lower(name),lower(?))>0 OR instr(lower(path),lower(?))>0 ORDER BY name COLLATE NOCASE LIMIT ?",-1,&statement,nullptr)!=SQLITE_OK)return files;
    const auto term=query.trim();sqlite3_bind_text(statement,1,term.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(statement,2,term.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(statement,3,juce::jmin(limit,1000));
    while(sqlite3_step(statement)==SQLITE_ROW){const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(statement,0));if(value)files.add(juce::File(juce::String::fromUTF8(value)));}sqlite3_finalize(statement);return files;
}
