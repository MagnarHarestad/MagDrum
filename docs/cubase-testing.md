# Verify MagDrum in Cubase Pro 15

Use the Windows x64 package for Cubase Pro 15 on a standard Intel/AMD Windows PC. The ARM64 package has not been run on native Windows ARM64.

## Install and load

1. Download `MagDrum-Windows-x64.zip` from the latest MagDrum test release and extract it.
2. Close Cubase completely, extract the ZIP, and double-click `Install-MagDrum.cmd`. Approve the standard Windows UAC prompt and confirm replacement when asked. The installer backs up the previous bundle outside VST3 scan folders and includes the matching `MagDrum.exe` helper in the new bundle. If the installer reports `Cubase15.exe` still running, check Task Manager's **Details** tab; do not proceed until Cubase has exited.
3. Start Cubase 15. Open **Studio > VST Plug-in Manager**, then choose **Rescan All**. Confirm that **MagDrum** appears in the instrument list. Select it and verify its version matches the release tag; for `v0.1.2-test.195`, the VST3 version should read `0.1.2.195`. Confirm Cubase shows **Default Kit** as the program name. If Cubase exposes a rename control, rename it, save and reopen the project, then confirm the name persists.
4. Create an instrument track with MagDrum and open its editor.

## Use the plug-in views

1. **Sample Library:** click **Add sample folder** and choose your drum library folder. The VST scans it in the background and shows progress in its status line; scanning can be cancelled and unchanged files are skipped. When the scan finishes, find, filter and audition samples in the list or sample map.
2. **Plugin Scanner:** open the **Plugin Scanner** view inside the VST. Click **Scan plugin folders**, select an instrument, then scan its exposed programs or import a `.vstpreset` as a named kit. Configure MIDI notes and velocity levels, then click **Scan kit notes**. The isolated helper performs discovery and rendering outside Cubase; captured samples should appear in the VST's sample library with source, kit, note and velocity details. Use **Plugin instruments** only when you need to interact with a plugin-specific editor.
3. **Drum Machine:** select one of the named General MIDI pads. Load a WAV directly or assign a selected library sample. The panel at right shows the pad's waveform and velocity layers. Click the pad to audition it.
4. **Sequencer and Cubase transport:** choose a pattern length from 1 to 16 bars and enter hits on the eight category-specific lanes. With Cubase stopped, press **Play** in MagDrum and confirm the pattern previews using the samples assigned to its MIDI notes. Stop it, start Cubase playback from a known bar, then press **Play** in MagDrum; confirm the pattern follows the host position and tempo, loops at its selected length, and stops cleanly. Change Cubase's tempo during playback and check that the sequencer follows it without duplicate or stuck notes. Stop Cubase while MagDrum's Play button remains active; confirm MagDrum returns to its local preview instead of staying idle. Export the pattern, import it into a Cubase MIDI track, then reimport it in MagDrum and compare the hits, length and tempo/meter events.
5. To test external MIDI input, create or play a Cubase part that sends note 36 with velocity 100. Cubase's displayed octave name for note 36 depends on its MIDI octave-numbering preference, so verify the MIDI note number if no sound plays. Test a mapped snare note and one unmapped note as well; confirm the correct sample plays and the unmapped note stays silent.

## Check project state and velocity layers

1. Save the Cubase project, close and reopen it, and confirm the sample assignments remain in MagDrum.
2. Load another sample on the same note for velocity 1–63 and keep the first sample on 64–127. Send notes at velocities 40 and 90 and confirm each range selects its assigned sample. Adjust that pad's gain, pan and tune, save the Cubase project, then reopen it and confirm the assignments, controls and sequencer pattern return.
3. Confirm that editing the assignment does not interrupt audio while notes are playing. In the hamburger menu, switch through all eight language choices and check that page names, controls and status messages update without broken accented characters.

## Report the result

Record the Cubase Pro 15 build, Windows version and architecture, whether MagDrum appeared in the plug-in manager, sample format, MIDI note/velocity used, whether audio played, and whether the saved assignment returned after reopening. Include any plug-in scan warning or crash text.

These are manual host-compatibility checks. The automated Windows smoke tests load the built VST3 in a headless scanner, but they cannot substitute for Cubase Pro 15 validation.
