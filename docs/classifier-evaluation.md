# Classifier evaluation

The evaluator scans and classifies labeled files under the supplied root, then runs
the quadratic manual-example and perceptual-similarity checks on a deterministic,
category-balanced subset of at most 200 samples by default. This keeps large local
libraries from causing an unbounded all-pairs workload. Use `--expensive-limit N`
to select a different subset size from 1 to 500. The initial scan still decodes the
full labeled corpus, while the expensive comparisons use only the reported subset.
The subset is chosen evenly through each category's path-sorted scan order; it is
repeatable for an unchanged library but is not a statistically independent test set.

## Local Battery library diagnostic

On 2026-11-20, a temporary subset of 300 locally installed NI Battery Factory
Library samples was evaluated: 30 each for Kick, Snare, Tom, Clap, Closed hi-hat,
Open hi-hat, Pedal hi-hat, Crash, Ride and Percussion. All 300 decoded and received
folder labels after adding `Closed hi-hat` and `Pedal hi-hat` directory aliases.
The 100-sample expensive subset was balanced across the 10 categories. Filename plus
audio features scored 90.3% accuracy, while audio-only features scored 12.0% accuracy
and 0.123 macro F1. Manual-example leave-one-sample-out scored 25.0%; similarity
candidate folder-category agreement was 44.4% (24/54), with 19 queries returning
candidates. These results show that filename evidence drives the strong combined
score and the current audio-only and similarity results are weak. Folder names are
only proxy labels, this is one proprietary library, and the subset is small; it is
not a human listening test or a representative benchmark. Audio was read from its
installed location and was not copied into the repository or release.

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

Production classification can use an exact drum-category folder name among the nearest five parent folders as an independent hint. The evaluation tool removes parent folders before scoring either filename-plus-audio or audio-only results, so a labeled category directory cannot leak its answer into the reported metrics.

Pass `--feature-summary` after the dataset root to print each labeled category's median and interquartile range for duration, spectral centroid, spectral flatness, transient-to-RMS ratio, attack count, pitch and pitch confidence. This helps identify where measured features overlap before adjusting classification rules; the summary is descriptive and does not select or validate thresholds.

The optional user-example classifier uses only samples whose category the user manually corrected. It can suggest a category for an otherwise low-confidence scan when at least two nearby examples agree; the suggestion remains below 75% confidence and never replaces a manual category. Matching uses seven normalized scalar measurements and, when available, the versioned four-window/16-band acoustic fingerprint. The evaluator also measures this model by hiding each query filename and testing against the remaining folder-labeled samples, plus a leave-one-kit-out split when the folder structure identifies multiple kits. Evaluation folder labels stand in for manual corrections, so these results do not show the behavior of a user's particular corrected library. The model remains a local, uncalibrated review aid; it does not automatically accept suggestions or claim listener-verified drum identity.

One possible small, permissively licensed smoke corpus is [Boochi44/free-drum-samples](https://github.com/Boochi44/free-drum-samples), whose README labels three curated kits with instrument folders and states that the files are CC0. Its kits cover a narrow hip-hop/trap sound palette, and many filenames contain their class; review both reports and treat the results as a limited diagnostic. The corpus is external evaluation data: do not add it to MagDrum source, test fixtures, installers or release assets. The evaluator does not download or redistribute datasets.

## First local measurement

On 2026-11-09, the evaluator scanned revision `77ba31428a079dd8f17c8e144c1e649ea0a198b3` of that corpus: 57 WAV files decoded, 44 files had recognized category-folder labels, and 13 files in other folders were excluded. There were no decode errors. Filename-plus-audio classification scored 68.2% accuracy and 0.697 macro F1; 34/44 (77.3%) outputs passed the review threshold, and those accepted outputs were 82.4% accurate. Audio-only classification scored 25.0% accuracy and 0.195 macro F1, with 0/44 outputs accepted at the threshold. In the audio-only report, Kick, Clap, Open hi-hat and Percussion had zero recall; Closed hi-hat recall was 1.0 and Snare recall was 0.667. Production similarity search returned 36 candidates for 17/44 queries; 20/36 (55.6%) shared the query folder category, and the top candidate matched for 12/17 queries with results (70.6%). This is a weak folder-label proxy and not a listener judgment. These results are evidence that the current heuristic is not ready to claim broad audio-only classification quality. The corpus is small, processed and class names often appear in filenames, so the measurements are directional rather than representative of acoustic drum libraries generally.

## Second local measurement

On 2026-11-09, the evaluator scanned 128 FLAC recordings from the CC0 Versilian Virtuosity Drums kit. The labels were assigned to evaluation folders from the kit's SFZ region-family codes (`hhc`, `hho`, `hhp`, `kick`, `ride`/`ridebell`, `sidestick`, `snare`, `tomhi` and `tomlo`). All 128 files decoded without errors. After adding these common SFZ codes to the filename heuristics, filename-plus-audio classification scored 100.0% accuracy and 1.000 macro F1 on this kit. This score measures successful recognition of its encoded family names and does not show that the waveform classifier can distinguish these classes.

With filenames hidden, audio-only classification scored 14.1% accuracy and 0.083 macro F1, and 0/128 outputs passed the app review threshold. Kicks were the only reliably recognized family (16/16); closed hats, open hats, pedal hats, rides and rimshots were mostly or entirely called Snare, while toms were usually called Kick. This is a clear failure of the current acoustic heuristics on this dataset. Together with the first corpus, the results support keeping filename cues as useful metadata while treating audio-only categories as low-confidence suggestions for review, not as dependable automatic labels. The kit represents one recorded contemporary jazz kit and is not a broad benchmark; its files remain external evaluation data and are not included in MagDrum.

## Third local measurement

On 2026-11-09, revision `9f04cf9a734527edfbb0a4eee1f674e45bbf71bc` of the CC0 [Virtuosity Drums](https://github.com/sfzinstruments/virtuosity_drums) library was evaluated; its [LICENSE](https://github.com/sfzinstruments/virtuosity_drums/blob/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/LICENSE) declares CC0 1.0. A temporary labeled subset contained 121 WAV/FLAC samples across Kick (14), Snare (14), Closed hi-hat (14), Open hi-hat (12), Pedal hi-hat (12), Tom (14), Ride (14), Crash (11), and Percussion (16). Labels followed the library's source folders and SFZ sample names; multiple mic captures and velocity/repetition variants come from the same kit. All 121 files decoded. Filename-plus-audio classification scored 75.2% accuracy and 0.737 macro F1; 91/121 suggestions passed the review threshold and all accepted suggestions matched the folder labels. With filenames hidden, it scored 2.5% accuracy and 0.007 macro F1, with 0/121 accepted. This is another warning that audio-only classification is not reliable; this selected subset is one kit, not an independent or genre-diverse benchmark.

On the same 121 files, production similarity search returned 235 candidates for 95/121 queries. Of the returned candidates, 211/235 (89.8%) matched the query folder category; the top candidate matched for 88/95 queries with results (92.6%). The result is likely influenced by related velocity and round-robin recordings within the same kit. It shows category agreement for this particular sample set, not human-perceived similarity or general retrieval quality. The temporary labeled copy and downloaded library were kept outside the MagDrum repository and release assets.

## User-example model measurements

## Current heuristic re-evaluation

On 2026-11-15, the classifier added conservative audio-only fallback rules for longer, low-frequency kicks and sustained mid-frequency toms, based on the feature distributions of the 121-file Virtuosity subset. On that set, filename-hidden accuracy increased from 2.5% / 0.007 macro F1 to 22.3% / 0.212; 0/121 audio-only suggestions reached the 0.75 review threshold. Filename-plus-audio results were 84.3% / 0.835 macro F1, with 91/121 accepted suggestions and 100% accuracy among those accepted. This remains a single-kit result and is not evidence of cross-kit reliability. On 2026-11-17, exact SFZ Tom-family codes (`htom`, `ltom`, `tomhi`, `tomlo`) were added to filename/folder recognition and the evaluation label map. Re-running the same 121-file subset scored 86.8% filename-plus-audio accuracy / 0.848 macro F1; 105/121 suggestions met the 0.75 review threshold and all accepted suggestions matched the folder label. All 14 Tom samples were recognized. Filename-hidden results remained 22.3% / 0.212, with 0/121 suggestions accepted; this change is driven by explicit SFZ naming metadata, not stronger audio recognition. A follow-up Windows evaluation added whole-word percussion instrument cues, including agogo, woodblock, triangle, guiro and timbale. On this same set, 16 Agogo samples were identified from their filenames: filename-plus-audio accuracy reached 100.0% / 1.000 macro F1, with 121/121 suggestions accepted and correct. Audio-only classification stayed at 22.3% / 0.212, confirming that this gain came from explicit instrument names rather than acoustic recognition.


On 2026-11-18, the Windows evaluator was rebuilt from the current source and run against the cached CC0 Virtuosity corpus at revision `9f04cf9a734527edfbb0a4eee1f674e45bbf71bc`. Before the filename cue fix, 83/121 samples were categorized correctly (68.6%, macro F1 0.603); errors were concentrated in files named `kickmic_hh_closed`, `kickmic_hh_open` and `kickmic_hh_pedal`, which were labeled Snare. Matching both `hh closed/open/pedal` and the reverse word order raised filename-plus-audio results to 121/121 (100.0%, macro F1 1.000) on this curated set. The audio-only result remained 22.3% accuracy / 0.212 macro F1, with 0/121 suggestions accepted; the user-example leave-one-out result was 71.1% / 0.711. This improvement is specifically a filename-token-order fix, not improved acoustic recognition or evidence of performance on other kits. Windows Release CTest passed 2/2 with regression fixtures for both token orders.

Also on 2026-11-18, conservative audio-only review hints were added for noisy cymbal/hi-hat samples, high pitched bell-like percussion and a narrow low-mid snare feature range. All outputs remain at 0.58 confidence, below the app's 0.75 acceptance threshold, and are suggestions for manual review. Re-evaluation raised audio-only results on the 121-file Virtuosity subset from 22.3% accuracy / 0.212 macro F1 to 70.2% / 0.737; 0/121 suggestions met the acceptance threshold. On the separate 52-file three-kit corpus, audio-only results were 48.1% / 0.420, compared with the previous 44.2% / 0.320; again, 0/52 were accepted. The Virtuosity set is one kit and the feature boundaries may reflect its recording/trim conventions; the smaller cross-kit result remains mixed, with confusion between percussion and snare and between open and closed hats. These results are limited diagnostics, not a general-purpose validated classifier. The updated Windows Debug CTest suite passed 2/2.

The same rules were evaluated on the 44 labeled samples from the three-kit free sample corpus. The earlier filename-hidden result was 25.0% / 0.195 macro F1, with no suggestions at the review threshold. After the exact `808s` folder mapping and low-confidence spectral kick fallback, the corpus contains 52 labeled samples (five FX files remain unlabeled): filename-plus-audio scored 71.2% accuracy / 0.727 macro F1, with 35/52 suggestions accepted and 82.9% accuracy among accepted; audio-only scored 44.2% / 0.320, up from 32.7% / 0.272, with 0/52 suggestions accepted. Audio-only Kick recall rose from 35.3% to 70.6% (precision 80.0%); three Percussion samples were also suggested as Kick, so these remain review-only candidates. On the 121-file single-kit corpus, the same change left audio-only results unchanged at 22.3% / 0.212, with 0/121 accepted. User-example matching scored 71.2% / 0.636 leave-one-sample-out and 51.9% / 0.408 leave-one-kit-out. These folder-derived labels are evaluation proxies; audio-only classification remains a low-confidence manual-review aid, not a dependable automatic labeler. Run the evaluator with `--feature-summary` to inspect category-wise acoustic measurements; they do not define validated universal thresholds.

The example-matching model now selects the acoustic-fingerprint weight from 0.10, 0.25 and 0.50 using category-stratified inner leave-one-out validation. It evaluates up to eight examples per category, hides query filenames, and excludes each query from its candidate examples. The outer evaluation also hides query filenames. On the 44 labeled samples in the three-kit free sample corpus, nested leave-one-sample-out testing scored 65.9% accuracy and 0.618 macro F1; holding out all samples from each kit scored 38.6% accuracy and 0.356 macro F1. On the 121-sample single-kit Virtuosity subset, nested leave-one-sample-out testing scored 71.1% accuracy and 0.711 macro F1. The prior fixed 0.10 fingerprint weight scored 59.1% / 0.567 and 40.9% / 0.379 on the three-kit splits, and 65.3% / 0.616 on the single-kit set. The adaptive weight improves sample-out results on these small collections, but the leave-one-kit-out score is still weak. These folder labels are not calibrated probabilities or an independent listener review, so suggestions remain low-confidence and never overwrite a manual correction.

## Current Windows source re-run

On 2026-11-18, `MagDrumClassifierEvaluation` was rebuilt from the current Windows source at repository commit `9511448` (classifier implementation `06a7bad`) and run against the cached, labeled evaluation corpora without modifying their source files. The [three-kit corpus](https://github.com/Boochi44/free-drum-samples/tree/77ba31428a079dd8f17c8e144c1e649ea0a198b3) decoded 57 files; 52 had labels and five effects were excluded. Filename-plus-audio scored 71.2% accuracy / 0.745 macro F1; 32/52 suggestions (61.5%) met the 0.75 threshold and those accepted suggestions were 81.2% accurate. With filenames hidden, results were 48.1% / 0.420 and 0/52 reached the threshold. The manually corrected-example model scored 75.0% / 0.741 when one sample at a time was held out, but fell to 48.1% / 0.420 when every sample from the query's kit was excluded from training. This small corpus does not demonstrate useful cross-kit improvement.

The [Virtuosity corpus](https://github.com/sfzinstruments/virtuosity_drums/tree/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc) decoded 121/121 files. Filename-plus-audio scored 100.0% / 1.000 because its sample names encode the drum family; this is not an acoustic-only result. With filenames hidden, the heuristic scored 70.2% accuracy / 0.737 macro F1 and 0/121 suggestions met the 0.75 threshold. The manually corrected-example model scored 87.6% / 0.881 with one sample held out, but this is a single-kit result and cannot establish cross-kit generalization. The model's suggestions deliberately remain below the automatic-acceptance threshold, so these leave-one-out accuracy figures do not imply any automatically accepted suggestions. Both reruns reinforce that audio-only classification is a low-confidence review aid; category labels and confidence are not reliable across untested libraries.

To reproduce the curated subset, clone the upstream CC0 repository to a temporary location, then run the preparation script. It requires a new output directory outside both the source library and MagDrum, and copies a deterministic, category-balanced selection without changing the source:

```powershell
$source = Join-Path $env:TEMP 'virtuosity_drums'
$labeled = Join-Path $env:TEMP 'magdrum-virtuosity-labeled'
git clone --depth 1 https://github.com/sfzinstruments/virtuosity_drums.git $source
pwsh -NoProfile -File .\tools\Prepare-VirtuosityEvaluation.ps1 -SourceRoot $source -OutputRoot $labeled
& .\build\classifier-evaluation\MagDrumClassifierEvaluation_artefacts\Release\MagDrumClassifierEvaluation.exe $labeled
```

## Current Windows baseline (2026-11-19)

Re-running the evaluator built from the current Windows source against the same external labeled data confirmed the known limits. On the 121-file Virtuosity subset, filename-plus-audio cues scored 100% (121/121), while filename-hidden audio scored 70.2% accuracy / 0.737 macro F1; no audio-only category reached the 0.75 review threshold. The manually corrected-example matcher scored 87.6% / 0.881 macro F1 with one sample held out, but also produced no suggestions at the acceptance threshold. On the 52-sample, three-kit free-sample subset, filename-hidden accuracy was 48.1% / 0.420 macro F1, and leave-one-kit-out example matching produced the same score; no suggestions reached the acceptance threshold. These results support treating inferred audio categories as low-confidence review hints. Reliable cross-kit classification remains an open research item; the category filters continue to use explicit names, folders, categories and tags as the primary signals.
