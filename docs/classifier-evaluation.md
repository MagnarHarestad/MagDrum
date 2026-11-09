# Classifier evaluation

MagDrum can evaluate its current classifier on a local audio collection organized into category folders. The evaluator analyzes supported audio files without copying or modifying them and reports per-class precision, recall, F1, overall accuracy and macro F1. It prints two reports: the normal filename-plus-audio result and an audio-only result that hides each filename before classification. This exposes performance that could otherwise come from category words embedded in sample names.

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

The evaluator is a measurement tool, not a quality guarantee or a trained model. Confidence is not calibrated, and a score on one collection does not establish performance on other libraries. The initial synthetic smoke fixture verifies folder labeling, audio decoding and both reports; it is not evidence of real-library accuracy.

One possible small, permissively licensed smoke corpus is [Boochi44/free-drum-samples](https://github.com/Boochi44/free-drum-samples), whose README labels three curated kits with instrument folders and states that the files are CC0. Its kits cover a narrow hip-hop/trap sound palette, and many filenames contain their class; review both reports and treat the results as a limited diagnostic. The corpus is external evaluation data: do not add it to MagDrum source, test fixtures, installers or release assets. The evaluator does not download or redistribute datasets.

## First local measurement

On 2026-11-09, the evaluator scanned revision `77ba31428a079dd8f17c8e144c1e649ea0a198b3` of that corpus: 57 WAV files decoded, 44 files had recognized category-folder labels, and 13 files in other folders were excluded. There were no decode errors. Filename-plus-audio classification scored 68.2% accuracy and 0.697 macro F1; audio-only classification scored 25.0% accuracy and 0.195 macro F1. In the audio-only report, Kick, Clap, Open hi-hat and Percussion had zero recall; Closed hi-hat recall was 1.0 and Snare recall was 0.667. These results are evidence that this heuristic is not ready to claim broad audio-only classification quality. The corpus is small, processed and class names often appear in filenames, so the measurements are directional rather than representative of acoustic drum libraries generally.
