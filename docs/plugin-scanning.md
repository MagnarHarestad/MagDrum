# Scanning sounds from an instrument plugin

The desktop app can discover installed audio plugins using JUCE's plugin list. Use the list's options to scan default locations or add a custom folder. MagDrum keeps the discovered plugin inventory in its local SQLite database and caches JUCE's plugin descriptions in the user's application data folder.

## Capture a kit

1. Select an instrument plugin and load it. MagDrum opens the plugin's own editor.
2. Choose a kit or preset in that editor. MagDrum does not assume that the plugin exposes a list of presets.
3. Close the editor, enter a kit name, and save the current plugin state. The state can later be recalled by loading the same instrument and choosing that kit name.
4. Choose velocity levels, note duration, release time and total capture length, then start the scan. MagDrum sends MIDI notes 0–127 at each selected velocity and renders the output offline.
5. MagDrum records silent notes, analyzes audible output, writes WAV previews under its application data directory and indexes the previews in the sample browser. The original plugin state is restored after the render.
6. After a scan, use **View layer candidates** with the same kit name to review large acoustic-fingerprint changes between adjacent sampled velocities. The report is a listening aid, not proof of an instrument's internal sample-layer boundaries. Scan at two or more velocity levels to produce comparisons.

The scan runs away from the real-time audio callback. Cancellation takes effect between MIDI notes. Some plugins may not render correctly offline or may not provide serializable state. Plugin scanning currently runs in-process; process isolation and recovery after a plugin crash remain future work. Preview WAVs stay local and are not part of MagDrum's release packages.
