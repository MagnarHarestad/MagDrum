# Semantic MIDI model

MagDrum represents a drum event with a semantic instrument and articulation, velocity, beat position and duration. MIDI note numbers are applied only at profile boundaries.

The current core has a General MIDI profile for kick, snare, hi-hat, toms, ride, crash, clap, rimshot and percussion. Conversion to a target profile returns both the note and whether an articulation fallback was used. Callers can disable fallback and report unsupported articulations instead of silently substituting them.

`MidiMappingProfile` also accepts a custom entry list, and named profiles can be saved to and loaded from SQLite. `MidiPatternIO` imports/exports Standard MIDI Files using PPQ timing, tracks note-on/off pairs, preserves tempo and time-signature metadata, reports unmapped notes, and emits unsupported events from export when no mapping is available.

The standalone app provides MIDI import/export buttons, save/load controls for named profiles, entry set/remove controls by instrument and articulation, and CSV profile import/export. The CSV format has the header `instrument,articulation,note`; instrument and articulation names are case-sensitive enum names such as `HiHat,Closed,42`. Invalid names, duplicate semantic entries and notes outside 0–127 are rejected. Import replaces the active profile and saves it by the CSV filename.

After importing a MIDI file, open **Edit pattern** to review drum events and change instrument, articulation, velocity, beat position and duration, or delete an event. The editor sorts events by beat position after edits; tempo and time-signature metadata remain attached to the pattern for export.

When MIDI import encounters unmapped notes, the app lists each note number and how many note events used it. MIDI export lists the semantic events that were omitted because the active profile did not map them.

## Current limitations

- GM mappings are a starter profile, not a guarantee that every instrument follows GM exactly.
- Reverse conversion resolves duplicate note assignments to the first matching semantic event.
- The editor covers semantic drum note events. It does not yet edit tempo or time-signature events or show a side-by-side MIDI round-trip diff.
