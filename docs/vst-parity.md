# Standalone-to-VST functionality parity

The VST is intended to expose the MagDrum workflows from inside Cubase. This checklist tracks parity without claiming that the standalone app and an instrument plug-in have identical host responsibilities.

| Standalone workflow | VST status | VST implementation target |
| --- | --- | --- |
| Recursive sample-folder scan, file analysis, cancellation, resume, scan history | Implemented | The VST scans in a background worker, shares the same database, reports progress and scan results, and exposes scan history. |
| Sample search, category/source/kit filters, feature bands, confidence review and sorting | Partial | Keep filters and paging responsive in the VST sample library. |
| Sample audition, waveform, category/tag edits, source-kit access, sample comparison | Partial | Preview, category and tag editing, and two-file waveform comparison are in the VST. Source-kit access remains. |
| Exact and perceptual duplicate lookup, similarity candidates | Implemented | The Sample tools menu reports exact duplicates, acoustic-similarity and perceptual near-duplicate candidates. |
| Plugin-instrument discovery, presets, saved states, MIDI-note/velocity scans, round-robin and layer review | Not ported | Provide an in-plugin scan workspace using isolated workers. Use host-driven rendering where an independent audio device is unavailable. |
| Semantic MIDI import/export, event editor, ambiguity/fallback reports, GM/custom profiles and CSV profile import/export | Partial | Add a MIDI workspace and persist selected mapping profiles with the Cubase project. |
| Multi-bar, meter-aware sequencer, editable velocity, tempo/signature metadata, preview and loop | Partial | Extend the VST sequencer while keeping its lane-to-note mapping explicit and safe. |
| Portable metadata archive import/export | Implemented | The Sample tools menu imports and exports the shared tag/category/mapping-profile archive without copying audio. |

The VST cannot take ownership of Cubase's audio device. Any standalone action that currently opens or configures a device must use the DAW's playback/render callbacks or a separate worker instead. Windows x64 is the first target for parity work; other plug-in formats must be rebuilt and reviewed after the Windows behavior settles.
