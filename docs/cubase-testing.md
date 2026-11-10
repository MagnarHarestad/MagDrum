# Verify MagDrum in Cubase Pro 15

Use the Windows x64 package for Cubase Pro 15 on a standard Intel/AMD Windows PC. The ARM64 package has not been run on native Windows ARM64.

## Install and load

1. Download `MagDrum-Windows-x64.zip` from the [v0.1.1-test.73 release](https://github.com/MagnarHarestad/MagDrum/releases/tag/v0.1.1-test.73) and extract it.
2. Close Cubase. Run the included `Install-MagDrum.ps1` from PowerShell. The default is a per-user VST3 install and does not require administrator access.
3. Start Cubase 15. Open **Studio > VST Plug-in Manager**, then choose **Rescan All**. Confirm that **MagDrum** appears in the instrument list.
4. Create an instrument track with MagDrum and open its editor.

## Play a sample

1. In MagDrum, choose the MIDI note to trigger. The editor opens on MIDI note 36.
2. Choose **Load sample** and select a WAV, AIFF or FLAC file. This loads the file directly; scanning it into the standalone library first is optional.
3. Create or play a MIDI part that sends note 36 with velocity 100. Cubase's displayed octave name for note 36 depends on its MIDI octave-numbering preference, so verify the MIDI note number if no sound plays.
4. Confirm that the sample is audible through the instrument track and that the MIDI activity/output meters respond.

## Check project state and velocity layers

1. Save the Cubase project, close and reopen it, and confirm the sample assignment remains in MagDrum.
2. Optionally load another sample on the same note for velocity 1–63 and keep the first sample on 64–127. Send notes at velocities 40 and 90 and confirm each range selects its assigned sample.
3. Confirm that editing the assignment does not interrupt audio while notes are playing.

## Report the result

Record the Cubase Pro 15 build, Windows version and architecture, whether MagDrum appeared in the plug-in manager, sample format, MIDI note/velocity used, whether audio played, and whether the saved assignment returned after reopening. Include any plug-in scan warning or crash text.

These are manual host-compatibility checks. The automated Windows smoke tests load the built VST3 in a headless scanner, but they cannot substitute for Cubase Pro 15 validation.
