# Classifier evaluation

MagDrum can evaluate its current classifier on a local audio collection organized into category folders. The evaluator analyzes supported audio files without copying or modifying them and reports per-class precision, recall, F1, overall accuracy and macro F1. It prints two reports: the normal filename-plus-audio result and an audio-only result that hides each filename before classification. Each report also shows the coverage and accuracy of categories at or above the app's 0.75 review threshold; `Unknown` remains unaccepted. This selective result reflects the current review rule, not calibrated probability.

Build the optional evaluator with the same CMake, JUCE, compiler and platform dependencies used by MagDrum:

```powershell
cmake -S . -B build/classifier-evaluation -A x64 `
  -DBUILD_TESTING=OFF -DMAGDRUM_BUILD_CLASSIFIER_EVALUATION=ON
cmake --build build/classifier-evaluation --config Release --target MagDrumClassifierEvaluation
```

Run it against the root of a local, labeled collection:

```powershell
& .\build\classifier-evaluation\MagDrumClassifierEvaluation_artefacts\Release\MagDrumClassifierEvaluation.exe `
  'C:\path\to\labeled-drums'
```

The label is inferred from the closest recognized parent folder. Supported folder names include `kick`/`kicks`, `snare`/`snares`, `hi-hats`, `open-hats`, `pedal-hats`, `claps`, `toms`, `rimshots`, `rides`, `crashes`, and `percs`/`percussion`. Other folders are skipped and counted as unlabeled. Keep each labeled folder limited to one category. A mixed or incorrectly labeled directory makes the resulting metrics misleading.

The evaluator is a measurement tool, not a quality guarantee or a trained model. Confidence is not calibrated, and a score on one collection does not establish performance on other libraries. It also indexes the labeled audio metadata into a temporary SQLite database and evaluates the production perceptual-candidate search at threshold 0.85, reporting how many queries return candidates, candidate category agreement, and top-candidate category agreement. Folder-category agreement is a weak label proxy and does not verify human-perceived similarity; listen to retrieved candidates for a real perceptual judgment. The temporary database is removed after each run, and source audio is not copied or modified. A synthetic smoke run with four gain variants of a single waveform successfully exercised candidate retrieval: all 4 queries returned candidates, 4 of 12 results matched folder categories (33.3%), and the top result matched for 0 of 4 queries. The fixture deliberately reuses one waveform across two categories, so these values only verify the report and candidate-query path; they are not evidence of real-library or perceptual accuracy.

One possible small, permissively licensed smoke corpus is [Boochi44/free-drum-samples](https://github.com/Boochi44/free-drum-samples), whose README labels three curated kits with instrument folders and states that the files are CC0. Its kits cover a narrow hip-hop/trap sound palette, and many filenames contain their class; review both reports and treat the results as a limited diagnostic. The corpus is external evaluation data: do not add it to MagDrum source, test fixtures, installers or release assets. The evaluator does not download or redistribute datasets.

## First local measurement

On 2026-11-09, the evaluator scanned revision `77ba31428a079dd8f17c8e144c1e649ea0a198b3` of that corpus: 57 WAV files decoded, 44 files had recognized category-folder labels, and 13 files in other folders were excluded. There were no decode errors. Filename-plus-audio classification scored 68.2% accuracy and 0.697 macro F1; 34/44 (77.3%) outputs passed the review threshold, and those accepted outputs were 82.4% accurate. Audio-only classification scored 25.0% accuracy and 0.195 macro F1, with 0/44 outputs accepted at the threshold. In the audio-only report, Kick, Clap, Open hi-hat and Percussion had zero recall; Closed hi-hat recall was 1.0 and Snare recall was 0.667. Production similarity search returned 36 candidates for 17/44 queries; 20/36 (55.6%) shared the query folder category, and the top candidate matched for 12/17 queries with results (70.6%). This is a weak folder-label proxy and not a listener judgment. These results are evidence that the current heuristic is not ready to claim broad audio-only classification quality. The corpus is small, processed and class names often appear in filenames, so the measurements are directional rather than representative of acoustic drum libraries generally.

## Second local measurement

On 2026-11-09, the evaluator scanned 128 FLAC recordings from the CC0 Versilian Virtuosity Drums kit. The labels were assigned to evaluation folders from the kit's SFZ region-family codes (`hhc`, `hho`, `hhp`, `kick`, `ride`/`ridebell`, `sidestick`, `snare`, `tomhi` and `tomlo`). All 128 files decoded without errors. After adding these common SFZ codes to the filename heuristics, filename-plus-audio classification scored 100.0% accuracy and 1.000 macro F1 on this kit. This score measures successful recognition of its encoded family names and does not show that the waveform classifier can distinguish these classes.

With filenames hidden, audio-only classification scored 14.1% accuracy and 0.083 macro F1, and 0/128 outputs passed the app review threshold. Kicks were the only reliably recognized family (16/16); closed hats, open hats, pedal hats, rides and rimshots were mostly or entirely called Snare, while toms were usually called Kick. This is a clear failure of the current acoustic heuristics on this dataset. Together with the first corpus, the results support keeping filename cues as useful metadata while treating audio-only categories as low-confidence suggestions for review, not as dependable automatic labels. The kit represents one recorded contemporary jazz kit and is not a broad benchmark; its files remain external evaluation data and are not included in MagDrum.

## Third local measurement

On 2026-11-09, revision `9f04cf9a734527edfbb0a4eee1f674e45bbf71bc` of the CC0 [Virtuosity Drums](https://github.com/sfzinstruments/virtuosity_drums) library was evaluated. A temporary labeled subset contained 121 WAV/FLAC samples across Kick (14), Snare (14), Closed hi-hat (14), Open hi-hat (12), Pedal hi-hat (12), Tom (14), Ride (14), Crash (11), and Percussion (16). Labels followed the library's source folders and SFZ sample names; multiple mic captures and velocity/repetition variants come from the same kit. All 121 files decoded. Filename-plus-audio classification scored 75.2% accuracy and 0.737 macro F1; 91/121 suggestions passed the review threshold and all accepted suggestions matched the folder labels. With filenames hidden, it scored 2.5% accuracy and 0.007 macro F1, with 0/121 accepted. This is another warning that audio-only classification is not reliable; this selected subset is one kit, not an independent or genre-diverse benchmark.

On the same 121 files, production similarity search returned 235 candidates for 95/121 queries. Of the returned candidates, 211/235 (89.8%) matched the query folder category; the top candidate matched for 88/95 queries with results (92.6%). The result is likely influenced by related velocity and round-robin recordings within the same kit. It shows category agreement for this particular sample set, not human-perceived similarity or general retrieval quality. The temporary labeled copy and downloaded library were kept outside the MagDrum repository and release assets.
