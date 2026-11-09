# Cross-device library synchronization investigation

## Status

Cross-device synchronization is investigated but not implemented. MagDrum's SQLite database remains local to each device. No cloud provider, account system, or synchronization service has been selected.

## Data that can be synchronized

The useful shared state is compact metadata and user intent:

- semantic MIDI mapping profiles and their edits;
- manual sample categories and user tags;
- user-approved kit names and plugin identifiers;
- optional browser preferences and saved searches.

Audio sources remain where the user installed or recorded them. Scanned sample locations, app-group URLs, plugin binaries, plugin state blobs, and captured previews can contain machine-specific paths or licensed content; they must stay local by default. A device may match a local audio file to shared metadata by content SHA-256 and size, then bind it to its own local path. A missing match should remain a visible unavailable item rather than triggering an implicit download.

## Why SQLite should not be synchronized as a file

The current database uses local paths as the `samples` primary key and includes device-local scan history and file timestamps. Concurrently copying or replacing the live SQLite file risks journal inconsistency, lost edits, and path collisions between devices. Cross-device identity therefore needs to be separated from file location before a sync implementation: stable IDs for logical sample content and user metadata, with one or more per-device location records. That requires an additive schema migration and a backfill from existing hashes and paths.

## Proposed design

1. **Separate identity from location.** Add stable IDs for logical samples, sources, mapping profiles and plugin kits. Store local path, security-scoped bookmark, file URL, modification time, and availability in device-local location tables. A sample's content ID can use its SHA-256; generated plugin previews need a distinct local-only identity unless a user explicitly chooses to share them.
2. **Record user changes as versioned operations.** Maintain a local transactional outbox containing an operation UUID, entity UUID, device UUID, monotonic device sequence, schema version, operation type, and compact payload. Apply remote operations idempotently and checkpoint each device's received sequence. Keep scan measurements and scan-history rows local; they can be regenerated.
3. **Merge by data meaning.** Merge independent tag additions/removals as a set with tombstones. For concurrently edited manual categories or the same mapping entry, preserve both versions and show a conflict for user resolution; do not silently select an incompatible MIDI articulation. Profile renames and deletes also retain conflict/tombstone records until every linked device has received them.
4. **Keep transport behind an interface.** The core should operate on exportable operation batches and checkpoints. A later adapter may use a user-selected folder or chosen cloud API, but it must not share a live SQLite file. Transport must support retry, offline edits, deduplication, schema negotiation and explicit account disconnect/data deletion.
5. **Make sharing opt-in.** Ask before sharing mapping/profile metadata or plugin kit records. Never upload audio samples or captured commercial-plugin audio by default. Show file counts, bytes, provider/account, last sync result and conflicts. Support an offline export/import archive as the first transport milestone.

## Verification gates before a service is implemented

- Two isolated databases converge after bidirectional edits, repeated delivery, offline work, and out-of-order batches.
- Conflicting mapping edits remain visible and do not silently change MIDI output.
- A sample with the same content on two devices binds to both local paths without copying audio; missing or changed files are reported.
- Large libraries sync metadata in bounded pages and never block the audio callback or a scan worker.
- Deletion tombstones survive reconnects and are not confused with a temporarily offline device.
- Mobile app-group storage and security-scoped file access remain local; sync does not assume unrestricted iOS filesystem access.
- Provider data can be exported and deleted, and credentials are stored using platform credential storage.
- No third-party sample or plugin audio is transmitted without a separate explicit user choice and applicable license review.

## Decisions still needed

Before implementation, choose whether synchronization is wanted for this product, which provider or user-controlled transport to support, whether sharing is private to one account or includes collaborators, and what encryption and retention behavior users expect. Until those choices are made, the recommended next step is a local metadata export/import format and two-database merge tests, not a cloud dependency.
