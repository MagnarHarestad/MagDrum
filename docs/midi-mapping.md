# Semantic MIDI model

MagDrum represents a drum event with a semantic instrument and articulation, velocity, beat position and duration. MIDI note numbers are applied only at profile boundaries.

The current core has a General MIDI profile for kick, snare, hi-hat, toms, ride, crash, clap, rimshot and percussion. Conversion to a target profile returns both the note and whether an articulation fallback was used. MIDI export defaults to leaving unsupported articulations unmapped. The VST, desktop and mobile export controls offer three explicit policies: no fallback, use only the same instrument's default articulation, or use any mapped articulation from the same instrument. Each substitution is reported after export; events that remain unmapped are listed as omitted. Plug-in projects saved with the former on/off setting remain compatible: enabled maps to the broad same-instrument policy.

`MidiMappingProfile` also accepts a custom entry list, and named profiles can be saved to and loaded from SQLite. `MidiPatternIO` imports/exports Standard MIDI Files using PPQ timing, tracks note-on/off pairs, preserves tempo and time-signature metadata, reports unmapped notes and notes with multiple semantic mappings, and emits unsupported events from export when no mapping is available. Duplicate identical mapping rows do not create a false ambiguity.

The desktop app provides MIDI import/export buttons, save/load controls for named profiles, entry set/remove controls by instrument and articulation, and CSV profile import/export. The mobile app can choose and save named profiles, then set or remove a note for an instrument/articulation pair. Profiles are stored in SQLite, so desktop and mobile builds using the same MagDrum data location share them. The CSV format has the header `instrument,articulation,note`; instrument and articulation names are case-sensitive enum names such as `HiHat,Closed,42`. Invalid names, duplicate semantic entries and notes outside 0–127 are rejected. Desktop import replaces the active profile and saves it by the CSV filename.

After importing a MIDI file, open **Edit pattern** to review drum events and change instrument, articulation, velocity, beat position and duration, or delete an event. The editor sorts events by beat position after edits. Use **Compare with imported** to see the events and tempo/meter metadata removed from or added to the original imported pattern, including pattern-length changes. It also lists imported tempo and time-signature events and lets users set or remove either type at any non-negative beat without changing the other metadata events.

MIDI imports accept both PPQ beat-based and valid SMPTE absolute-time division. SMPTE event times are converted into musical beats using the file's tempo map (defaulting to 120 BPM when no initial tempo is present); the resulting patterns export in standard PPQ format. Invalid SMPTE frame-rate codes are rejected with an import error.

When MIDI import encounters unmapped notes, the app lists each note number and how many note events used it. When a MIDI note maps to multiple instrument/articulation pairs, the import report lists all candidates and explains that the first profile entry was used for those events. Edit the profile to make that note unique before importing if another meaning is intended. MIDI export lists the semantic events that were omitted because the active profile did not map them.

## Current limitations

- GM mappings are a starter profile, not a guarantee that every instrument follows GM exactly.
- Ambiguous note assignments are reported, but users must edit the profile and import again to choose another semantic interpretation.
- Deterministic tests round-trip 24 generated patterns and 1,536 events across custom mappings, velocities, PPQ-grid positions, tempo and time-signature changes. The editor diff has not yet been reviewed against a broad collection of third-party MIDI files or through DAW round-trips.
