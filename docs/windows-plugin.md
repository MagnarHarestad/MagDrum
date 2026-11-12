# MagDrum Windows plug-in views

The Windows VST3 editor is organized into four views. Open MagDrum on an instrument track in Cubase and choose a view from the navigation row.

## Sample Library

Click **Add sample folder** to choose a folder and scan it directly from the VST. Scanning runs in the background, can be cancelled, skips unchanged files, saves checkpoints, and resumes after an interrupted scan. The resulting index is shared with the MagDrum desktop app. Search by text/tags or filter by category, source, kit, duration, brightness and review confidence. Choose a sort order, then use **More samples** to page through large libraries. The list shows category, duration and measured brightness; the map positions samples by duration and brightness and colors them by category. Click a point or list row to select a sound, then use **Preview** (or double-click a list row) to audition it. Select a pad in **Drum Machine** first, return to the library, and choose **Assign to pad** to load the selected sample on that MIDI note. **Sample tools** provides category and tag editing, scan history, similarity and duplicate searches, waveform comparison, and portable metadata import/export.

## Drum Machine

The 4×4 pad grid uses named General MIDI drum notes, so a Kick pad always addresses MIDI note 36, Snare 38, Closed Hat 42, Open Hat 46, and so on. Click a pad to select it and audition its assigned sound. The panel beside the grid shows the sample waveform, file path and assigned velocity layers. **Load WAV** loads a local audio file directly; **Clear pad** removes all layers from that note. To assign a library sample, select the pad first, then return to **Sample Library** and use **Assign to pad**. Velocity ranges are inclusive, from 1 to 127, and may not overlap.

## Sequencer

The 16-step grid has dedicated lanes for Kick (36), Snare (38), Closed Hat (42), Open Hat (46), Tom (43), Ride (51), Crash (49) and Clap (39). Each lane is permanently tied to its General MIDI note, preventing drum categories from being accidentally swapped. Set tempo from 30 to 300 BPM. **Import MIDI** maps a MIDI file into the grid using the selected profile; it reports unmapped, ambiguous and out-of-range notes. **Export MIDI** writes the current one-bar, 4/4 pattern using that profile. Steps with no sample assigned remain silent during preview and will play if Cubase receives MIDI for a loaded note.

## MIDI Maps

Choose General MIDI or a saved profile, edit drum/articulation-to-note mappings, and import or export profiles as CSV. Saved profiles are stored in the shared MagDrum sample database and the selected profile is also stored with the Cubase plug-in state. The current sequencer pattern and tempo are stored with that state too. Profiles apply to MIDI file import/export; the drum-machine pads and incoming Cubase MIDI remain on their named General MIDI notes so assigning a sample to a pad does not change its playing note.

## Install and rescan

For the current user's plug-in folder, run `Install-MagDrum.ps1` from the extracted package. To install or replace the system-wide VST3 at `C:\Program Files\Common Files\VST3\MagDrum.vst3`, close Cubase, run PowerShell as administrator, and use `Install-MagDrum.ps1 -AllUsers`. Start Cubase and choose **Studio > VST Plug-in Manager > Rescan All**.
