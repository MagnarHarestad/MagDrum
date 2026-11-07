# Architecture

MagDrum separates library indexing, audio analysis, MIDI semantics and platform adapters. Phase 1 is a standalone JUCE app: recursive WAV scans store references and basic metadata in SQLite; the browser filters and auditions indexed files.

The scanner leaves source files untouched. Plugin hosting and scanning will run outside the real-time audio callback. Shared core code is intended to serve platform-specific application and plugin wrappers.
