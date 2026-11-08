# Semantic MIDI model

MagDrum represents a drum event with a semantic instrument and articulation, velocity, beat position and duration. MIDI note numbers are applied only at profile boundaries.

The current core has a General MIDI profile for kick, snare, hi-hat, toms, ride, crash, clap, rimshot and percussion. Conversion to a target profile returns both the note and whether an articulation fallback was used. Callers can disable fallback and report unsupported articulations instead of silently substituting them.

`MidiMappingProfile` also accepts a custom entry list, and named profiles can be saved to and loaded from SQLite. `MidiPatternIO` imports/exports Standard MIDI Files using PPQ timing, tracks note-on/off pairs, preserves tempo and time-signature metadata, reports unmapped notes, and emits unsupported events from export when no mapping is available.

Editing profiles in the app is not implemented yet. The first importer supports PPQ time division; SMPTE-timed files are reported as unsupported.

## Current limitations

- GM mappings are a starter profile, not a guarantee that every instrument follows GM exactly.
- Reverse conversion resolves duplicate note assignments to the first matching semantic event.
- The standalone browser does not yet expose MIDI event editing or MIDI file workflows.
