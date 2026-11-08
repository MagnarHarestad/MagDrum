#include "SampleDatabase.h"
#include "WavScanner.h"

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
    const char* schema = "CREATE TABLE IF NOT EXISTS samples (path TEXT PRIMARY KEY,name TEXT NOT NULL,duration REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,format TEXT NOT NULL DEFAULT '',bit_depth INTEGER NOT NULL DEFAULT 0,peak REAL NOT NULL DEFAULT 0,rms REAL NOT NULL DEFAULT 0,spectral_centroid REAL NOT NULL DEFAULT 0,transient_strength REAL NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT 'Unknown',classification_confidence REAL NOT NULL DEFAULT 0,user_category TEXT NOT NULL DEFAULT '')";
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
        "ALTER TABLE samples ADD COLUMN user_category TEXT NOT NULL DEFAULT ''"
    };
    for (const auto* migration : migrations)
        sqlite3_exec(connection, migration, nullptr, nullptr, nullptr); // duplicate-column means already upgraded
    return true;
}

bool SampleDatabase::insertSample(const ScannedSample& sample)
{
    if (connection == nullptr) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO samples(path,name,duration,channels,sample_rate,format,bit_depth,peak,rms,spectral_centroid,transient_strength,fingerprint,category,classification_confidence) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET name=excluded.name,duration=excluded.duration,channels=excluded.channels,sample_rate=excluded.sample_rate,format=excluded.format,bit_depth=excluded.bit_depth,peak=excluded.peak,rms=excluded.rms,spectral_centroid=excluded.spectral_centroid,transient_strength=excluded.transient_strength,fingerprint=excluded.fingerprint,category=CASE WHEN samples.user_category='' THEN excluded.category ELSE samples.user_category END,classification_confidence=excluded.classification_confidence";
    if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    const auto path = sample.file.getFullPathName(); const auto name = sample.file.getFileName();
    sqlite3_bind_text(statement,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,name.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_double(statement,3,sample.durationSeconds); sqlite3_bind_int(statement,4,sample.channels); sqlite3_bind_double(statement,5,sample.sampleRate);
    sqlite3_bind_text(statement,6,sample.format.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(statement,7,sample.bitDepth);
    sqlite3_bind_double(statement,8,sample.peak); sqlite3_bind_double(statement,9,sample.rms); sqlite3_bind_double(statement,10,sample.spectralCentroidHz);
    sqlite3_bind_double(statement,11,sample.transientStrength); sqlite3_bind_text(statement,12,sample.fingerprint.toRawUTF8(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(statement,13,sample.category.toRawUTF8(),-1,SQLITE_TRANSIENT); sqlite3_bind_double(statement,14,sample.classificationConfidence);
    const bool ok=sqlite3_step(statement)==SQLITE_DONE; sqlite3_finalize(statement); return ok;
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
