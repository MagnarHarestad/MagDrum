# MagDrum Windows plug-in views

The Windows VST3 editor is organized into four views. Open MagDrum on an instrument track in Cubase and choose a view from the navigation row.

## Sample Library

Click **Scan installed libraries** to scan audio in detected default Native Instruments, Spectrasonics/STEAM and Steinberg/Cubase folders. The scan runs in the background across the folders it finds, can be cancelled, skips unchanged files, saves checkpoints, and resumes after an interrupted scan. It also searches those folders for drum-related `.mid` and `.midi` files; open **Drum MIDI (N)** in the sequencer to inspect and import a result. Use **Add sample folder** when your audio libraries are stored in custom locations. The audio index reads supported WAV, AIFF and FLAC files; Steinberg VST Sound archives are not unpacked. The resulting sample index is shared with the MagDrum desktop app. Search by text/tags or filter by category, source, kit, duration, brightness and review confidence. Choose a sort order, then use **More samples** to page through large libraries. The list shows category, duration and measured brightness; the map positions samples by duration and brightness and colors them by category. Click a point or list row to select a sound, then use **Preview** (or double-click a list row) to audition it. Select a pad in **Drum Machine** first, return to the library, and choose **Assign to pad** to load the selected sample on that MIDI note. **Sample tools** provides category and tag editing, scan history, similarity and duplicate searches, waveform comparison, and portable metadata import/export.

## Drum Machine

The 4×4 pad grid follows the Boss DR-660 layout: crashes and ride sounds on the top row, toms next, side stick/clap/hi-hats on the third row, then Kick, Snare, Pedal HH and Cowbell along the bottom. The bottom-left Kick sends MIDI note 36; Snare sends 38, Closed HH 42, Open HH 46, Pedal HH 44 and Cowbell 56. Click a pad to select it and audition its assigned sound. The panel beside the grid shows the sample waveform, file path and assigned velocity layers. **Load WAV** loads a local audio file directly; **Clear pad** removes all layers from that note. To assign a library sample, select the pad first, then return to **Sample Library** and use **Assign to pad**. Velocity ranges are inclusive, from 1 to 127, and may not overlap.

## Sequencer

The grid has sixteen selectable bars (256 sixteenth-note steps total) with dedicated lanes for Kick (36), Snare (38), Closed Hat (42), Open Hat (46), Tom (43), Ride (51), Crash (49) and Clap (39). Each lane is permanently tied to its General MIDI note, preventing drum categories from being accidentally swapped. Right-click a step to set its velocity. Set tempo from 30 to 300 BPM. **Play** loops the 16-bar pattern through assigned samples. **Import MIDI** opens a file picker; **Drum MIDI (N)** lists patterns discovered by the library scan. Imports use the selected profile and report unmapped, ambiguous and out-of-range notes. **Export MIDI** writes the selected 16-bar, 4/4 pattern including step velocities. Steps with no sample assigned remain silent during preview and will play if Cubase receives MIDI for a loaded note.

## MIDI Maps

Choose General MIDI or a saved profile, edit drum/articulation-to-note mappings, and import or export profiles as CSV. Saved profiles are stored in the shared MagDrum sample database and the selected profile is also stored with the Cubase plug-in state. The current sequencer pattern and tempo are stored with that state too. Profiles apply to MIDI file import/export; the drum-machine pads and incoming Cubase MIDI remain on their named General MIDI notes so assigning a sample to a pad does not change its playing note.

## Install and rescan

For the current user's plug-in folder, run `Install-MagDrum.ps1` from the extracted package. To install or replace the system-wide VST3 at `C:\Program Files\Common Files\VST3\MagDrum.vst3`, close Cubase, run PowerShell as administrator, and use `Install-MagDrum.ps1 -AllUsers`. Start Cubase and choose **Studio > VST Plug-in Manager > Rescan All**.
