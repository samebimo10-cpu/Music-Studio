# Architecture

Studio is three binaries built from one static library:

```
source/core   studio_core    engine, DSP, instruments, generator, project model, export, release
source/app    Studio         JUCE desktop UI (links studio_core only)
source/cli    studio_cli     headless front end over the same code
tests/        studio_tests   JUCE UnitTest suite, category "Studio"
```

`studio_core` links the JUCE modules privately and re-exports their include paths and
definitions, so the app, CLI and tests compile against one consistent JUCE configuration.

## Core layout

| Folder | What it holds |
| --- | --- |
| `model/` | `Project` and every entity (Track, Clip, Take, Section, ChordEvent, Effect slot, AutomationLane, Release, Contributor, Split), JSON round-trip, `TempoMap` |
| `project/` | `ProjectStore`: folder layout, save/load, audio import, snapshots, autosave, crash recovery, sync manifest, `UndoHistory`, templates |
| `engine/` | `AudioEngine` (device, transport, recording, monitoring), `MixGraph` (the real-time render graph), `OfflineRenderer`, `ClipCache`, `PluginHost`, `Metronome` |
| `dsp/` | Effect base class and all built-in effects, spectral effects (noise removal, de-reverb), voice effects (pitch shift/correction, doubler, breath), time-stretch, true-peak detection |
| `analysis/` | BS.1770 loudness, YIN pitch, spectrum, chroma, key, tempo, chords, room check |
| `vocal/` | The 10-step `VocalChain` and its presets; comping, pitch editor, harmonies, voice-to-MIDI, timing alignment, stem separation |
| `instruments/` | `SynthInstrument`, `DrumKit`, the starter `InstrumentLibrary`, streaming `SamplerInstrument` and `SamplePackManager` |
| `beatgen/` | Style templates and the `BeatGenerator` |
| `arrange/`, `midi/`, `mixing/`, `mastering/` | Editing operations, MIDI tools, auto-mix and mix assistant, mastering chain and AI master |
| `exporting/`, `release/`, `lyrics/`, `library/` | Exports and album mastering, release desk, lyrics tools, SQLite loop library |

## Threads

| Thread | Does | Never does |
| --- | --- | --- |
| Audio (device callback) | Runs the current `MixGraph`, records input into lock-free FIFOs, meters | Allocate, lock, touch disk, read the `Project` |
| Message (UI) | Edits the `Project`, builds new graphs, owns undo | Process audio |
| Recording writer | `ThreadedWriter` drains input FIFOs to WAV files | — |
| Disk streamer | Refills sampler voice ring buffers from disk | — |
| Background jobs | Vocal processing, analysis, rendering, export, downloads (`ThreadPool`) | Touch the live graph |

The audio thread talks to the rest only through atomics and single-producer/single-consumer
FIFOs (`AbstractFifo`). Live MIDI from controllers arrives on a `MidiMessageCollector`.

## Graph handover

The `Project` is never read on the audio thread. When it changes, the message thread:

1. Builds a new `MixGraph` from the project: one node per track and bus, with
   effect chains, sends, sidechains, automation curves and pre-resolved MIDI and
   audio clip schedules.
2. Re-uses effect and instrument instances from the current graph where the slot id
   or track id matches. This keeps reverb tails, held notes and compressor state across edits.
3. Publishes it through an atomic `pending` pointer.

At the start of the next block, the audio thread swaps in the pending graph and chases
notes that should already be sounding. It pushes the old graph into a retire FIFO, and
the engine's timer deletes it on the message thread. No allocation or deallocation happens
on the audio thread.

## Latency compensation

Each node reports its latency: the effect chain's total plus any plugin latency. The graph
gives every node a `readAhead` equal to the largest latency on its path minus its own.
Clips and MIDI are then scheduled earlier by that amount. As a result, the look-ahead
limiter, STFT effects (one frame, 1024 samples) and plugins stay sample-aligned with dry
tracks. The same graph is used offline, so renders match playback exactly.

## Silence skip

A track whose input has been silent for 10 seconds plus its chain latency stops running its
effects until signal returns. This lets a template with many idle vocal chains play without
CPU cost. The skip is per node, so tails always finish.

## Recording

- Input goes to one FIFO per armed track; the writer thread makes a WAV take.
- Loop recording records one continuous file, and stores wrap indices that split it into
  takes per loop pass.
- Punch in and out trim to the punch range.
- Count-in and pre-roll are handled by the transport before recording starts.
- Monitoring is direct, input to output inside the callback. It has an optional cue
  reverb that only goes to the monitor path.
- Listening checks (phone, car, earbuds, mono) run on the master output only and are never
  rendered into exports.

## Offline rendering and export

`OfflineRenderer` runs the same `MixGraph` faster than real time. Stems are made by soloing
buses. `Exporter::exportAll` works in this order:

1. Render the mix.
2. Fit the loudness to the mastering target.
3. Run the mastering chain.
4. Write all formats from that one master.

That way the WAV, FLAC, MP3, AAC and video all match. MP3, AAC and MP4 go through `ffmpeg`
(or `lame`) found on the PATH or next to the app.

## Projects, undo and recovery

- **Undo:** stores whole-project snapshots in memory. That is cheap because audio lives in
  files, not in the JSON. Named snapshots go to `history/`.
- **Autosave:** every 30 s, to a recovery file next to `project.json`. A lock file marks an
  open session, so a lock found at startup means a crash and recovery is offered.
- **Sync manifest:** lists every file with a SHA-256 hash, so a future sync service only
  uploads changed audio.

## DSP and ML slots

The spec calls for on-device models (ONNX Runtime) in several places. Studio ships DSP or
rule-based implementations behind the same interfaces, so a model can be dropped in later
without changing callers:

| Slot | Current implementation | Interface |
| --- | --- | --- |
| Noise removal | STFT Wiener filter with decision-directed SNR, minimum-statistics noise tracking, a learned profile, and 50/60 Hz hum notches | `dsp::NoiseReductionEffect`, effect type `denoise` |
| De-reverb | Spectral late-reverb suppression (Lebart decay model) | `dsp::DeReverbEffect`, effect type `dereverb` |
| Stem separation | Harmonic/percussive separation plus centre extraction and band splits into vocals, drums, bass and other | `vocal::separateStems` |
| Beat variations | Seeded rule-based variation: density, feel and fills within the style templates | `BeatGenerator` with `GeneratorInfo` |
| AI master | Spectral and dynamics analysis mapped to chain settings, with plain-language reasons | `mastering::suggest` |
| Artwork text check | Edge and contrast heuristics (no OCR) | `release::checkArtwork` |

## UI

- Every screen derives from `Screen` and reads and writes the project through `Session`.
  `Session::perform()` applies an edit, records undo and rebuilds the graph.
- `StudioLookAndFeel` is the dark theme. Meters use a colour-blind-safe blue-to-yellow-to-orange scale (never red against green).
- `Studio --screenshot <dir>` loads a demo song, renders every screen to PNG and exits. CI
  uses it to catch UI regressions.
