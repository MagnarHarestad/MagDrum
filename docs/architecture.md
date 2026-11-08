# Architecture

MagDrum separates library indexing, audio analysis, MIDI semantics and platform adapters. Phase 1 is a standalone JUCE app: recursive WAV scans store file references and basic metadata in SQLite; the browser filters and auditions indexed files.

SQLite is compiled from its official amalgamation release and wrapped by `SampleDatabase`. The scanner leaves source files untouched. Classification is injected through `DrumClassificationModel`; the current deterministic heuristic implementation is the default, and model outputs are checked against known categories and confidence bounds. Plugin hosting and scanning run outside the real-time audio callback. Shared core code is intended to serve platform-specific application and plugin wrappers.
