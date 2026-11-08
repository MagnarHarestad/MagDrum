# Roadmap

1. **Library foundation — in progress:** standalone JUCE app, recursive WAV/AIFF/FLAC decoding where supported, SQLite index, filename/folder and category filters, audition, cancellation and CI. Current analysis extracts duration, channels, sample rate, bit depth, peak, RMS, one-window spectral centroid, transient strength and file SHA-256. The hash identifies identical files; it is not a perceptual audio fingerprint.
2. **Audio intelligence — prototype:** deterministic low-confidence drum-family heuristics, manual category correction, user tags and feature-distance similarity query. Classification quality, feature coverage across short files, duplicate workflow, complete tag filters and confidence review still need work.
3. **Instrument scanning — not implemented:** desktop VST3 discovery/hosting, manual kit selection, MIDI-note rendering, audio capture and previews.
4. **MIDI compatibility — core started:** semantic drum events and GM profile conversion, including explicit fallback results. Custom profile persistence/editor, MIDI file import/export, tempo/signature preservation and mapping UI remain.
5. **Platform products — partial:** current VST3 source lets users load WAV/AIFF/FLAC-decoded samples onto MIDI notes and persists those paths in plugin state. Sample buffers are swapped without blocking the audio callback. The published test release predates the sample-player upgrade and must be rebuilt/tagged after CI verification. Standalone desktop app builds on Windows/macOS/Linux in CI. AU and AUv3 targets and the iOS application remain.
6. **Advanced analysis — not implemented:** model-assisted classification, articulations, velocity layers, synchronization and a step sequencer.

Desktop VST3 scanning is not an iOS requirement.
