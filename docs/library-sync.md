# Cross-device library synchronization investigation

## Status

The SQLite database remains local to each device; MagDrum does not provide a cloud account or service. Two explicit opt-in workflows are available from the standalone and VST sample-library views: import/export a metadata archive, or sync through a folder managed by a service the user already uses (for example, a cloud-drive folder or a shared local folder).

## Portable metadata archive

The desktop app can export and merge `com.magdrum.library-metadata` version 1 JSON archives. The archive contains SHA-256 and size identities, manual categories, user tags and named MIDI mapping profiles. It never includes local paths, audio files, scan measurements, plugin state blobs or captured audio.

Import matches scanned local files by exact content hash and size. It unions user tags, fills an empty manual category, imports missing mapping profiles, and reports identities that are unavailable on the receiving device. Existing conflicting categories and mapping profiles are left untouched and counted in the import report. Re-importing the same archive is idempotent for tags and profiles. Version 1 manual archives represent tag additions only. Folder-sync snapshots use sync version 2 and carry add/remove operations; older version 1 folder snapshots remain additive for compatibility. The JSON is plaintext; users must place it in a folder or storage they trust. It is not uploaded by MagDrum.

Folder sync stores one `MagDrum-sync-<device-id>.json` snapshot per database/device in the selected folder. Sync version 2 records tag additions and observed removals as operation IDs matched to local samples by hash and size. A removal only cancels additions the device had observed when the user removed the tag, so a concurrent independent re-add remains. Each run validates all discovered archives before changing metadata, then merges the other device snapshots in one SQLite transaction. Version 2 checks every removal reference against an add operation for the same hash, size and tag; malformed references reject the whole merge. Manual-category and profile conflicts are preserved, and the updated local snapshot is staged before committing. The stable device ID is stored in the local SQLite database, not in the shared folder. A maximum of 128 device archives, 128 MB per archive and 256 MB total is accepted. Keep the folder within a trusted, provider-managed location; MagDrum does not validate when the provider has finished uploading or downloading files, so sync again after the provider reports that it is up to date. If the final snapshot replacement fails after the database commit, the UI reports that metadata merged but the snapshot needs a retry.

Two-database smoke tests cover matching identical audio at different paths, preserving local tags, importing a profile, retaining incoming conflict values and source device IDs, applying chosen category/profile values, rejecting stale local state, and rejecting unsupported archive versions. Folder-sync smoke coverage checks distinct persistent device IDs, bidirectional tag/profile metadata exchange, incoming conflict choices from each device, removal propagation, concurrent re-adds, malformed cross-tag references and rollback, conflict preservation, path exclusion, and idempotent repeated syncs.

## Data that can be synchronized

The currently synchronized state is compact metadata and user intent:

- semantic MIDI mapping profiles and their edits;
- manual sample categories and user tags;
- user-approved kit names and plugin identifiers are reserved for a later schema revision;
- browser preferences and saved searches are not currently synchronized.

Audio sources remain where the user installed or recorded them. Scanned sample locations, app-group URLs, plugin binaries, plugin state blobs, and captured previews can contain machine-specific paths or licensed content; they must stay local by default. A device may match a local audio file to shared metadata by content SHA-256 and size, then bind it to its own local path. A missing match should remain a visible unavailable item rather than triggering an implicit download.

## Why SQLite should not be synchronized as a file

The current database uses local paths as the `samples` primary key and includes device-local scan history and file timestamps. Concurrently copying or replacing the live SQLite file risks journal inconsistency, lost edits, and path collisions between devices. Cross-device identity therefore needs to be separated from file location before a sync implementation: stable IDs for logical sample content and user metadata, with one or more per-device location records. That requires an additive schema migration and a backfill from existing hashes and paths.

## Proposed design

1. **Separate identity from location.** Add stable IDs for logical samples, sources, mapping profiles and plugin kits. Store local path, security-scoped bookmark, file URL, modification time, and availability in device-local location tables. A sample's content ID can use its SHA-256; generated plugin previews need a distinct local-only identity unless a user explicitly chooses to share them.
2. **Record user changes as versioned operations.** Maintain a local transactional outbox containing an operation UUID, entity UUID, device UUID, monotonic device sequence, schema version, operation type, and compact payload. Apply remote operations idempotently and checkpoint each device's received sequence. Keep scan measurements and scan-history rows local; they can be regenerated.
3. **Merge by data meaning.** Folder sync version 2 merges independent tag additions/removals as an observed-remove set; this is implemented and covered by two-database tests. Concurrent manual-category and mapping-profile edits preserve the local value and return the incoming values with their source device IDs. Both desktop interfaces present these choices; applying one rechecks the current local value and fails if it changed after the conflict was shown. Profile renames and deletes also retain conflict/tombstone records until every linked device has received them.
4. **Keep transport behind an interface.** The core should operate on exportable operation batches and checkpoints. A later adapter may use a user-selected folder or chosen cloud API, but it must not share a live SQLite file. Transport must support retry, offline edits, deduplication, schema negotiation and explicit account disconnect/data deletion.
5. **Make sharing opt-in.** Folder sync is started manually, requires the user to choose a folder, and only shares tags, manual categories and MIDI mapping profiles. It does not copy audio samples, local paths, plugin state, or captured commercial-plugin audio. Direct cloud-provider APIs, automatic scheduling and transfer-state reporting remain future work.

## Verification gates before a service is implemented

- Two isolated databases converge after bidirectional edits, repeated delivery, offline work, and out-of-order batches.
- Conflicting mapping edits remain visible and do not silently change MIDI output.
- A sample with the same content on two devices binds to both local paths without copying audio; missing or changed files are reported.
- Large libraries sync metadata in bounded pages and never block the audio callback or a scan worker.
- [x] User-tag removals survive reconnects and do not erase concurrent re-adds; covered by two-database folder-sync tests.
- Mobile app-group storage and security-scoped file access remain local; sync does not assume unrestricted iOS filesystem access.
- Provider data can be exported and deleted, and credentials are stored using platform credential storage.
- No third-party sample or plugin audio is transmitted without a separate explicit user choice and applicable license review.

## Current limitations and remaining decisions

The current transport is a user-selected folder from either desktop interface. There is no provider account integration, automatic background schedule, encryption layer, collaborator/retention model, or confirmation that a cloud-drive client has finished propagating files. Manual version 1 archive import/export remains additive for tags; folder sync version 2 propagates user-tag removals and preserves concurrent re-adds. Concurrent category or profile edits are reviewed in both desktop interfaces. Users can apply an incoming value or keep the local value; stale local edits reject the selection. Do not store the sync folder in an untrusted location. The versioned metadata archive remains available for backup and manual transfer.
