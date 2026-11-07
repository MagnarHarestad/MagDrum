#include "SampleDatabase.h"

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
    const char* schema = "CREATE TABLE IF NOT EXISTS samples (path TEXT PRIMARY KEY,name TEXT NOT NULL,duration REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0)";
    return sqlite3_exec(connection, schema, nullptr, nullptr, nullptr) == SQLITE_OK;
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
