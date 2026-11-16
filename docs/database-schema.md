# Database schema

MagDrum stores the local library in `samples.sqlite3` under the user's application data directory. The SQLite database remains local to each device. Opt-in metadata synchronization exports validated JSON snapshots to a user-selected folder; audio, local paths and the live database are never synchronized or uploaded.

The database evolves with additive migrations. `schema_info.version` records the current schema level (version 19); SQLite's `user_version` is not used. Existing sample paths, manual categories and user tags are retained while columns, indexes or tables are added. Startup tolerates only the expected duplicate-column error from an already-applied additive migration; other migration errors fail database opening instead of being ignored. Version 17 adds a transactional rebuild of the FTS5 trigram search index from existing sample, tag, source, plugin and kit metadata. Version 18 adds the persistent local device identity used by optional metadata-folder sync. Version 19 adds operation records for user-tag additions and observed removals plus the references each removal observed; startup backfills existing user tags so older libraries can participate in sync. Folder snapshots use these operations to preserve concurrent re-adds. Rows without acoustic fingerprint version 1 or the current sample analysis version (version 7) are rescanned once so updated measurements and filename-derived classifier and articulation tags are populated.

## Current tables

| Table | Purpose |
| --- | --- |
| `samples` | Native files and generated previews, audio metadata including a pitch estimate/confidence when measurable, four-window spectral centroid and spectral flatness, transient attack count, heuristic category/confidence, exact file SHA-256, a four-segment/16-band normalized acoustic fingerprint and its algorithm version, analysis version, manual category override, file size and modification time, and a nullable source relationship. Composite indexes support stable name, duration and centroid ordering in paginated browsing. |
| `sample_sources` | Stable folder and plugin source identities, display names and folder roots, separate from individual sample paths. Existing folder scans are backfilled when the database opens. |
| `sample_scan_history` | Sample-folder scan start/end, outcome, counts of considered/skipped/indexed files, errors, restart interruptions and the last completed audio-file checkpoint used to resume a cancelled or interrupted scan. |
| `sample_tags` | Materialized user tags attached to a sample path. |
| `metadata_tag_operations` | Hash-and-size-scoped user-tag add/remove operations with stable operation IDs for portable folder sync. |
| `metadata_tag_observed_adds` | Add-operation IDs observed by each removal, allowing an independent later re-add to survive merge. |
| `metadata_sync_state` | Device-local persistent identity for opt-in metadata folder sync. |
| `sample_classification_tags` | Automatic descriptive tags produced by the classifier, stored separately from user-edited tags and searchable in the browser. |
| `sample_search_fts` / `sample_search_values` | FTS5 trigram index and source view for substring search across sample names, paths, user/classification tags, source names/identifiers, plugin instruments and kits. Queries shorter than three characters use the compatibility scan path. Triggers keep the index synchronized with supported metadata writes. |
| `plugin_instruments` | Discovered plugin instrument identity, format, vendor, category, install reference and version. The format-independent JUCE identifier is unique. |
| `plugin_kits` | Kit/preset records tied to a discovered instrument, including a place for user-approved saved plugin state. |
| `plugin_sounds` | Captured note/velocity sounds tied to a kit, with silence flag, preview reference, duration and spectral measurements including centroid/flatness and transient attack count, pitch fields, classification, exact and acoustic fingerprints. |
| `plugin_velocity_layer_candidates` | Versioned, heuristic timbral-change candidates between adjacent sampled velocity levels for a MIDI note and kit. |
| `plugin_confirmed_velocity_layers` | User-confirmed, non-overlapping MIDI-note velocity ranges, stored separately from heuristic boundary candidates. |
| `plugin_round_robin_takes` | Each captured note/velocity repeat, including silence, preview path and acoustic fingerprint. |
| `plugin_round_robin_candidates` | Versioned heuristic candidates where repeated triggers have distinct acoustic fingerprints. |
| `plugin_confirmed_round_robin_takes` | The exact captured takes the user has confirmed as one repeated-trigger group; membership is removed automatically when those takes are rescanned or deleted. |
| `mapping_profiles` | Named semantic MIDI mapping profiles. |
| `mapping_entries` | Instrument/articulation to MIDI-note mappings within a profile. |
| `schema_info` | Single-row schema version marker, currently version 19. |

Database APIs can upsert plugin instruments, named kits with opaque plugin state, recall saved kit state, note/velocity sound captures with metadata and versioned velocity-boundary candidates. Manual category corrections to generated previews update both their sample row and the linked plugin sound. The desktop scanner populates kits and captures. Plugin discovery persists in `plugin_instruments` and in a JUCE XML cache used by the plugin list.

## Planned additions

Confirmed velocity ranges and round-robin membership now have distinct, validated database records, migration coverage, and a user-facing review workflow. Velocity-boundary and repeated-trigger variation candidates remain heuristics, not proof of internal instrument behavior; confirmations must come from listening review and still need evaluation against known instruments.
