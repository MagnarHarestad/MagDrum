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
    if (sqlite3_exec(connection, "CREATE TABLE IF NOT EXISTS sample_tags(path TEXT NOT NULL,tag TEXT NOT NULL,PRIMARY KEY(path,tag),FOREIGN KEY(path) REFERENCES samples(path) ON DELETE CASCADE); CREATE INDEX IF NOT EXISTS sample_tags_tag ON sample_tags(tag)", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    if (sqlite3_exec(connection,"CREATE TABLE IF NOT EXISTS mapping_profiles(name TEXT PRIMARY KEY); CREATE TABLE IF NOT EXISTS mapping_entries(profile_name TEXT NOT NULL,instrument INTEGER NOT NULL,articulation INTEGER NOT NULL,note INTEGER NOT NULL CHECK(note BETWEEN 0 AND 127),PRIMARY KEY(profile_name,instrument,articulation),FOREIGN KEY(profile_name) REFERENCES mapping_profiles(name) ON DELETE CASCADE)",nullptr,nullptr,nullptr)!=SQLITE_OK)
        return false;
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

juce::Array<juce::File> SampleDatabase::searchPaths(const juce::String& query,int limit) const
{
    juce::Array<juce::File> files;if(connection==nullptr||limit<=0)return files;sqlite3_stmt* statement=nullptr;
    if(sqlite3_prepare_v2(connection,"SELECT path FROM samples WHERE instr(lower(name),lower(?))>0 OR instr(lower(path),lower(?))>0 ORDER BY name COLLATE NOCASE LIMIT ?",-1,&statement,nullptr)!=SQLITE_OK)return files;
    const auto term=query.trim();sqlite3_bind_text(statement,1,term.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(statement,2,term.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(statement,3,juce::jmin(limit,1000));
    while(sqlite3_step(statement)==SQLITE_ROW){const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(statement,0));if(value)files.add(juce::File(juce::String::fromUTF8(value)));}sqlite3_finalize(statement);return files;
}
