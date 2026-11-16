#include "SampleDatabase.h"
#include "WavScanner.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <vector>

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

struct MetadataTagOperation
{
    juce::String operationId, sampleHash, tag;
    std::int64_t sampleSize = 0;
    bool isRemoval = false;
    juce::StringArray observedAddIds;
};

bool validMetadataTagOperationId(const juce::String& value)
{
    return value.isNotEmpty() && value.length() <= 2048
        && value.containsOnly("0123456789abcdefghijklmnopqrstuvwxyz:");
}

bool parseMetadataTagOperations(const juce::var& value, std::vector<MetadataTagOperation>& operations,
                                juce::String& error)
{
    const auto* array = value.getArray();
    if (array == nullptr || array->size() > 1000000)
    {
        error = "A synchronized device archive has an invalid tag-operation list.";
        return false;
    }

    std::set<std::string> operationIds;
    operations.clear();
    operations.reserve(static_cast<size_t>(array->size()));
    for (const auto& item : *array)
    {
        if (!item.isObject())
        {
            error = "A synchronized tag operation is invalid.";
            return false;
        }
        MetadataTagOperation operation;
        const auto idValue = item.getProperty("operation_id", {});
        const auto hashValue = item.getProperty("sha256", {});
        const auto sizeValue = item.getProperty("size_bytes", {});
        const auto tagValue = item.getProperty("tag", {});
        const auto typeValue = item.getProperty("type", {});
        const auto observedValue = item.getProperty("observed_add_ids", {});
        const auto* observed = observedValue.getArray();
        if (!idValue.isString() || !hashValue.isString() || (!sizeValue.isInt() && !sizeValue.isInt64())
            || !tagValue.isString() || !typeValue.isString() || observed == nullptr || observed->size() > 100000
            || !validMetadataTagOperationId(idValue.toString()))
        {
            error = "A synchronized tag operation has invalid fields.";
            return false;
        }
        operation.operationId = idValue.toString();
        operation.sampleHash = hashValue.toString().toLowerCase();
        operation.sampleSize = static_cast<juce::int64>(sizeValue);
        operation.tag = tagValue.toString().trim();
        const auto operationType = typeValue.toString();
        operation.isRemoval = operationType == "remove";
        if (operation.sampleHash.length() != 64 || !operation.sampleHash.containsOnly("0123456789abcdef")
            || operation.sampleSize < 0 || operation.tag.isEmpty() || operation.tag.length() > 256
            || (!operation.isRemoval && operationType != "add") || (!operation.isRemoval && !observed->isEmpty())
            || !operationIds.insert(operation.operationId.toStdString()).second)
        {
            error = "A synchronized tag operation is duplicated or invalid.";
            return false;
        }
        std::set<std::string> observedIds;
        for (const auto& observedId : *observed)
        {
            if (!observedId.isString() || !validMetadataTagOperationId(observedId.toString())
                || !observedIds.insert(observedId.toString().toStdString()).second)
            {
                error = "A synchronized tag removal has invalid observed-add references.";
                return false;
            }
            operation.observedAddIds.add(observedId.toString());
        }
        operations.push_back(std::move(operation));
    }
    return true;
}

juce::var exportMetadataTagOperations(sqlite3* connection)
{
    juce::Array<juce::var> output;
    sqlite3_stmt* query = nullptr;
    const char* sql = "SELECT o.operation_id,o.sample_sha256,o.sample_size_bytes,o.tag,o.operation,r.add_operation_id FROM metadata_tag_operations o LEFT JOIN metadata_tag_observed_adds r ON r.remove_operation_id=o.operation_id ORDER BY o.operation_id,r.add_operation_id";
    if (sqlite3_prepare_v2(connection, sql, -1, &query, nullptr) != SQLITE_OK)
        return {};

    juce::var current;
    juce::String currentId;
    juce::Array<juce::var> observedIds;
    const auto finishCurrent = [&]
    {
        if (current.isObject())
        {
            current.getDynamicObject()->setProperty("observed_add_ids", juce::var(observedIds));
            output.add(current);
        }
    };
    int step = SQLITE_OK;
    while ((step = sqlite3_step(query)) == SQLITE_ROW)
    {
        const auto* idValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
        const auto* hashValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 1));
        const auto* tagValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 3));
        const auto* typeValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 4));
        if (idValue == nullptr || hashValue == nullptr || tagValue == nullptr || typeValue == nullptr)
        {
            sqlite3_finalize(query);
            return {};
        }
        const juce::String operationId = juce::String::fromUTF8(idValue);
        if (operationId != currentId)
        {
            finishCurrent();
            currentId = operationId;
            observedIds.clear();
            current = juce::var(new juce::DynamicObject());
            auto* object = current.getDynamicObject();
            object->setProperty("operation_id", operationId);
            object->setProperty("sha256", juce::String::fromUTF8(hashValue));
            object->setProperty("size_bytes", juce::var(static_cast<juce::int64>(sqlite3_column_int64(query, 2))));
            object->setProperty("tag", juce::String::fromUTF8(tagValue));
            object->setProperty("type", juce::String::fromUTF8(typeValue));
        }
        const auto* observedValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 5));
        if (observedValue != nullptr)
            observedIds.add(juce::String::fromUTF8(observedValue));
    }
    sqlite3_finalize(query);
    if (step != SQLITE_DONE)
        return {};
    finishCurrent();
    if (output.size() > 1000000)
        return {};
    return juce::var(output);
}

bool mergeMetadataTagOperations(sqlite3* connection, const std::vector<MetadataTagOperation>& operations,
                                std::set<std::pair<std::string, std::int64_t>>& touchedIdentities,
                                juce::String& error)
{
    for (const auto& operation : operations)
    {
        sqlite3_stmt* find = nullptr;
        if (sqlite3_prepare_v2(connection,
                "SELECT sample_sha256,sample_size_bytes,tag,operation FROM metadata_tag_operations WHERE operation_id=?",
                -1, &find, nullptr) != SQLITE_OK)
        { error = "Could not inspect a synchronized tag operation."; return false; }
        sqlite3_bind_text(find, 1, operation.operationId.toRawUTF8(), -1, SQLITE_TRANSIENT);
        const int findResult = sqlite3_step(find);
        const bool exists = findResult == SQLITE_ROW;
        bool sameOperation = false;
        if (exists)
        {
            const auto* hashValue = reinterpret_cast<const char*>(sqlite3_column_text(find, 0));
            const auto* tagValue = reinterpret_cast<const char*>(sqlite3_column_text(find, 2));
            const auto* typeValue = reinterpret_cast<const char*>(sqlite3_column_text(find, 3));
            sameOperation = hashValue != nullptr && tagValue != nullptr && typeValue != nullptr
                && operation.sampleHash == juce::String::fromUTF8(hashValue)
                && operation.sampleSize == sqlite3_column_int64(find, 1)
                && operation.tag == juce::String::fromUTF8(tagValue)
                && juce::String(operation.isRemoval ? "remove" : "add") == juce::String::fromUTF8(typeValue);
        }
        sqlite3_finalize(find);
        if ((!exists && findResult != SQLITE_DONE) || (exists && !sameOperation))
        { error = "A synchronized tag-operation ID conflicts with existing metadata."; return false; }

        if (!exists)
        {
            sqlite3_stmt* insert = nullptr;
            if (sqlite3_prepare_v2(connection,
                    "INSERT INTO metadata_tag_operations(operation_id,sample_sha256,sample_size_bytes,tag,operation) VALUES(?,?,?,?,?)",
                    -1, &insert, nullptr) != SQLITE_OK)
            { error = "Could not store a synchronized tag operation."; return false; }
            sqlite3_bind_text(insert, 1, operation.operationId.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insert, 2, operation.sampleHash.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(insert, 3, operation.sampleSize);
            sqlite3_bind_text(insert, 4, operation.tag.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insert, 5, operation.isRemoval ? "remove" : "add", -1, SQLITE_TRANSIENT);
            const bool inserted = sqlite3_step(insert) == SQLITE_DONE;
            sqlite3_finalize(insert);
            if (!inserted)
            { error = "Could not store a synchronized tag operation."; return false; }
        }

        if (operation.isRemoval)
        {
            std::set<std::string> existingReferences;
            sqlite3_stmt* findReferences = nullptr;
            if (sqlite3_prepare_v2(connection,
                    "SELECT add_operation_id FROM metadata_tag_observed_adds WHERE remove_operation_id=?",
                    -1, &findReferences, nullptr) != SQLITE_OK)
            { error = "Could not inspect synchronized tag-removal references."; return false; }
            sqlite3_bind_text(findReferences, 1, operation.operationId.toRawUTF8(), -1, SQLITE_TRANSIENT);
            int referenceStep = SQLITE_OK;
            while ((referenceStep = sqlite3_step(findReferences)) == SQLITE_ROW)
            {
                const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(findReferences, 0));
                if (value != nullptr) existingReferences.insert(value);
            }
            sqlite3_finalize(findReferences);
            if (referenceStep != SQLITE_DONE)
            { error = "Could not inspect synchronized tag-removal references."; return false; }
            std::set<std::string> incomingReferences;
            for (const auto& id : operation.observedAddIds) incomingReferences.insert(id.toStdString());
            if (exists && existingReferences != incomingReferences)
            { error = "A synchronized tag removal changed its observed-add set."; return false; }

            if (!exists)
            {
                sqlite3_stmt* insertReference = nullptr;
                if (sqlite3_prepare_v2(connection,
                        "INSERT INTO metadata_tag_observed_adds(remove_operation_id,add_operation_id) VALUES(?,?)",
                        -1, &insertReference, nullptr) != SQLITE_OK)
                { error = "Could not store synchronized tag-removal references."; return false; }
                for (const auto& addId : operation.observedAddIds)
                {
                    sqlite3_reset(insertReference);
                    sqlite3_clear_bindings(insertReference);
                    sqlite3_bind_text(insertReference, 1, operation.operationId.toRawUTF8(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insertReference, 2, addId.toRawUTF8(), -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(insertReference) != SQLITE_DONE)
                    { sqlite3_finalize(insertReference); error = "Could not store synchronized tag-removal references."; return false; }
                }
                sqlite3_finalize(insertReference);
            }
        }
        touchedIdentities.insert({operation.sampleHash.toStdString(), operation.sampleSize});
    }
    return true;
}

bool validateSynchronizedTagRemovalReferences(sqlite3* connection, juce::String& error)
{
    const char* sql = R"sql(
        SELECT 1
        FROM metadata_tag_observed_adds AS reference
        LEFT JOIN metadata_tag_operations AS removal
            ON removal.operation_id=reference.remove_operation_id
        LEFT JOIN metadata_tag_operations AS addition
            ON addition.operation_id=reference.add_operation_id
        WHERE removal.operation_id IS NULL OR removal.operation<>'remove'
            OR addition.operation_id IS NULL OR addition.operation<>'add'
            OR addition.sample_sha256<>removal.sample_sha256
            OR addition.sample_size_bytes<>removal.sample_size_bytes
            OR addition.tag<>removal.tag
        LIMIT 1
    )sql";
    sqlite3_stmt* query = nullptr;
    if (sqlite3_prepare_v2(connection, sql, -1, &query, nullptr) != SQLITE_OK)
    {
        error = "Could not validate synchronized tag-removal references.";
        return false;
    }
    const int step = sqlite3_step(query);
    sqlite3_finalize(query);
    if (step == SQLITE_ROW)
    {
        error = "A synchronized tag removal references a missing or unrelated add operation.";
        return false;
    }
    if (step != SQLITE_DONE)
    {
        error = "Could not validate synchronized tag-removal references.";
        return false;
    }
    return true;
}
bool rebuildSynchronizedTags(sqlite3* connection, const std::string& hash, std::int64_t size,
                             int& tagsAdded, juce::String& error)
{
    using TagPath = std::pair<std::string, std::string>;
    std::set<TagPath> before;
    sqlite3_stmt* query = nullptr;
    if (sqlite3_prepare_v2(connection,
            "SELECT s.path,t.tag FROM samples s JOIN sample_tags t ON t.path=s.path WHERE lower(s.fingerprint)=? AND s.size_bytes=?",
            -1, &query, nullptr) != SQLITE_OK)
    { error = "Could not read local tags before applying synchronized changes."; return false; }
    sqlite3_bind_text(query, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(query, 2, size);
    int step = SQLITE_OK;
    while ((step = sqlite3_step(query)) == SQLITE_ROW)
    {
        const auto* pathValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
        const auto* tagValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 1));
        if (pathValue != nullptr && tagValue != nullptr) before.insert({pathValue, tagValue});
    }
    sqlite3_finalize(query);
    if (step != SQLITE_DONE)
    { error = "Could not read local tags before applying synchronized changes."; return false; }

    sqlite3_stmt* remove = nullptr;
    if (sqlite3_prepare_v2(connection,
            "DELETE FROM sample_tags WHERE path IN (SELECT path FROM samples WHERE lower(fingerprint)=? AND size_bytes=?)",
            -1, &remove, nullptr) != SQLITE_OK)
    { error = "Could not update synchronized sample tags."; return false; }
    sqlite3_bind_text(remove, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(remove, 2, size);
    bool ok = sqlite3_step(remove) == SQLITE_DONE;
    sqlite3_finalize(remove);
    if (!ok)
    { error = "Could not update synchronized sample tags."; return false; }

    sqlite3_stmt* insert = nullptr;
    const char* insertSql = "INSERT OR IGNORE INTO sample_tags(path,tag) SELECT s.path,a.tag FROM samples s JOIN metadata_tag_operations a ON a.sample_sha256=? AND a.sample_size_bytes=? AND a.operation='add' WHERE lower(s.fingerprint)=? AND s.size_bytes=? AND NOT EXISTS(SELECT 1 FROM metadata_tag_observed_adds r WHERE r.add_operation_id=a.operation_id)";
    if (sqlite3_prepare_v2(connection, insertSql, -1, &insert, nullptr) != SQLITE_OK)
    { error = "Could not apply synchronized sample tags."; return false; }
    sqlite3_bind_text(insert, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(insert, 2, size);
    sqlite3_bind_text(insert, 3, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(insert, 4, size);
    ok = sqlite3_step(insert) == SQLITE_DONE;
    sqlite3_finalize(insert);
    if (!ok)
    { error = "Could not apply synchronized sample tags."; return false; }

    if (sqlite3_prepare_v2(connection,
            "SELECT s.path,t.tag FROM samples s JOIN sample_tags t ON t.path=s.path WHERE lower(s.fingerprint)=? AND s.size_bytes=?",
            -1, &query, nullptr) != SQLITE_OK)
    { error = "Could not verify synchronized sample tags."; return false; }
    sqlite3_bind_text(query, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(query, 2, size);
    std::set<TagPath> after;
    step = SQLITE_OK;
    while ((step = sqlite3_step(query)) == SQLITE_ROW)
    {
        const auto* pathValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
        const auto* tagValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 1));
        if (pathValue != nullptr && tagValue != nullptr) after.insert({pathValue, tagValue});
    }
    sqlite3_finalize(query);
    if (step != SQLITE_DONE)
    { error = "Could not verify synchronized sample tags."; return false; }
    for (const auto& tag : after)
        if (before.find(tag) == before.end()) ++tagsAdded;
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
    const char* schema = "CREATE TABLE IF NOT EXISTS samples (path TEXT PRIMARY KEY,name TEXT NOT NULL,duration REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,format TEXT NOT NULL DEFAULT '',bit_depth INTEGER NOT NULL DEFAULT 0,peak REAL NOT NULL DEFAULT 0,rms REAL NOT NULL DEFAULT 0,spectral_centroid REAL NOT NULL DEFAULT 0,spectral_flatness REAL NOT NULL DEFAULT 0,transient_strength REAL NOT NULL DEFAULT 0,transient_count INTEGER NOT NULL DEFAULT 0,fundamental_hz REAL NOT NULL DEFAULT 0,pitch_confidence REAL NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT 'Unknown',classification_confidence REAL NOT NULL DEFAULT 0,user_category TEXT NOT NULL DEFAULT '',size_bytes INTEGER NOT NULL DEFAULT 0,modified_time_ms INTEGER NOT NULL DEFAULT 0,acoustic_fingerprint TEXT NOT NULL DEFAULT '',acoustic_fingerprint_version INTEGER NOT NULL DEFAULT 0,analysis_version INTEGER NOT NULL DEFAULT 0)";
    if (sqlite3_exec(connection, schema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    // Upgrade databases created by earlier MagDrum builds without losing indexed paths.
    const char* migrations[] = {
        "ALTER TABLE samples ADD COLUMN format TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE samples ADD COLUMN bit_depth INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN peak REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN rms REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN spectral_centroid REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN spectral_flatness REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN transient_strength REAL NOT NULL DEFAULT 0",
        "ALTER TABLE samples ADD COLUMN transient_count INTEGER NOT NULL DEFAULT 0",
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
    {
        char* errorMessage = nullptr;
        const int result = sqlite3_exec(connection, migration, nullptr, nullptr, &errorMessage);
        if (result != SQLITE_OK)
        {
            const auto message = errorMessage != nullptr
                ? juce::String::fromUTF8(errorMessage)
                : juce::String::fromUTF8(sqlite3_errmsg(connection));
            sqlite3_free(errorMessage);

            // The migrations are idempotent: a duplicate column means the
            // database already passed this additive change. Other SQL or I/O
            // errors must fail open instead of leaving a partially upgraded DB.
            if (result != SQLITE_ERROR || !message.startsWith("duplicate column name:"))
                return false;
        }
    }
    if (sqlite3_exec(connection, "CREATE INDEX IF NOT EXISTS samples_fingerprint ON samples(fingerprint); CREATE INDEX IF NOT EXISTS samples_duration ON samples(duration); CREATE INDEX IF NOT EXISTS samples_name_path ON samples(name COLLATE NOCASE,path COLLATE NOCASE); CREATE INDEX IF NOT EXISTS samples_duration_name_path ON samples(duration,name COLLATE NOCASE,path COLLATE NOCASE); CREATE INDEX IF NOT EXISTS samples_centroid_name_path ON samples(spectral_centroid,name COLLATE NOCASE,path COLLATE NOCASE)", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    int previousSchemaVersion = 0;
    sqlite3_stmt* versionStatement = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT version FROM schema_info WHERE singleton=1", -1, &versionStatement, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(versionStatement) == SQLITE_ROW)
            previousSchemaVersion = sqlite3_column_int(versionStatement, 0);
        sqlite3_finalize(versionStatement);
    }
    const char* pluginSchema = "CREATE TABLE IF NOT EXISTS schema_info(singleton INTEGER PRIMARY KEY CHECK(singleton=1),version INTEGER NOT NULL); INSERT INTO schema_info(singleton,version) VALUES(1,10) ON CONFLICT(singleton) DO UPDATE SET version=max(version,excluded.version); CREATE TABLE IF NOT EXISTS plugin_instruments(id INTEGER PRIMARY KEY,identifier TEXT NOT NULL UNIQUE,format TEXT NOT NULL,name TEXT NOT NULL,manufacturer TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT '',file TEXT NOT NULL DEFAULT '',version TEXT NOT NULL DEFAULT '',last_scanned TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP); CREATE INDEX IF NOT EXISTS plugin_instruments_name ON plugin_instruments(name COLLATE NOCASE); CREATE TABLE IF NOT EXISTS plugin_kits(id INTEGER PRIMARY KEY,plugin_id INTEGER NOT NULL,name TEXT NOT NULL,plugin_state BLOB,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,UNIQUE(plugin_id,name),FOREIGN KEY(plugin_id) REFERENCES plugin_instruments(id) ON DELETE CASCADE); CREATE TABLE IF NOT EXISTS plugin_sounds(id INTEGER PRIMARY KEY,kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL CHECK(midi_note BETWEEN 0 AND 127),velocity INTEGER NOT NULL CHECK(velocity BETWEEN 1 AND 127),articulation TEXT NOT NULL DEFAULT '',silent INTEGER NOT NULL DEFAULT 0,preview_path TEXT NOT NULL DEFAULT '',duration REAL NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,peak REAL NOT NULL DEFAULT 0,rms REAL NOT NULL DEFAULT 0,spectral_centroid REAL NOT NULL DEFAULT 0,spectral_flatness REAL NOT NULL DEFAULT 0,transient_strength REAL NOT NULL DEFAULT 0,transient_count INTEGER NOT NULL DEFAULT 0,fundamental_hz REAL NOT NULL DEFAULT 0,pitch_confidence REAL NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT 'Unknown',classification_confidence REAL NOT NULL DEFAULT 0,acoustic_fingerprint TEXT NOT NULL DEFAULT '',UNIQUE(kit_id,midi_note,velocity,articulation),FOREIGN KEY(kit_id) REFERENCES plugin_kits(id) ON DELETE CASCADE); CREATE INDEX IF NOT EXISTS plugin_sounds_note ON plugin_sounds(midi_note,velocity); CREATE TABLE IF NOT EXISTS plugin_velocity_layer_candidates(kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL CHECK(midi_note BETWEEN 0 AND 127),lower_velocity INTEGER NOT NULL CHECK(lower_velocity BETWEEN 1 AND 127),upper_velocity INTEGER NOT NULL CHECK(upper_velocity BETWEEN 1 AND 127),spectral_distance REAL NOT NULL,algorithm_version INTEGER NOT NULL,PRIMARY KEY(kit_id,midi_note,lower_velocity,upper_velocity),FOREIGN KEY(kit_id) REFERENCES plugin_kits(id) ON DELETE CASCADE)";
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
    const char* classificationTagSchema = "UPDATE schema_info SET version=max(version,12) WHERE singleton=1; CREATE TABLE IF NOT EXISTS sample_classification_tags(path TEXT NOT NULL,tag TEXT NOT NULL,PRIMARY KEY(path,tag),FOREIGN KEY(path) REFERENCES samples(path) ON DELETE CASCADE); CREATE INDEX IF NOT EXISTS sample_classification_tags_tag ON sample_classification_tags(tag)";
    if (sqlite3_exec(connection, classificationTagSchema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    if (sqlite3_exec(connection, "UPDATE schema_info SET version=max(version,16) WHERE singleton=1", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN fundamental_hz REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN pitch_confidence REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN spectral_flatness REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN transient_count INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(connection, "ALTER TABLE plugin_sounds ADD COLUMN acoustic_fingerprint TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
    if (sqlite3_exec(connection, "CREATE TABLE IF NOT EXISTS sample_tags(path TEXT NOT NULL,tag TEXT NOT NULL,PRIMARY KEY(path,tag),FOREIGN KEY(path) REFERENCES samples(path) ON DELETE CASCADE); CREATE INDEX IF NOT EXISTS sample_tags_tag ON sample_tags(tag)", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    const char* metadataTagOperationSchema = R"sql(
        CREATE TABLE IF NOT EXISTS metadata_tag_operations(
            operation_id TEXT PRIMARY KEY,
            sample_sha256 TEXT NOT NULL,
            sample_size_bytes INTEGER NOT NULL CHECK(sample_size_bytes>=0),
            tag TEXT NOT NULL,
            operation TEXT NOT NULL CHECK(operation IN ('add','remove'))
        );
        CREATE INDEX IF NOT EXISTS metadata_tag_operations_sample
            ON metadata_tag_operations(sample_sha256,sample_size_bytes,tag,operation);
        CREATE TABLE IF NOT EXISTS metadata_tag_observed_adds(
            remove_operation_id TEXT NOT NULL,
            add_operation_id TEXT NOT NULL,
            PRIMARY KEY(remove_operation_id,add_operation_id)
        );
        UPDATE schema_info SET version=max(version,19) WHERE singleton=1;
        INSERT OR IGNORE INTO metadata_tag_operations(operation_id,sample_sha256,sample_size_bytes,tag,operation)
        SELECT 'legacy:' || lower(s.fingerprint) || ':' || s.size_bytes || ':' || lower(hex(t.tag)),
               lower(s.fingerprint),s.size_bytes,t.tag,'add'
        FROM samples s JOIN sample_tags t ON t.path=s.path
        WHERE length(s.fingerprint)=64 AND lower(s.fingerprint) NOT GLOB '*[^0-9a-f]*' AND s.size_bytes>=0;
    )sql";
    if (sqlite3_exec(connection, metadataTagOperationSchema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    if (sqlite3_exec(connection,"CREATE TABLE IF NOT EXISTS mapping_profiles(name TEXT PRIMARY KEY); CREATE TABLE IF NOT EXISTS mapping_entries(profile_name TEXT NOT NULL,instrument INTEGER NOT NULL,articulation INTEGER NOT NULL,note INTEGER NOT NULL CHECK(note BETWEEN 0 AND 127),PRIMARY KEY(profile_name,instrument,articulation),FOREIGN KEY(profile_name) REFERENCES mapping_profiles(name) ON DELETE CASCADE); CREATE TABLE IF NOT EXISTS sample_scan_history(id INTEGER PRIMARY KEY,root_path TEXT NOT NULL,started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,finished_at TEXT,status TEXT NOT NULL DEFAULT 'running',files_considered INTEGER NOT NULL DEFAULT 0,files_skipped INTEGER NOT NULL DEFAULT 0,files_indexed INTEGER NOT NULL DEFAULT 0,errors INTEGER NOT NULL DEFAULT 0,last_completed_path TEXT NOT NULL DEFAULT '')",nullptr,nullptr,nullptr)!=SQLITE_OK)
        return false;
    sqlite3_exec(connection, "ALTER TABLE sample_scan_history ADD COLUMN last_completed_path TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr); // duplicate column means already upgraded
    const char* searchSchema = R"sql(
        CREATE VIRTUAL TABLE IF NOT EXISTS sample_search_fts USING fts5(name,path,metadata,tokenize='trigram');
        CREATE VIEW IF NOT EXISTS sample_search_values AS
        SELECT s.rowid AS sample_rowid,s.name,s.path,
            trim(
                coalesce((SELECT group_concat(tag,' ') FROM sample_tags WHERE path=s.path),'') || ' ' ||
                coalesce((SELECT group_concat(tag,' ') FROM sample_classification_tags WHERE path=s.path),'') || ' ' ||
                coalesce(src.display_name,'') || ' ' || coalesce(src.identifier,'') || ' ' ||
                coalesce((SELECT group_concat(p.name || ' ' || k.name,' ')
                    FROM plugin_sounds ps JOIN plugin_kits k ON k.id=ps.kit_id
                    JOIN plugin_instruments p ON p.id=k.plugin_id WHERE ps.preview_path=s.path),'') || ' ' ||
                coalesce((SELECT group_concat(p.name || ' ' || k.name,' ')
                    FROM plugin_round_robin_takes rr JOIN plugin_kits k ON k.id=rr.kit_id
                    JOIN plugin_instruments p ON p.id=k.plugin_id WHERE rr.preview_path=s.path),'')
            ) AS metadata
        FROM samples s LEFT JOIN sample_sources src ON src.id=s.source_id;
        CREATE TRIGGER IF NOT EXISTS samples_search_insert AFTER INSERT ON samples BEGIN
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=NEW.rowid;
        END;
        CREATE TRIGGER IF NOT EXISTS samples_search_delete AFTER DELETE ON samples BEGIN
            DELETE FROM sample_search_fts WHERE rowid=OLD.rowid;
        END;
        CREATE TRIGGER IF NOT EXISTS samples_search_update AFTER UPDATE OF name,path,source_id ON samples BEGIN
            DELETE FROM sample_search_fts WHERE rowid=OLD.rowid;
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=NEW.rowid;
        END;
        CREATE TRIGGER IF NOT EXISTS sample_tags_search_insert AFTER INSERT ON sample_tags BEGIN
            DELETE FROM sample_search_fts WHERE rowid=(SELECT rowid FROM samples WHERE path=NEW.path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=(SELECT rowid FROM samples WHERE path=NEW.path);
        END;
        CREATE TRIGGER IF NOT EXISTS sample_tags_search_delete AFTER DELETE ON sample_tags BEGIN
            DELETE FROM sample_search_fts WHERE rowid=(SELECT rowid FROM samples WHERE path=OLD.path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=(SELECT rowid FROM samples WHERE path=OLD.path);
        END;
        CREATE TRIGGER IF NOT EXISTS sample_classification_tags_search_insert AFTER INSERT ON sample_classification_tags BEGIN
            DELETE FROM sample_search_fts WHERE rowid=(SELECT rowid FROM samples WHERE path=NEW.path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=(SELECT rowid FROM samples WHERE path=NEW.path);
        END;
        CREATE TRIGGER IF NOT EXISTS sample_classification_tags_search_delete AFTER DELETE ON sample_classification_tags BEGIN
            DELETE FROM sample_search_fts WHERE rowid=(SELECT rowid FROM samples WHERE path=OLD.path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=(SELECT rowid FROM samples WHERE path=OLD.path);
        END;
        CREATE TRIGGER IF NOT EXISTS sample_sources_search_update AFTER UPDATE OF display_name,identifier ON sample_sources BEGIN
            DELETE FROM sample_search_fts WHERE rowid IN (SELECT rowid FROM samples WHERE source_id=OLD.id OR source_id=NEW.id);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values
            WHERE sample_rowid IN (SELECT rowid FROM samples WHERE source_id=OLD.id OR source_id=NEW.id);
        END;
        CREATE TRIGGER IF NOT EXISTS plugin_sounds_search_insert AFTER INSERT ON plugin_sounds WHEN NEW.preview_path<>'' BEGIN
            DELETE FROM sample_search_fts WHERE rowid=(SELECT rowid FROM samples WHERE path=NEW.preview_path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=(SELECT rowid FROM samples WHERE path=NEW.preview_path);
        END;
        CREATE TRIGGER IF NOT EXISTS plugin_sounds_search_delete AFTER DELETE ON plugin_sounds WHEN OLD.preview_path<>'' BEGIN
            DELETE FROM sample_search_fts WHERE rowid=(SELECT rowid FROM samples WHERE path=OLD.preview_path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=(SELECT rowid FROM samples WHERE path=OLD.preview_path);
        END;
        CREATE TRIGGER IF NOT EXISTS plugin_sounds_search_update AFTER UPDATE OF preview_path,kit_id ON plugin_sounds BEGIN
            DELETE FROM sample_search_fts WHERE rowid IN (SELECT rowid FROM samples WHERE path=OLD.preview_path OR path=NEW.preview_path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values
            WHERE sample_rowid IN (SELECT rowid FROM samples WHERE path=OLD.preview_path OR path=NEW.preview_path);
        END;
        CREATE TRIGGER IF NOT EXISTS plugin_round_robin_search_insert AFTER INSERT ON plugin_round_robin_takes WHEN NEW.preview_path<>'' BEGIN
            DELETE FROM sample_search_fts WHERE rowid=(SELECT rowid FROM samples WHERE path=NEW.preview_path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=(SELECT rowid FROM samples WHERE path=NEW.preview_path);
        END;
        CREATE TRIGGER IF NOT EXISTS plugin_round_robin_search_delete AFTER DELETE ON plugin_round_robin_takes WHEN OLD.preview_path<>'' BEGIN
            DELETE FROM sample_search_fts WHERE rowid=(SELECT rowid FROM samples WHERE path=OLD.preview_path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid=(SELECT rowid FROM samples WHERE path=OLD.preview_path);
        END;
        CREATE TRIGGER IF NOT EXISTS plugin_round_robin_search_update AFTER UPDATE OF preview_path,kit_id ON plugin_round_robin_takes BEGIN
            DELETE FROM sample_search_fts WHERE rowid IN (SELECT rowid FROM samples WHERE path=OLD.preview_path OR path=NEW.preview_path);
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values
            WHERE sample_rowid IN (SELECT rowid FROM samples WHERE path=OLD.preview_path OR path=NEW.preview_path);
        END;
        CREATE TRIGGER IF NOT EXISTS plugin_kits_search_update AFTER UPDATE OF name,plugin_id ON plugin_kits BEGIN
            DELETE FROM sample_search_fts WHERE rowid IN (
                SELECT s.rowid FROM samples s JOIN plugin_sounds ps ON ps.preview_path=s.path WHERE ps.kit_id=NEW.id
                UNION SELECT s.rowid FROM samples s JOIN plugin_round_robin_takes rr ON rr.preview_path=s.path WHERE rr.kit_id=NEW.id
            );
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid IN (
                SELECT s.rowid FROM samples s JOIN plugin_sounds ps ON ps.preview_path=s.path WHERE ps.kit_id=NEW.id
                UNION SELECT s.rowid FROM samples s JOIN plugin_round_robin_takes rr ON rr.preview_path=s.path WHERE rr.kit_id=NEW.id
            );
        END;
        CREATE TRIGGER IF NOT EXISTS plugin_instruments_search_update AFTER UPDATE OF name ON plugin_instruments BEGIN
            DELETE FROM sample_search_fts WHERE rowid IN (
                SELECT s.rowid FROM samples s JOIN plugin_sounds ps ON ps.preview_path=s.path
                JOIN plugin_kits k ON k.id=ps.kit_id WHERE k.plugin_id=NEW.id
                UNION SELECT s.rowid FROM samples s JOIN plugin_round_robin_takes rr ON rr.preview_path=s.path
                JOIN plugin_kits k ON k.id=rr.kit_id WHERE k.plugin_id=NEW.id
            );
            INSERT OR REPLACE INTO sample_search_fts(rowid,name,path,metadata)
            SELECT sample_rowid,name,path,metadata FROM sample_search_values WHERE sample_rowid IN (
                SELECT s.rowid FROM samples s JOIN plugin_sounds ps ON ps.preview_path=s.path
                JOIN plugin_kits k ON k.id=ps.kit_id WHERE k.plugin_id=NEW.id
                UNION SELECT s.rowid FROM samples s JOIN plugin_round_robin_takes rr ON rr.preview_path=s.path
                JOIN plugin_kits k ON k.id=rr.kit_id WHERE k.plugin_id=NEW.id
            );
        END;
    )sql";
    if (sqlite3_exec(connection, searchSchema, nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    if (previousSchemaVersion < 17)
    {
        const char* rebuildSearch = "BEGIN IMMEDIATE; DELETE FROM sample_search_fts; INSERT INTO sample_search_fts(rowid,name,path,metadata) SELECT sample_rowid,name,path,metadata FROM sample_search_values; UPDATE schema_info SET version=max(version,17) WHERE singleton=1; COMMIT";
        if (sqlite3_exec(connection, rebuildSearch, nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    }
    if (sqlite3_exec(connection,
                     "CREATE TABLE IF NOT EXISTS metadata_sync_state(key TEXT PRIMARY KEY,value TEXT NOT NULL); UPDATE schema_info SET version=max(version,18) WHERE singleton=1",
                     nullptr, nullptr, nullptr) != SQLITE_OK)
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

juce::StringArray SampleDatabase::getPluginKitNames() const
{
    juce::StringArray result;
    if (connection == nullptr) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT DISTINCT p.name || ' / ' || k.name FROM plugin_kits k "
                      "JOIN plugin_instruments p ON p.id=k.plugin_id ORDER BY p.name COLLATE NOCASE,k.name COLLATE NOCASE";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        if (value != nullptr) result.add(juce::String::fromUTF8(value));
    }
    sqlite3_finalize(statement);
    return result;
}

juce::StringArray SampleDatabase::getPluginKitNames(const juce::String& pluginIdentifier) const
{
    juce::StringArray result;
    if (connection == nullptr || pluginIdentifier.trim().isEmpty()) return result;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT k.name FROM plugin_kits k JOIN plugin_instruments p ON p.id=k.plugin_id "
                      "WHERE p.identifier=? ORDER BY k.name COLLATE NOCASE";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return result;
    const auto identifier = pluginIdentifier.trim();
    sqlite3_bind_text(statement, 1, identifier.toRawUTF8(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        if (value != nullptr) result.add(juce::String::fromUTF8(value));
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
    if (connection == nullptr || kitId <= 0 || sound.midiNote < 0 || sound.midiNote > 127 || sound.velocity < 1 || sound.velocity > 127 || sound.duration < 0.0 || sound.sampleRate < 0.0 || sound.channels < 0 || sound.transientCount < 0 || sound.transientCount > 65535 || sound.peak < 0.0 || sound.rms < 0.0 || !std::isfinite(sound.spectralFlatness) || sound.spectralFlatness < 0.0 || sound.spectralFlatness > 1.0 || sound.fundamentalHz < 0.0 || sound.pitchConfidence < 0.0 || sound.pitchConfidence > 1.0 || sound.confidence < 0.0 || sound.confidence > 1.0) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO plugin_sounds(kit_id,midi_note,velocity,articulation,silent,preview_path,duration,sample_rate,channels,peak,rms,spectral_centroid,spectral_flatness,transient_strength,transient_count,fundamental_hz,pitch_confidence,fingerprint,category,classification_confidence,acoustic_fingerprint) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(kit_id,midi_note,velocity,articulation) DO UPDATE SET silent=excluded.silent,preview_path=excluded.preview_path,duration=excluded.duration,sample_rate=excluded.sample_rate,channels=excluded.channels,peak=excluded.peak,rms=excluded.rms,spectral_centroid=excluded.spectral_centroid,spectral_flatness=excluded.spectral_flatness,transient_strength=excluded.transient_strength,transient_count=excluded.transient_count,fundamental_hz=excluded.fundamental_hz,pitch_confidence=excluded.pitch_confidence,fingerprint=excluded.fingerprint,category=excluded.category,classification_confidence=excluded.classification_confidence,acoustic_fingerprint=excluded.acoustic_fingerprint";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(statement, 1, kitId); sqlite3_bind_int(statement, 2, sound.midiNote); sqlite3_bind_int(statement, 3, sound.velocity);
    sqlite3_bind_text(statement, 4, sound.articulation.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_int(statement, 5, sound.silent ? 1 : 0);
    sqlite3_bind_text(statement, 6, sound.previewPath.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_double(statement, 7, sound.duration);
    sqlite3_bind_double(statement, 8, sound.sampleRate); sqlite3_bind_int(statement, 9, sound.channels); sqlite3_bind_double(statement, 10, sound.peak);
    sqlite3_bind_double(statement, 11, sound.rms); sqlite3_bind_double(statement, 12, sound.spectralCentroid); sqlite3_bind_double(statement, 13, sound.spectralFlatness); sqlite3_bind_double(statement, 14, sound.transientStrength); sqlite3_bind_int(statement, 15, sound.transientCount);
    sqlite3_bind_double(statement, 16, sound.fundamentalHz); sqlite3_bind_double(statement, 17, sound.pitchConfidence);
    sqlite3_bind_text(statement, 18, sound.fingerprint.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(statement, 19, sound.category.toRawUTF8(), -1, SQLITE_TRANSIENT); sqlite3_bind_double(statement, 20, sound.confidence); sqlite3_bind_text(statement, 21, sound.acousticFingerprint.toRawUTF8(), -1, SQLITE_TRANSIENT);
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
    if (connection == nullptr || sample.transientCount < 0 || sample.transientCount > 65535) return false;
    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO samples(path,name,duration,channels,sample_rate,format,bit_depth,peak,rms,spectral_centroid,spectral_flatness,transient_strength,transient_count,fundamental_hz,pitch_confidence,fingerprint,category,classification_confidence,size_bytes,modified_time_ms,acoustic_fingerprint,acoustic_fingerprint_version,analysis_version,source_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET name=excluded.name,duration=excluded.duration,channels=excluded.channels,sample_rate=excluded.sample_rate,format=excluded.format,bit_depth=excluded.bit_depth,peak=excluded.peak,rms=excluded.rms,spectral_centroid=excluded.spectral_centroid,spectral_flatness=excluded.spectral_flatness,transient_strength=excluded.transient_strength,transient_count=excluded.transient_count,fundamental_hz=excluded.fundamental_hz,pitch_confidence=excluded.pitch_confidence,fingerprint=excluded.fingerprint,category=CASE WHEN samples.user_category='' THEN excluded.category ELSE samples.user_category END,classification_confidence=excluded.classification_confidence,size_bytes=excluded.size_bytes,modified_time_ms=excluded.modified_time_ms,acoustic_fingerprint=excluded.acoustic_fingerprint,acoustic_fingerprint_version=excluded.acoustic_fingerprint_version,analysis_version=excluded.analysis_version,source_id=COALESCE(excluded.source_id,samples.source_id)";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    const auto path = sample.file.getFullPathName(); const auto name = sample.file.getFileName();
    sqlite3_bind_text(statement,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,name.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_double(statement,3,sample.durationSeconds); sqlite3_bind_int(statement,4,sample.channels); sqlite3_bind_double(statement,5,sample.sampleRate);
    sqlite3_bind_text(statement,6,sample.format.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(statement,7,sample.bitDepth);
    sqlite3_bind_double(statement,8,sample.peak); sqlite3_bind_double(statement,9,sample.rms); sqlite3_bind_double(statement,10,sample.spectralCentroidHz);
    sqlite3_bind_double(statement,11,sample.spectralFlatness); sqlite3_bind_double(statement,12,sample.transientStrength); sqlite3_bind_int(statement,13,sample.transientCount);
    sqlite3_bind_double(statement,14,sample.fundamentalHz); sqlite3_bind_double(statement,15,sample.pitchConfidence);
    sqlite3_bind_text(statement,16,sample.fingerprint.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(statement,17,sample.category.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_double(statement,18,sample.classificationConfidence);
    sqlite3_bind_int64(statement,19,sample.fileSizeBytes); sqlite3_bind_int64(statement,20,sample.modifiedTimeMillis);
    sqlite3_bind_text(statement,21,sample.acousticFingerprint.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(statement,22,sample.acousticFingerprintVersion);
    sqlite3_bind_int(statement,23,sample.analysisVersion);
    if (sourceId > 0) sqlite3_bind_int64(statement,24,sourceId); else sqlite3_bind_null(statement,24);
    bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (ok)
    {
        sqlite3_stmt* removeTags = nullptr;
        ok = sqlite3_prepare_v2(connection, "DELETE FROM sample_classification_tags WHERE path=?", -1, &removeTags, nullptr) == SQLITE_OK;
        if (ok)
        {
            sqlite3_bind_text(removeTags, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(removeTags) == SQLITE_DONE;
        }
        sqlite3_finalize(removeTags);
    }
    if (ok && !sample.classificationTags.isEmpty())
    {
        sqlite3_stmt* addTag = nullptr;
        ok = sqlite3_prepare_v2(connection, "INSERT OR IGNORE INTO sample_classification_tags(path,tag) VALUES(?,?)", -1, &addTag, nullptr) == SQLITE_OK;
        if (ok)
        {
            for (const auto& rawTag : sample.classificationTags)
            {
                const auto tag = rawTag.trim();
                if (tag.isEmpty()) continue;
                sqlite3_reset(addTag);
                sqlite3_clear_bindings(addTag);
                sqlite3_bind_text(addTag, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addTag, 2, tag.toRawUTF8(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(addTag) != SQLITE_DONE) { ok = false; break; }
            }
        }
        sqlite3_finalize(addTag);
    }
    const bool committed = sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (ok && !committed) sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && committed;
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

std::optional<std::int64_t> SampleDatabase::beginSampleScan(const juce::File& root, const juce::String& resumeAfterPath)
{
    if (connection == nullptr || !root.isDirectory()) return std::nullopt;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "INSERT INTO sample_scan_history(root_path,last_completed_path) VALUES(?,?)", -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    const auto path = root.getFullPathName();
    sqlite3_bind_text(statement, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, resumeAfterPath.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok) return std::nullopt;
    return sqlite3_last_insert_rowid(connection);
}

juce::String SampleDatabase::getResumableSampleScanPath(const juce::File& root) const
{
    if (connection == nullptr || !root.isDirectory()) return {};
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT last_completed_path FROM sample_scan_history WHERE root_path=? AND status IN ('interrupted','cancelled') ORDER BY id DESC LIMIT 1", -1, &statement, nullptr) != SQLITE_OK)
        return {};
    const auto rootPath = root.getFullPathName();
    sqlite3_bind_text(statement, 1, rootPath.toRawUTF8(), -1, SQLITE_TRANSIENT);
    juce::String result;
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        if (value != nullptr) result = juce::String::fromUTF8(value);
    }
    sqlite3_finalize(statement);
    return result;
}

bool SampleDatabase::updateSampleScanCheckpoint(std::int64_t scanId, const juce::File& completedFile)
{
    if (connection == nullptr || scanId <= 0) return false;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, "UPDATE sample_scan_history SET last_completed_path=? WHERE id=? AND status='running'", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    const auto path = completedFile.getFullPathName();
    sqlite3_bind_text(statement, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, scanId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(connection) == 1;
    sqlite3_finalize(statement);
    return ok;
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
    if (sqlite3_prepare_v2(connection, "SELECT id,root_path,started_at,coalesce(finished_at,''),status,files_considered,files_skipped,files_indexed,errors,last_completed_path FROM sample_scan_history ORDER BY id DESC LIMIT ?", -1, &statement, nullptr) != SQLITE_OK)
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
                    sqlite3_column_int64(statement, 7), sqlite3_column_int64(statement, 8), text(9)});
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

std::vector<ScannedSample> SampleDatabase::getManualClassificationExamples(int limit) const
{
    std::vector<ScannedSample> examples;
    if (connection == nullptr || limit <= 0)
        return examples;

    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT path,user_category,duration,channels,sample_rate,format,bit_depth,peak,rms,"
                      "spectral_centroid,spectral_flatness,transient_strength,transient_count,fundamental_hz,"
                      "pitch_confidence,analysis_version,size_bytes,modified_time_ms,fingerprint,"
                      "acoustic_fingerprint,acoustic_fingerprint_version FROM samples "
                      "WHERE user_category<>'' AND user_category<>'Unknown' "
                      "ORDER BY user_category COLLATE NOCASE,path COLLATE NOCASE LIMIT ?";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK)
        return examples;
    sqlite3_bind_int(statement, 1, juce::jlimit(1, 10000, limit));

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const auto text = [statement](int column)
        {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
            return value != nullptr ? juce::String::fromUTF8(value) : juce::String();
        };
        ScannedSample example;
        example.file = juce::File(text(0));
        example.category = text(1);
        example.durationSeconds = sqlite3_column_double(statement, 2);
        example.channels = sqlite3_column_int(statement, 3);
        example.sampleRate = sqlite3_column_double(statement, 4);
        example.format = text(5);
        example.bitDepth = sqlite3_column_int(statement, 6);
        example.peak = sqlite3_column_double(statement, 7);
        example.rms = sqlite3_column_double(statement, 8);
        example.spectralCentroidHz = sqlite3_column_double(statement, 9);
        example.spectralFlatness = sqlite3_column_double(statement, 10);
        example.transientStrength = sqlite3_column_double(statement, 11);
        example.transientCount = sqlite3_column_int(statement, 12);
        example.fundamentalHz = sqlite3_column_double(statement, 13);
        example.pitchConfidence = sqlite3_column_double(statement, 14);
        example.analysisVersion = sqlite3_column_int(statement, 15);
        example.fileSizeBytes = sqlite3_column_int64(statement, 16);
        example.modifiedTimeMillis = sqlite3_column_int64(statement, 17);
        example.fingerprint = text(18);
        example.acousticFingerprint = text(19);
        example.acousticFingerprintVersion = sqlite3_column_int(statement, 20);
        examples.push_back(std::move(example));
    }
    sqlite3_finalize(statement);
    return examples;
}

bool SampleDatabase::setUserTags(const juce::File& file, const juce::StringArray& tags)
{
    if (connection == nullptr || sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;

    const auto path = file.getFullPathName();
    juce::StringArray currentTags, requestedTags;
    sqlite3_stmt* current = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT tag FROM sample_tags WHERE path=? ORDER BY tag", -1, &current, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(current, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    int currentStep = SQLITE_OK;
    while ((currentStep = sqlite3_step(current)) == SQLITE_ROW)
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(current, 0));
        if (value != nullptr) currentTags.add(juce::String::fromUTF8(value));
    }
    sqlite3_finalize(current);
    if (currentStep != SQLITE_DONE)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    for (const auto& raw : tags)
    {
        const auto tag = raw.trim();
        if (tag.isNotEmpty()) requestedTags.addIfNotAlreadyThere(tag);
    }

    juce::String sampleHash;
    std::int64_t sampleSize = 0;
    sqlite3_stmt* identity = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT fingerprint,size_bytes FROM samples WHERE path=?", -1, &identity, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(identity, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    const int identityStep = sqlite3_step(identity);
    if (identityStep == SQLITE_ROW)
    {
        const auto* hashValue = reinterpret_cast<const char*>(sqlite3_column_text(identity, 0));
        if (hashValue != nullptr) sampleHash = juce::String::fromUTF8(hashValue).toLowerCase();
        sampleSize = sqlite3_column_int64(identity, 1);
    }
    sqlite3_finalize(identity);
    if (identityStep != SQLITE_ROW && identityStep != SQLITE_DONE)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    const bool hasPortableIdentity = sampleHash.length() == 64 && sampleHash.containsOnly("0123456789abcdef") && sampleSize >= 0;

    const auto recordAdd = [&](const juce::String& tag)
    {
        sqlite3_stmt* operation = nullptr;
        if (sqlite3_prepare_v2(connection,
                "INSERT INTO metadata_tag_operations(operation_id,sample_sha256,sample_size_bytes,tag,operation) VALUES(?,?,?,?,'add')",
                -1, &operation, nullptr) != SQLITE_OK)
            return false;
        const auto operationId = juce::Uuid().toString().removeCharacters("{}-").toLowerCase();
        sqlite3_bind_text(operation, 1, operationId.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(operation, 2, sampleHash.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(operation, 3, sampleSize);
        sqlite3_bind_text(operation, 4, tag.toRawUTF8(), -1, SQLITE_TRANSIENT);
        const bool ok = sqlite3_step(operation) == SQLITE_DONE;
        sqlite3_finalize(operation);
        return ok;
    };
    const auto recordRemoval = [&](const juce::String& tag)
    {
        std::vector<juce::String> observedAdds;
        sqlite3_stmt* findAdds = nullptr;
        if (sqlite3_prepare_v2(connection,
                "SELECT a.operation_id FROM metadata_tag_operations a WHERE a.sample_sha256=? AND a.sample_size_bytes=? AND a.tag=? AND a.operation='add' AND NOT EXISTS(SELECT 1 FROM metadata_tag_observed_adds r WHERE r.add_operation_id=a.operation_id)",
                -1, &findAdds, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(findAdds, 1, sampleHash.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(findAdds, 2, sampleSize);
        sqlite3_bind_text(findAdds, 3, tag.toRawUTF8(), -1, SQLITE_TRANSIENT);
        int step = SQLITE_OK;
        while ((step = sqlite3_step(findAdds)) == SQLITE_ROW)
        {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(findAdds, 0));
            if (value != nullptr) observedAdds.emplace_back(juce::String::fromUTF8(value));
        }
        sqlite3_finalize(findAdds);
        if (step != SQLITE_DONE) return false;

        const auto removalId = juce::Uuid().toString().removeCharacters("{}-").toLowerCase();
        sqlite3_stmt* insertRemoval = nullptr;
        if (sqlite3_prepare_v2(connection,
                "INSERT INTO metadata_tag_operations(operation_id,sample_sha256,sample_size_bytes,tag,operation) VALUES(?,?,?,?,'remove')",
                -1, &insertRemoval, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(insertRemoval, 1, removalId.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insertRemoval, 2, sampleHash.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(insertRemoval, 3, sampleSize);
        sqlite3_bind_text(insertRemoval, 4, tag.toRawUTF8(), -1, SQLITE_TRANSIENT);
        bool ok = sqlite3_step(insertRemoval) == SQLITE_DONE;
        sqlite3_finalize(insertRemoval);
        if (!ok) return false;

        sqlite3_stmt* insertObserved = nullptr;
        if (sqlite3_prepare_v2(connection,
                "INSERT INTO metadata_tag_observed_adds(remove_operation_id,add_operation_id) VALUES(?,?)",
                -1, &insertObserved, nullptr) != SQLITE_OK)
            return false;
        for (const auto& addId : observedAdds)
        {
            sqlite3_reset(insertObserved);
            sqlite3_clear_bindings(insertObserved);
            sqlite3_bind_text(insertObserved, 1, removalId.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insertObserved, 2, addId.toRawUTF8(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(insertObserved) != SQLITE_DONE) { ok = false; break; }
        }
        sqlite3_finalize(insertObserved);
        return ok;
    };

    bool ok = true;
    if (hasPortableIdentity)
    {
        for (const auto& oldTag : currentTags)
            if (!requestedTags.contains(oldTag))
                ok = recordRemoval(oldTag) && ok;
        for (const auto& newTag : requestedTags)
            if (!currentTags.contains(newTag))
                ok = recordAdd(newTag) && ok;
    }

    sqlite3_stmt* remove = nullptr;
    if (ok && sqlite3_prepare_v2(connection, "DELETE FROM sample_tags WHERE path=?", -1, &remove, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(remove, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(remove) == SQLITE_DONE;
        sqlite3_finalize(remove);
    }
    else if (ok) ok = false;

    sqlite3_stmt* add = nullptr;
    if (ok && sqlite3_prepare_v2(connection,
            "INSERT OR IGNORE INTO sample_tags(path,tag) SELECT ?,? WHERE EXISTS(SELECT 1 FROM samples WHERE path=?)",
            -1, &add, nullptr) == SQLITE_OK)
    {
        for (const auto& tag : requestedTags)
        {
            sqlite3_reset(add);
            sqlite3_clear_bindings(add);
            sqlite3_bind_text(add, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(add, 2, tag.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(add, 3, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(add) != SQLITE_DONE) { ok = false; break; }
        }
        sqlite3_finalize(add);
    }
    else if (ok) ok = false;

    const int transactionResult = sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok && transactionResult == SQLITE_OK;
}

juce::StringArray SampleDatabase::getUserTags(const juce::File& file) const
{
    juce::StringArray tags;if(connection==nullptr)return tags;sqlite3_stmt* q=nullptr;
    if(sqlite3_prepare_v2(connection,"SELECT tag FROM sample_tags WHERE path=? ORDER BY tag COLLATE NOCASE",-1,&q,nullptr)!=SQLITE_OK)return tags;
    const auto path=file.getFullPathName();sqlite3_bind_text(q,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);
    while(sqlite3_step(q)==SQLITE_ROW){const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(q,0));if(value)tags.add(juce::String::fromUTF8(value));}sqlite3_finalize(q);return tags;
}

juce::StringArray SampleDatabase::getClassificationTags(const juce::File& file) const
{
    juce::StringArray tags;
    if (connection == nullptr) return tags;
    sqlite3_stmt* query = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT tag FROM sample_classification_tags WHERE path=? ORDER BY tag COLLATE NOCASE", -1, &query, nullptr) != SQLITE_OK)
        return tags;
    const auto path = file.getFullPathName();
    sqlite3_bind_text(query, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(query) == SQLITE_ROW)
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
        if (value != nullptr) tags.add(juce::String::fromUTF8(value));
    }
    sqlite3_finalize(query);
    return tags;
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

juce::StringArray SampleDatabase::getMappingProfileNames() const
{
    juce::StringArray names;
    if (connection == nullptr) return names;
    sqlite3_stmt* query = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT name FROM mapping_profiles ORDER BY name COLLATE NOCASE", -1, &query, nullptr) != SQLITE_OK)
        return names;
    while (sqlite3_step(query) == SQLITE_ROW)
    {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
        if (value != nullptr) names.addIfNotAlreadyThere(juce::String::fromUTF8(value));
    }
    sqlite3_finalize(query);
    return names;
}

juce::var SampleDatabase::exportPortableMetadata() const
{
    auto archive = juce::var(new juce::DynamicObject());
    auto* root = archive.getDynamicObject();
    root->setProperty("format", "com.magdrum.library-metadata");
    root->setProperty("version", 1);
    root->setProperty("created_utc", juce::Time::getCurrentTime().toISO8601(true));

    struct ExportedSample
    {
        juce::String hash;
        std::int64_t size = 0;
        juce::StringArray categories, tags;
    };
    std::map<std::pair<std::string, std::int64_t>, ExportedSample> samples;
    if (connection != nullptr)
    {
        sqlite3_stmt* query = nullptr;
        const char* sql = "SELECT s.fingerprint,s.size_bytes,s.user_category,st.tag FROM samples s LEFT JOIN sample_tags st ON st.path=s.path WHERE length(s.fingerprint)=64 AND (s.user_category<>'' OR st.tag IS NOT NULL) ORDER BY s.fingerprint,s.size_bytes,st.tag";
        if (sqlite3_prepare_v2(connection, sql, -1, &query, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(query) == SQLITE_ROW)
            {
                const auto* hashValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
                if (hashValue == nullptr) continue;
                const auto hash = juce::String::fromUTF8(hashValue).toLowerCase();
                if (!hash.containsOnly("0123456789abcdef")) continue;
                const auto size = sqlite3_column_int64(query, 1);
                auto& sample = samples[{hash.toStdString(), size}];
                sample.hash = hash;
                sample.size = size;
                const auto* categoryValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 2));
                if (categoryValue != nullptr)
                    sample.categories.addIfNotAlreadyThere(juce::String::fromUTF8(categoryValue));
                const auto* tagValue = reinterpret_cast<const char*>(sqlite3_column_text(query, 3));
                if (tagValue != nullptr)
                    sample.tags.addIfNotAlreadyThere(juce::String::fromUTF8(tagValue));
            }
            sqlite3_finalize(query);
        }
    }

    juce::Array<juce::var> sampleArray;
    for (const auto& [key, sample] : samples)
    {
        juce::ignoreUnused(key);
        auto item = juce::var(new juce::DynamicObject());
        item.getDynamicObject()->setProperty("sha256", sample.hash);
        item.getDynamicObject()->setProperty("size_bytes", juce::var(static_cast<juce::int64>(sample.size)));
        juce::Array<juce::var> categories, tags;
        for (const auto& category : sample.categories) categories.add(category);
        for (const auto& tag : sample.tags) tags.add(tag);
        item.getDynamicObject()->setProperty("manual_categories", juce::var(categories));
        item.getDynamicObject()->setProperty("user_tags", juce::var(tags));
        sampleArray.add(item);
    }
    root->setProperty("samples", juce::var(sampleArray));

    juce::Array<juce::var> profiles;
    for (const auto& name : getMappingProfileNames())
        if (const auto profile = loadMappingProfile(name))
        {
            auto item = juce::var(new juce::DynamicObject());
            item.getDynamicObject()->setProperty("name", name);
            item.getDynamicObject()->setProperty("csv", juce::String::fromUTF8(profile->toCsv().c_str()));
            profiles.add(item);
        }
    root->setProperty("mapping_profiles", juce::var(profiles));
    return archive;
}

PortableMetadataMergeResult SampleDatabase::mergePortableMetadata(const juce::var& archive, bool mergeTags)
{
    PortableMetadataMergeResult result;
    if (connection == nullptr || !archive.isObject()
        || archive.getProperty("format", {}).toString() != "com.magdrum.library-metadata"
        || static_cast<int>(archive.getProperty("version", {})) != 1)
    {
        result.error = "Unsupported or invalid MagDrum metadata archive.";
        return result;
    }

    const auto sampleValue = archive.getProperty("samples", {});
    const auto profileValue = archive.getProperty("mapping_profiles", {});
    const auto* sampleArray = sampleValue.getArray();
    const auto* profileArray = profileValue.getArray();
    if (sampleArray == nullptr || profileArray == nullptr || sampleArray->size() > 1000000
        || profileArray->size() > 10000)
    {
        result.error = "Archive is missing its metadata arrays or exceeds the supported record limit.";
        return result;
    }

    const auto sourceDeviceId = archive.getProperty("device_id", {}).toString();
    const auto appendConflictValue = [&result, &sourceDeviceId](PortableMetadataConflict conflict,
                                                                 const juce::String& incomingValue)
    {
        auto existing = std::find_if(result.conflicts.begin(), result.conflicts.end(),
            [&conflict](const PortableMetadataConflict& candidate)
            {
                return candidate.type == conflict.type && candidate.sampleHash == conflict.sampleHash
                    && candidate.sampleSizeBytes == conflict.sampleSizeBytes
                    && candidate.profileName == conflict.profileName && candidate.localValue == conflict.localValue;
            });
        if (existing == result.conflicts.end())
        {
            result.conflicts.push_back(std::move(conflict));
            existing = std::prev(result.conflicts.end());
        }
        const bool duplicate = std::any_of(existing->incomingValues.begin(), existing->incomingValues.end(),
            [&sourceDeviceId, &incomingValue](const PortableMetadataConflictCandidate& candidate)
            {
                return candidate.deviceId == sourceDeviceId && candidate.value == incomingValue;
            });
        if (!duplicate) existing->incomingValues.push_back({sourceDeviceId, incomingValue});
    };

    struct IncomingSample
    {
        juce::String hash;
        std::int64_t size = 0;
        juce::StringArray categories, tags;
    };
    struct IncomingProfile
    {
        juce::String name;
        MidiMappingProfile profile;
        IncomingProfile(juce::String value, MidiMappingProfile mapping) : name(std::move(value)), profile(std::move(mapping)) {}
    };
    std::vector<IncomingSample> incomingSamples;
    std::vector<IncomingProfile> incomingProfiles;
    std::set<std::pair<std::string, std::int64_t>> sampleKeys;
    std::set<std::string> profileNames;

    for (const auto& value : *sampleArray)
    {
        if (!value.isObject()) { result.error = "Archive contains an invalid sample record."; return result; }
        const auto hash = value.getProperty("sha256", {}).toString().toLowerCase();
        const auto sizeValue = value.getProperty("size_bytes", {});
        if (hash.length() != 64 || !hash.containsOnly("0123456789abcdef")
            || (!sizeValue.isInt() && !sizeValue.isInt64()) || static_cast<juce::int64>(sizeValue) < 0
            || !sampleKeys.insert({hash.toStdString(), static_cast<juce::int64>(sizeValue)}).second)
        {
            result.error = "Archive contains a duplicate or invalid sample identity.";
            return result;
        }
        IncomingSample sample;
        sample.hash = hash;
        sample.size = static_cast<juce::int64>(sizeValue);
        const auto categoriesValue = value.getProperty("manual_categories", {});
        const auto tagsValue = value.getProperty("user_tags", {});
        const auto* categories = categoriesValue.getArray();
        const auto* tags = tagsValue.getArray();
        if (categories == nullptr || tags == nullptr || categories->size() > 16 || tags->size() > 10000)
        {
            result.error = "Archive contains an invalid sample metadata list.";
            return result;
        }
        for (const auto& category : *categories)
        {
            if (!category.isString() || category.toString().trim().isEmpty() || category.toString().length() > 64)
            { result.error = "Archive contains an invalid manual category."; return result; }
            sample.categories.addIfNotAlreadyThere(category.toString().trim());
        }
        for (const auto& tag : *tags)
        {
            if (!tag.isString() || tag.toString().trim().isEmpty() || tag.toString().length() > 256)
            { result.error = "Archive contains an invalid user tag."; return result; }
            sample.tags.addIfNotAlreadyThere(tag.toString().trim());
        }
        incomingSamples.push_back(std::move(sample));
    }

    for (const auto& value : *profileArray)
    {
        if (!value.isObject()) { result.error = "Archive contains an invalid mapping profile."; return result; }
        const auto nameValue = value.getProperty("name", {});
        const auto csvValue = value.getProperty("csv", {});
        if (!nameValue.isString() || !csvValue.isString() || nameValue.toString().trim().isEmpty()
            || nameValue.toString().length() > 128 || csvValue.toString().length() > 65536
            || !profileNames.insert(nameValue.toString().trim().toLowerCase().toStdString()).second)
        { result.error = "Archive contains a duplicate or invalid mapping profile."; return result; }
        const auto csv = csvValue.toString().toStdString();
        const auto profile = MidiMappingProfile::fromCsv(csv);
        if (!profile) { result.error = "Archive contains an invalid MIDI mapping CSV."; return result; }
        incomingProfiles.emplace_back(nameValue.toString().trim(), *profile);
    }

    const bool ownsTransaction = sqlite3_get_autocommit(connection) != 0;
    if (ownsTransaction && sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
    { result.error = "Could not start the metadata import transaction."; return result; }
    bool ok = true;
    sqlite3_stmt* findSamples = nullptr;
    sqlite3_stmt* updateCategory = nullptr;
    sqlite3_stmt* insertTag = nullptr;
    sqlite3_stmt* findProfile = nullptr;
    sqlite3_stmt* insertProfile = nullptr;
    sqlite3_stmt* insertMapping = nullptr;
    const auto prepare = [&](sqlite3_stmt** statement, const char* sql)
    {
        if (sqlite3_prepare_v2(connection, sql, -1, statement, nullptr) == SQLITE_OK) return true;
        ok = false;
        return false;
    };
    prepare(&findSamples, "SELECT path,user_category FROM samples WHERE fingerprint=? AND size_bytes=?");
    prepare(&updateCategory, "UPDATE samples SET user_category=?,category=? WHERE path=? AND user_category=''");
    prepare(&insertTag, "INSERT OR IGNORE INTO sample_tags(path,tag) VALUES(?,?)");
    prepare(&findProfile, "SELECT 1 FROM mapping_profiles WHERE name=?");
    prepare(&insertProfile, "INSERT INTO mapping_profiles(name) VALUES(?)");
    prepare(&insertMapping, "INSERT INTO mapping_entries(profile_name,instrument,articulation,note) VALUES(?,?,?,?)");
    if (!ok)
    {
        for (auto* statement : {findSamples, updateCategory, insertTag, findProfile, insertProfile, insertMapping})
            if (statement != nullptr) sqlite3_finalize(statement);
        if (ownsTransaction) sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        result.error = "Could not prepare the metadata import transaction.";
        return result;
    }

    for (const auto& sample : incomingSamples)
    {
        if (!ok) break;
        sqlite3_reset(findSamples); sqlite3_clear_bindings(findSamples);
        sqlite3_bind_text(findSamples, 1, sample.hash.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(findSamples, 2, sample.size);
        bool matched = false;
        int sampleStep = SQLITE_OK;
        while ((sampleStep = sqlite3_step(findSamples)) == SQLITE_ROW)
        {
            matched = true;
            const auto* pathValue = reinterpret_cast<const char*>(sqlite3_column_text(findSamples, 0));
            const auto* categoryValue = reinterpret_cast<const char*>(sqlite3_column_text(findSamples, 1));
            if (pathValue == nullptr) { ok = false; break; }
            const juce::String path = juce::String::fromUTF8(pathValue);
            const juce::String currentCategory = categoryValue != nullptr ? juce::String::fromUTF8(categoryValue) : juce::String();
            if (sample.categories.size() == 1)
            {
                const auto incoming = sample.categories[0];
                if (currentCategory.isEmpty())
                {
                    sqlite3_reset(updateCategory); sqlite3_clear_bindings(updateCategory);
                    sqlite3_bind_text(updateCategory, 1, incoming.toRawUTF8(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(updateCategory, 2, incoming.toRawUTF8(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(updateCategory, 3, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(updateCategory) != SQLITE_DONE) { ok = false; break; }
                }
                else if (currentCategory != incoming)
                {
                    PortableMetadataConflict conflict;
                    conflict.type = PortableMetadataConflictType::manualCategory;
                    conflict.sampleHash = sample.hash;
                    conflict.sampleSizeBytes = sample.size;
                    conflict.localValue = currentCategory;
                    appendConflictValue(std::move(conflict), incoming);
                }
            }
            else if (sample.categories.size() > 1)
            {
                for (const auto& incoming : sample.categories)
                {
                    PortableMetadataConflict conflict;
                    conflict.type = PortableMetadataConflictType::manualCategory;
                    conflict.sampleHash = sample.hash;
                    conflict.sampleSizeBytes = sample.size;
                    conflict.localValue = currentCategory;
                    appendConflictValue(std::move(conflict), incoming);
                }
            }

            for (const auto& tag : sample.tags)
            {
                if (!mergeTags) continue;
                sqlite3_reset(insertTag); sqlite3_clear_bindings(insertTag);
                sqlite3_bind_text(insertTag, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertTag, 2, tag.toRawUTF8(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(insertTag) != SQLITE_DONE) { ok = false; break; }
                const bool inserted = sqlite3_changes(connection) > 0;
                result.tagsAdded += inserted ? 1 : 0;
                if (inserted)
                {
                    const auto operationId = juce::Uuid().toString().removeCharacters("{}-").toLowerCase();
                    sqlite3_stmt* addOperation = nullptr;
                    if (sqlite3_prepare_v2(connection,
                            "INSERT OR IGNORE INTO metadata_tag_operations(operation_id,sample_sha256,sample_size_bytes,tag,operation) VALUES(?,?,?,?,'add')",
                            -1, &addOperation, nullptr) != SQLITE_OK)
                    { ok = false; break; }
                    sqlite3_bind_text(addOperation, 1, operationId.toRawUTF8(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(addOperation, 2, sample.hash.toRawUTF8(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(addOperation, 3, sample.size);
                    sqlite3_bind_text(addOperation, 4, tag.toRawUTF8(), -1, SQLITE_TRANSIENT);
                    ok = sqlite3_step(addOperation) == SQLITE_DONE;
                    sqlite3_finalize(addOperation);
                    if (!ok) break;
                }
            }
            if (!ok) break;
        }
        if (sampleStep != SQLITE_DONE) ok = false;
        if (matched) ++result.samplesMatched; else ++result.samplesUnavailable;
    }

    for (const auto& incoming : incomingProfiles)
    {
        if (!ok) break;
        sqlite3_reset(findProfile); sqlite3_clear_bindings(findProfile);
        sqlite3_bind_text(findProfile, 1, incoming.name.toRawUTF8(), -1, SQLITE_TRANSIENT);
        const int profileStep = sqlite3_step(findProfile);
        if (profileStep == SQLITE_ROW)
        {
            const auto existing = loadMappingProfile(incoming.name);
            if (!existing || existing->toCsv() != incoming.profile.toCsv())
            {
                PortableMetadataConflict conflict;
                conflict.type = PortableMetadataConflictType::mappingProfile;
                conflict.profileName = incoming.name;
                conflict.localValue = existing ? juce::String::fromUTF8(existing->toCsv().c_str()) : juce::String();
                appendConflictValue(std::move(conflict), juce::String::fromUTF8(incoming.profile.toCsv().c_str()));
            }
            continue;
        }
        if (profileStep != SQLITE_DONE) { ok = false; break; }
        sqlite3_reset(insertProfile); sqlite3_clear_bindings(insertProfile);
        sqlite3_bind_text(insertProfile, 1, incoming.name.toRawUTF8(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(insertProfile) != SQLITE_DONE) { ok = false; break; }
        for (const auto& mapping : incoming.profile.entries())
        {
            sqlite3_reset(insertMapping); sqlite3_clear_bindings(insertMapping);
            sqlite3_bind_text(insertMapping, 1, incoming.name.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(insertMapping, 2, static_cast<int>(mapping.instrument));
            sqlite3_bind_int(insertMapping, 3, static_cast<int>(mapping.articulation));
            sqlite3_bind_int(insertMapping, 4, mapping.note);
            if (sqlite3_step(insertMapping) != SQLITE_DONE) { ok = false; break; }
        }
        if (ok) ++result.profilesAdded;
    }

    for (auto* statement : {findSamples, updateCategory, insertTag, findProfile, insertProfile, insertMapping})
        if (statement != nullptr) sqlite3_finalize(statement);
    const int transactionResult = ownsTransaction
        ? sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr)
        : SQLITE_OK;
    result.ok = ok && transactionResult == SQLITE_OK;
    result.categoryConflicts = static_cast<int>(std::count_if(result.conflicts.begin(), result.conflicts.end(),
        [](const PortableMetadataConflict& conflict) { return conflict.type == PortableMetadataConflictType::manualCategory; }));
    result.profileConflicts = static_cast<int>(std::count_if(result.conflicts.begin(), result.conflicts.end(),
        [](const PortableMetadataConflict& conflict) { return conflict.type == PortableMetadataConflictType::mappingProfile; }));
    if (!result.ok) result.error = "Metadata import failed; no changes were committed.";
    return result;
}

PortableMetadataSyncResult SampleDatabase::syncPortableMetadataFolder(const juce::File& folder)
{
    PortableMetadataSyncResult result;
    if (connection == nullptr || !folder.isDirectory())
    {
        result.error = "Choose an existing synchronized folder.";
        return result;
    }

    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        result.error = "Could not read this device's metadata sync identity.";
        return result;
    }
    sqlite3_stmt* deviceQuery = nullptr;
    if (sqlite3_prepare_v2(connection, "SELECT value FROM metadata_sync_state WHERE key='device_id'", -1, &deviceQuery, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(deviceQuery) == SQLITE_ROW)
        {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(deviceQuery, 0));
            if (value != nullptr) result.deviceId = juce::String::fromUTF8(value);
        }
        sqlite3_finalize(deviceQuery);
    }
    if (result.deviceId.isEmpty())
    {
        result.deviceId = juce::Uuid().toString().removeCharacters("{}").replace("-", "");
        sqlite3_stmt* deviceInsert = nullptr;
        if (sqlite3_prepare_v2(connection, "INSERT INTO metadata_sync_state(key,value) VALUES('device_id',?)", -1, &deviceInsert, nullptr) != SQLITE_OK)
        {
            sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
            result.error = "Could not save this device's metadata sync identity.";
            return result;
        }
        sqlite3_bind_text(deviceInsert, 1, result.deviceId.toRawUTF8(), -1, SQLITE_TRANSIENT);
        const bool saved = sqlite3_step(deviceInsert) == SQLITE_DONE;
        sqlite3_finalize(deviceInsert);
        if (!saved)
        {
            sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
            result.error = "Could not save this device's metadata sync identity.";
            return result;
        }
    }
    if (sqlite3_exec(connection, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        result.error = "Could not save this device's metadata sync identity.";
        return result;
    }

    auto archives = folder.findChildFiles(juce::File::findFiles, false, "MagDrum-sync-*.json");
    if (archives.size() > 128)
    {
        result.error = "The synchronized folder contains more than 128 MagDrum device archives.";
        return result;
    }

    struct IncomingSyncArchive
    {
        juce::var metadata;
        bool hasTagOperations = false;
        std::vector<MetadataTagOperation> tagOperations;
    };
    std::int64_t totalArchiveBytes = 0;
    std::vector<IncomingSyncArchive> incomingArchives;
    for (const auto& archiveFile : archives)
    {
        const auto archiveBytes = archiveFile.getSize();
        if (archiveBytes < 0 || archiveBytes > 128LL * 1024 * 1024
            || totalArchiveBytes > 256LL * 1024 * 1024 - archiveBytes)
        {
            result.error = "The synchronized metadata archives exceed the per-file or 256 MB total safety limit.";
            return result;
        }
        totalArchiveBytes += archiveBytes;

        const auto archive = juce::JSON::parse(archiveFile.loadFileAsString());
        if (!archive.isObject()
            || archive.getProperty("format", {}).toString() != "com.magdrum.library-metadata"
            || !archive.getProperty("device_id", {}).isString()
            || static_cast<int>(archive.getProperty("version", {})) != 1)
        {
            result.error = "A synchronized device archive is invalid or incomplete.";
            return result;
        }

        const auto syncVersionValue = archive.getProperty("sync_version", {});
        const int syncVersion = syncVersionValue.isVoid() ? 1 : static_cast<int>(syncVersionValue);
        if ((!syncVersionValue.isVoid() && !syncVersionValue.isInt() && !syncVersionValue.isInt64())
            || (syncVersion != 1 && syncVersion != 2))
        {
            result.error = "A synchronized device archive uses an unsupported sync version.";
            return result;
        }

        IncomingSyncArchive incoming;
        incoming.metadata = archive;
        incoming.hasTagOperations = syncVersion == 2;
        if (incoming.hasTagOperations
            && !parseMetadataTagOperations(archive.getProperty("tag_operations", {}), incoming.tagOperations, result.error))
            return result;

        if (archive.getProperty("device_id", {}).toString() == result.deviceId)
            continue;
        incomingArchives.push_back(std::move(incoming));
    }

    if (sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        result.error = "Could not start the metadata sync transaction.";
        return result;
    }
    const char* backfillTagOperations = R"sql(
        INSERT OR IGNORE INTO metadata_tag_operations(operation_id,sample_sha256,sample_size_bytes,tag,operation)
        SELECT 'legacy:' || lower(s.fingerprint) || ':' || s.size_bytes || ':' || lower(hex(t.tag)),
               lower(s.fingerprint),s.size_bytes,t.tag,'add'
        FROM samples s JOIN sample_tags t ON t.path=s.path
        WHERE length(s.fingerprint)=64 AND lower(s.fingerprint) NOT GLOB '*[^0-9a-f]*' AND s.size_bytes>=0;
    )sql";
    if (sqlite3_exec(connection, backfillTagOperations, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        result.error = "Could not migrate existing user tags for synchronization.";
        return result;
    }
    std::set<std::pair<std::string, std::int64_t>> touchedTagIdentities;
    for (const auto& archive : incomingArchives)
    {
        const auto merged = mergePortableMetadata(archive.metadata, !archive.hasTagOperations);
        if (!merged.ok)
        {
            sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
            result.archivesMerged = 0;
            result.samplesMatched = 0;
            result.samplesUnavailable = 0;
            result.tagsAdded = 0;
            result.categoryConflicts = 0;
            result.profilesAdded = 0;
            result.profileConflicts = 0;
            result.error = merged.error;
            return result;
        }

        ++result.archivesMerged;
        result.samplesMatched += merged.samplesMatched;
        result.samplesUnavailable += merged.samplesUnavailable;
        result.tagsAdded += merged.tagsAdded;
        for (const auto& conflict : merged.conflicts)
        {
            auto existing = std::find_if(result.conflicts.begin(), result.conflicts.end(),
                [&conflict](const PortableMetadataConflict& candidate)
                {
                    return candidate.type == conflict.type && candidate.sampleHash == conflict.sampleHash
                        && candidate.sampleSizeBytes == conflict.sampleSizeBytes
                        && candidate.profileName == conflict.profileName && candidate.localValue == conflict.localValue;
                });
            if (existing == result.conflicts.end())
            {
                result.conflicts.push_back(conflict);
                existing = std::prev(result.conflicts.end());
            }
            for (const auto& incoming : conflict.incomingValues)
            {
                const bool duplicate = std::any_of(existing->incomingValues.begin(), existing->incomingValues.end(),
                    [&incoming](const PortableMetadataConflictCandidate& candidate)
                    {
                        return candidate.deviceId == incoming.deviceId && candidate.value == incoming.value;
                    });
                if (!duplicate) existing->incomingValues.push_back(incoming);
            }
        }
        result.profilesAdded += merged.profilesAdded;
        result.categoryConflicts = static_cast<int>(std::count_if(result.conflicts.begin(), result.conflicts.end(),
            [](const PortableMetadataConflict& conflict) { return conflict.type == PortableMetadataConflictType::manualCategory; }));
        result.profileConflicts = static_cast<int>(std::count_if(result.conflicts.begin(), result.conflicts.end(),
            [](const PortableMetadataConflict& conflict) { return conflict.type == PortableMetadataConflictType::mappingProfile; }));
        if (archive.hasTagOperations
            && !mergeMetadataTagOperations(connection, archive.tagOperations, touchedTagIdentities, result.error))
        {
            sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
            result.archivesMerged = 0;
            result.samplesMatched = 0;
            result.samplesUnavailable = 0;
            result.tagsAdded = 0;
            result.categoryConflicts = 0;
            result.profilesAdded = 0;
            result.profileConflicts = 0;
            return result;
        }
    }

    if (!validateSynchronizedTagRemovalReferences(connection, result.error))
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        result.archivesMerged = 0;
        result.samplesMatched = 0;
        result.samplesUnavailable = 0;
        result.tagsAdded = 0;
        result.categoryConflicts = 0;
        result.profilesAdded = 0;
        result.profileConflicts = 0;
        return result;
    }
    for (const auto& identity : touchedTagIdentities)
    {
        if (!rebuildSynchronizedTags(connection, identity.first, identity.second, result.tagsAdded, result.error))
        {
            sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
            result.archivesMerged = 0;
            result.samplesMatched = 0;
            result.samplesUnavailable = 0;
            result.tagsAdded = 0;
            result.categoryConflicts = 0;
            result.profilesAdded = 0;
            result.profileConflicts = 0;
            return result;
        }
    }

    auto snapshot = exportPortableMetadata();
    if (!snapshot.isObject())
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        result.error = "Could not create this device's metadata snapshot.";
        return result;
    }
    snapshot.getDynamicObject()->setProperty("device_id", result.deviceId);
    snapshot.getDynamicObject()->setProperty("created_utc", juce::Time::getCurrentTime().toISO8601(true));
    snapshot.getDynamicObject()->setProperty("sync_version", 2);
    const auto tagOperations = exportMetadataTagOperations(connection);
    if (!tagOperations.isArray())
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        result.error = "Could not create this device's tag-operation snapshot.";
        return result;
    }
    snapshot.getDynamicObject()->setProperty("tag_operations", tagOperations);
    const auto ownArchive = folder.getChildFile("MagDrum-sync-" + result.deviceId + ".json");
    juce::TemporaryFile stagedSnapshot(ownArchive);
    if (!stagedSnapshot.getFile().replaceWithText(juce::JSON::toString(snapshot, true)))
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        result.error = "Could not write this device's metadata snapshot to the synchronized folder.";
        return result;
    }
    if (sqlite3_exec(connection, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        result.error = "Could not commit the metadata sync transaction.";
        return result;
    }
    result.metadataMerged = true;
    if (!stagedSnapshot.overwriteTargetFileWithTemporary())
    {
        result.error = "Metadata was merged, but the new device snapshot could not replace its previous file. Sync again after checking folder permissions.";
        return result;
    }

    result.ok = true;
    return result;
}

bool SampleDatabase::resolvePortableMetadataConflict(const PortableMetadataConflict& conflict,
                                                     const PortableMetadataConflictCandidate& choice)
{
    if (connection == nullptr || choice.value.isEmpty() || sqlite3_get_autocommit(connection) == 0)
        return false;
    const bool isKnownChoice = std::any_of(conflict.incomingValues.begin(), conflict.incomingValues.end(),
        [&choice](const PortableMetadataConflictCandidate& candidate)
        {
            return candidate.deviceId == choice.deviceId && candidate.value == choice.value;
        });
    if (!isKnownChoice || sqlite3_exec(connection, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;

    bool ok = true;
    if (conflict.type == PortableMetadataConflictType::manualCategory)
    {
        sqlite3_stmt* updatePluginSounds = nullptr;
        sqlite3_stmt* updateSamples = nullptr;
        if (sqlite3_prepare_v2(connection,
                "UPDATE plugin_sounds SET category=? WHERE preview_path IN "
                "(SELECT path FROM samples WHERE fingerprint=? AND size_bytes=? AND COALESCE(user_category,'')=?)",
                -1, &updatePluginSounds, nullptr) != SQLITE_OK
            || sqlite3_prepare_v2(connection,
                "UPDATE samples SET user_category=?,category=? WHERE fingerprint=? AND size_bytes=? AND COALESCE(user_category,'')=?",
                -1, &updateSamples, nullptr) != SQLITE_OK)
            ok = false;

        if (ok)
        {
            sqlite3_bind_text(updatePluginSounds, 1, choice.value.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(updatePluginSounds, 2, conflict.sampleHash.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(updatePluginSounds, 3, conflict.sampleSizeBytes);
            sqlite3_bind_text(updatePluginSounds, 4, conflict.localValue.toRawUTF8(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(updatePluginSounds) == SQLITE_DONE;
        }
        if (ok)
        {
            sqlite3_bind_text(updateSamples, 1, choice.value.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(updateSamples, 2, choice.value.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(updateSamples, 3, conflict.sampleHash.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(updateSamples, 4, conflict.sampleSizeBytes);
            sqlite3_bind_text(updateSamples, 5, conflict.localValue.toRawUTF8(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(updateSamples) == SQLITE_DONE && sqlite3_changes(connection) > 0;
        }
        if (updatePluginSounds != nullptr) sqlite3_finalize(updatePluginSounds);
        if (updateSamples != nullptr) sqlite3_finalize(updateSamples);
    }
    else if (conflict.type == PortableMetadataConflictType::mappingProfile)
    {
        const auto current = loadMappingProfile(conflict.profileName);
        const auto currentCsv = current ? juce::String::fromUTF8(current->toCsv().c_str()) : juce::String();
        const auto incoming = MidiMappingProfile::fromCsv(choice.value.toStdString());
        ok = !conflict.profileName.trim().isEmpty() && currentCsv == conflict.localValue && incoming.has_value();

        sqlite3_stmt* ensureProfile = nullptr;
        sqlite3_stmt* removeMappings = nullptr;
        sqlite3_stmt* addMapping = nullptr;
        if (ok && (sqlite3_prepare_v2(connection,
                "INSERT INTO mapping_profiles(name) VALUES(?) ON CONFLICT(name) DO NOTHING",
                -1, &ensureProfile, nullptr) != SQLITE_OK
            || sqlite3_prepare_v2(connection, "DELETE FROM mapping_entries WHERE profile_name=?",
                -1, &removeMappings, nullptr) != SQLITE_OK
            || sqlite3_prepare_v2(connection,
                "INSERT INTO mapping_entries(profile_name,instrument,articulation,note) VALUES(?,?,?,?)",
                -1, &addMapping, nullptr) != SQLITE_OK))
            ok = false;
        if (ok)
        {
            sqlite3_bind_text(ensureProfile, 1, conflict.profileName.toRawUTF8(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(ensureProfile) == SQLITE_DONE;
        }
        if (ok)
        {
            sqlite3_bind_text(removeMappings, 1, conflict.profileName.toRawUTF8(), -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(removeMappings) == SQLITE_DONE;
        }
        if (ok)
        {
            for (const auto& mapping : incoming->entries())
            {
                sqlite3_reset(addMapping);
                sqlite3_clear_bindings(addMapping);
                sqlite3_bind_text(addMapping, 1, conflict.profileName.toRawUTF8(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(addMapping, 2, static_cast<int>(mapping.instrument));
                sqlite3_bind_int(addMapping, 3, static_cast<int>(mapping.articulation));
                sqlite3_bind_int(addMapping, 4, mapping.note);
                if (sqlite3_step(addMapping) != SQLITE_DONE)
                {
                    ok = false;
                    break;
                }
            }
        }
        if (ensureProfile != nullptr) sqlite3_finalize(ensureProfile);
        if (removeMappings != nullptr) sqlite3_finalize(removeMappings);
        if (addMapping != nullptr) sqlite3_finalize(addMapping);
    }
    else
    {
        ok = false;
    }

    const int transactionResult = sqlite3_exec(connection, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    if (transactionResult != SQLITE_OK)
    {
        sqlite3_exec(connection, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    return ok;
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
    const char* sql = "SELECT s.path,COALESCE(NULLIF(s.user_category,''),s.category),s.duration,s.spectral_centroid,COALESCE(src.source_type,''),COALESCE(src.display_name,''),COALESCE(src.identifier,''),s.classification_confidence,(s.user_category<>''),(SELECT group_concat(tag,char(31)) FROM sample_classification_tags ct WHERE ct.path=s.path) FROM samples s LEFT JOIN sample_sources src ON src.id=s.source_id ORDER BY s.name COLLATE NOCASE";
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
        const auto* encodedTags = reinterpret_cast<const char*>(sqlite3_column_text(statement, 9));
        if (encodedTags != nullptr) record.classificationTags.addTokens(juce::String::fromUTF8(encodedTags), juce::String::charToString(31), "");
        record.classificationTags.removeEmptyStrings();
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

    const auto trimmedQuery = query.trim();
    const bool useTrigramSearch = trimmedQuery.length() >= 3;
    const auto ftsQuery = juce::String("\"") + trimmedQuery.replace("\"", "\"\"") + "\"";
    const auto queryPredicate = trimmedQuery.isEmpty()
        ? juce::String("1")
        : useTrigramSearch
            ? juce::String("s.rowid IN (SELECT rowid FROM sample_search_fts WHERE sample_search_fts MATCH ?1)")
            : juce::String("(instr(lower(s.name),lower(?1))>0 OR instr(lower(s.path),lower(?1))>0 "
                           "OR EXISTS(SELECT 1 FROM sample_tags st WHERE st.path=s.path AND instr(lower(st.tag),lower(?1))>0) "
                           "OR EXISTS(SELECT 1 FROM sample_classification_tags ct WHERE ct.path=s.path AND instr(lower(ct.tag),lower(?1))>0))");
    sqlite3_stmt* statement = nullptr;
    const auto sql = juce::String(
        "SELECT s.path,COALESCE(NULLIF(s.user_category,''),s.category),s.duration,s.spectral_centroid,COALESCE(src.source_type,''),COALESCE(src.display_name,''),COALESCE(src.identifier,''),s.classification_confidence,(s.user_category<>''),(SELECT group_concat(tag,char(31)) FROM sample_classification_tags ct WHERE ct.path=s.path) "
        "FROM samples s LEFT JOIN sample_sources src ON src.id=s.source_id WHERE (") + queryPredicate
        + ") AND (?4='' OR COALESCE(NULLIF(s.user_category,''),s.category)=?5 COLLATE NOCASE) "
        "ORDER BY s.name COLLATE NOCASE,s.path COLLATE NOCASE LIMIT ?6 OFFSET ?7";
    if (sqlite3_prepare_v2(connection, sql.toRawUTF8(), -1, &statement, nullptr) != SQLITE_OK) return records;

    const auto trimmedCategory = category.trim();
    const auto& boundQuery = useTrigramSearch ? ftsQuery : trimmedQuery;
    sqlite3_bind_text(statement, 1, boundQuery.toRawUTF8(), -1, SQLITE_TRANSIENT);
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
        const auto* encodedTags = reinterpret_cast<const char*>(sqlite3_column_text(statement, 9));
        if (encodedTags != nullptr) record.classificationTags.addTokens(juce::String::fromUTF8(encodedTags), juce::String::charToString(31), "");
        record.classificationTags.removeEmptyStrings();
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

    const auto trimmedQuery = filter.query.trim();
    const bool useTrigramSearch = trimmedQuery.length() >= 3;
    const auto ftsQuery = juce::String("\"") + trimmedQuery.replace("\"", "\"\"") + "\"";
    const auto queryPredicate = trimmedQuery.isEmpty()
        ? juce::String("1")
        : useTrigramSearch
            ? juce::String("s.rowid IN (SELECT rowid FROM sample_search_fts WHERE sample_search_fts MATCH ?1)")
            : juce::String("(instr(lower(s.name),lower(?1))>0 OR instr(lower(s.path),lower(?1))>0 "
                           "OR instr(lower(p.name),lower(?1))>0 OR instr(lower(k.name),lower(?1))>0 "
                           "OR EXISTS(SELECT 1 FROM sample_tags st WHERE st.path=s.path AND instr(lower(st.tag),lower(?1))>0) "
                           "OR EXISTS(SELECT 1 FROM sample_classification_tags ct WHERE ct.path=s.path AND instr(lower(ct.tag),lower(?1))>0))");
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
        "COALESCE(ps.articulation,''),COALESCE(rr.take_index,1),"
        "(SELECT group_concat(tag,char(31)) FROM sample_classification_tags ct WHERE ct.path=s.path) "
        "FROM samples s LEFT JOIN sample_sources src ON src.id=s.source_id "
        "LEFT JOIN plugin_sounds ps ON ps.preview_path=s.path "
        "LEFT JOIN (SELECT preview_path,min(kit_id) kit_id,min(midi_note) midi_note,min(velocity) velocity,min(take_index) take_index "
        "FROM plugin_round_robin_takes WHERE silent=0 AND preview_path<>'' GROUP BY preview_path) rr ON rr.preview_path=s.path "
        "LEFT JOIN plugin_kits k ON k.id=COALESCE(ps.kit_id,rr.kit_id) "
        "LEFT JOIN plugin_instruments p ON p.id=k.plugin_id "
        "WHERE (") + queryPredicate + ") AND (?2='' OR COALESCE(NULLIF(s.user_category,''),s.category)=?2 COLLATE NOCASE) "
        "AND (?3='' OR src.source_type=?3) AND (?4='' OR src.identifier=?4) "
        "AND (?5='' OR p.name=?5 COLLATE NOCASE) AND (?6='' OR k.name=?6 COLLATE NOCASE) "
        "AND (?7=0 OR (s.user_category='' AND (s.category='Unknown' OR s.classification_confidence<0.75))) "
        "AND (?8=1 OR (?8=2 AND s.duration<0.1) OR (?8=3 AND s.duration>=0.1 AND s.duration<0.5) "
        "OR (?8=4 AND s.duration>=0.5 AND s.duration<2.0) OR (?8=5 AND s.duration>=2.0) "
        "OR (?8=6 AND s.spectral_centroid>0 AND s.spectral_centroid<1000) "
        "OR (?8=7 AND s.spectral_centroid>=1000 AND s.spectral_centroid<3000) "
        "OR (?8=8 AND s.spectral_centroid>=3000)) ORDER BY " + ordering
        + " LIMIT ?9 OFFSET ?10";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, sql.toRawUTF8(), -1, &statement, nullptr) != SQLITE_OK) return page;
    const auto bindText = [statement](int index, const juce::String& value)
    {
        const auto trimmed = value.trim();
        sqlite3_bind_text(statement, index, trimmed.toRawUTF8(), -1, SQLITE_TRANSIENT);
    };
    bindText(1, useTrigramSearch ? ftsQuery : trimmedQuery); bindText(2, filter.category); bindText(3, filter.sourceType);
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
        record.classificationTags.addTokens(text(16), juce::String::charToString(31), "");
        record.classificationTags.removeEmptyStrings();
        page.records.add(std::move(record));
    }
    sqlite3_finalize(statement);
    return page;
}

juce::Array<juce::File> SampleDatabase::searchPaths(const juce::String& query,int limit) const
{
    juce::Array<juce::File> files;if(connection==nullptr||limit<=0)return files;sqlite3_stmt* statement=nullptr;
    const auto term=query.trim();const bool useTrigramSearch=term.length()>=3;
    const auto ftsQuery=juce::String("\"")+term.replace("\"","\"\"")+"\"";
    const auto sql=useTrigramSearch
        ? juce::String("SELECT s.path FROM samples s WHERE s.rowid IN (SELECT rowid FROM sample_search_fts WHERE sample_search_fts MATCH ?1) ORDER BY s.name COLLATE NOCASE LIMIT ?2")
        : juce::String("SELECT path FROM samples WHERE instr(lower(name),lower(?1))>0 OR instr(lower(path),lower(?1))>0 ORDER BY name COLLATE NOCASE LIMIT ?2");
    if(sqlite3_prepare_v2(connection,sql.toRawUTF8(),-1,&statement,nullptr)!=SQLITE_OK)return files;
    const auto& boundQuery=useTrigramSearch?ftsQuery:term;sqlite3_bind_text(statement,1,boundQuery.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(statement,2,juce::jmin(limit,1000));
    while(sqlite3_step(statement)==SQLITE_ROW){const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(statement,0));if(value)files.add(juce::File(juce::String::fromUTF8(value)));}sqlite3_finalize(statement);return files;
}
