# Database schema

MagDrum stores the local library in `samples.sqlite3` under the user's application data directory. The database is local to the user and is not synchronized or uploaded.

The database evolves with additive migrations. `schema_info.version` records the current schema level (version 6); SQLite's `user_version` is not used. Existing sample paths and manual categories are retained while columns are added. Rows without fingerprint version 1 are rescanned once so the current analysis is populated.

## Current tables

| Table | Purpose |
| --- | --- |
| `samples` | Native files and generated previews, audio metadata including a pitch estimate/confidence when measurable, heuristic category/confidence, exact file SHA-256, a four-segment/16-band normalized acoustic fingerprint and its algorithm version, manual category override, file size and modification time. The original file path is the source reference. |
| `sample_scan_history` | Sample-folder scan start/end, outcome, counts of considered/skipped/indexed files, errors and restart interruptions. |
| `sample_tags` | User tags attached to a sample path. |
| `plugin_instruments` | Discovered plugin instrument identity, format, vendor, category, install reference and version. The format-independent JUCE identifier is unique. |
| `plugin_kits` | Kit/preset records tied to a discovered instrument, including a place for user-approved saved plugin state. |
| `plugin_sounds` | Captured note/velocity sounds tied to a kit, with silence flag, preview reference, analysis and pitch fields, classification and fingerprint. |
| `mapping_profiles` | Named semantic MIDI mapping profiles. |
| `mapping_entries` | Instrument/articulation to MIDI-note mappings within a profile. |
| `schema_info` | Single-row schema version marker, currently version 6. |

Database APIs can upsert plugin instruments, named kits with opaque plugin state, recall saved kit state, and note/velocity sound captures with metadata. The desktop scanner populates kits and captures. Plugin discovery persists in `plugin_instruments` and in a JUCE XML cache used by the plugin list.

## Planned additions

The schema still needs source records distinct from file paths, user-defined classification tags and corrections for plugin-generated sounds, explicit velocity-layer and round-robin groups, and richer analysis/fingerprint versions. Those additions should be introduced with transactional, additive migrations and tested against an existing database before release.
