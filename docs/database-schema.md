# Database schema

MagDrum stores the local library in `samples.sqlite3` under the user's application data directory. The database is local to the user and is not synchronized or uploaded.

The database evolves with additive migrations. `schema_info.version` records the current schema level (version 11); SQLite's `user_version` is not used. Existing sample paths and manual categories are retained while columns are added. Rows without acoustic fingerprint version 1 or the current sample analysis version are rescanned once so updated measurements are populated.

## Current tables

| Table | Purpose |
| --- | --- |
| `samples` | Native files and generated previews, audio metadata including a pitch estimate/confidence when measurable, four-window spectral centroid, heuristic category/confidence, exact file SHA-256, a four-segment/16-band normalized acoustic fingerprint and its algorithm version, analysis version, manual category override, file size and modification time, and a nullable source relationship. |
| `sample_sources` | Stable folder and plugin source identities, display names and folder roots, separate from individual sample paths. Existing folder scans are backfilled when the database opens. |
| `sample_scan_history` | Sample-folder scan start/end, outcome, counts of considered/skipped/indexed files, errors and restart interruptions. |
| `sample_tags` | User tags attached to a sample path. |
| `plugin_instruments` | Discovered plugin instrument identity, format, vendor, category, install reference and version. The format-independent JUCE identifier is unique. |
| `plugin_kits` | Kit/preset records tied to a discovered instrument, including a place for user-approved saved plugin state. |
| `plugin_sounds` | Captured note/velocity sounds tied to a kit, with silence flag, preview reference, analysis and pitch fields, classification, exact and acoustic fingerprints. |
| `plugin_velocity_layer_candidates` | Versioned, heuristic timbral-change candidates between adjacent sampled velocity levels for a MIDI note and kit. |
| `plugin_confirmed_velocity_layers` | User-confirmed, non-overlapping MIDI-note velocity ranges, stored separately from heuristic boundary candidates. |
| `plugin_round_robin_takes` | Each captured note/velocity repeat, including silence, preview path and acoustic fingerprint. |
| `plugin_round_robin_candidates` | Versioned heuristic candidates where repeated triggers have distinct acoustic fingerprints. |
| `plugin_confirmed_round_robin_takes` | The exact captured takes the user has confirmed as one repeated-trigger group; membership is removed automatically when those takes are rescanned or deleted. |
| `mapping_profiles` | Named semantic MIDI mapping profiles. |
| `mapping_entries` | Instrument/articulation to MIDI-note mappings within a profile. |
| `schema_info` | Single-row schema version marker, currently version 11. |

Database APIs can upsert plugin instruments, named kits with opaque plugin state, recall saved kit state, note/velocity sound captures with metadata and versioned velocity-boundary candidates. Manual category corrections to generated previews update both their sample row and the linked plugin sound. The desktop scanner populates kits and captures. Plugin discovery persists in `plugin_instruments` and in a JUCE XML cache used by the plugin list.

## Planned additions

Confirmed velocity ranges and round-robin membership now have distinct, validated database records, migration coverage, and a user-facing review workflow. Velocity-boundary and repeated-trigger variation candidates remain heuristics, not proof of internal instrument behavior; confirmations must come from listening review and still need evaluation against known instruments.
