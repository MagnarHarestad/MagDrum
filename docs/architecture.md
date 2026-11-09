# Architecture

MagDrum separates library indexing, audio analysis, MIDI semantics and platform adapters. Phase 1 is a standalone JUCE app: recursive WAV scans store file references and basic metadata in SQLite; the browser filters and auditions indexed files.

SQLite is compiled from its official amalgamation release and wrapped by `SampleDatabase`. The scanner leaves source files untouched. Classification is injected through `DrumClassificationModel`; the current deterministic heuristic implementation is the default, and model outputs are checked against known categories and confidence bounds. Plugin note scans run in a child process so third-party plugin crashes do not take down the main app; audio rendering also stays outside the real-time callback. Shared core code is intended to serve platform-specific application and plugin wrappers.

The standalone app writes structured JSON Lines events for application startup/shutdown and sample/plugin scan outcomes to the user's application-data `MagDrum/logs/magdrum.jsonl` file. The active log is capped at 2 MiB; one previous file is retained as `magdrum.previous.jsonl`.

The sequencer preview uses a positionable WAV source that wraps at the rendered file boundary when looping is enabled. Its loop flag is atomic so the GUI can change loop mode while the audio thread is reading without a lock or allocation on that thread.
