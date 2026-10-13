# Spec coverage

This maps every item in the product spec to its status in this repository.

- **Done**: built and working. Where it says "tested", there is a unit test or an end-to-end CLI check for it.
- **Partial**: works, with a gap that is noted.
- **Not built**: absent. Most of these are Phase 4 platform work that needs servers, partners or a commercial agreement.

## Platform and architecture

| Spec item | Status | Notes |
| --- | --- | --- |
| Native C++ on JUCE | Done | JUCE 8.0.9. The app, CLI and tests share `studio_core` |
| Windows and macOS desktop | Done | CI builds both, plus Linux |
| ASIO, Core Audio | Done | Core Audio is built in. ASIO needs the Steinberg SDK at build time (`STUDIO_ASIO_SDK_DIR`); without it Windows uses WASAPI |
| Oboe/AAudio (Android) | Not built | No mobile app (Phase 4) |
| Native UI | Done | JUCE native UI, not a WebView |
| ONNX Runtime models | Partial | Every model slot has a working DSP or rule-based implementation behind a stable interface. No ONNX Runtime is linked yet. See [ARCHITECTURE.md](ARCHITECTURE.md#dsp-and-ml-slots) |
| VST3/AU hosting | Done | Scan, insert on any track, editor window, latency compensated |
| Project folders, SQLite library index | Done | |
| Cloud backend, payments | Not built | Phase 4. Only the sync manifest (content hashes) exists |
| Real-time thread never allocates or waits on disk; lock-free queues | Done | Graph handover, recording FIFOs and the disk streamer |

## Module 1: Recording and vocal processing

| Spec item | Status | Notes |
| --- | --- | --- |
| Input setup wizard | Done | Picks the device, sets gain against a live meter, gives plain-language advice on clipping or too low a level |
| Low-latency monitoring with optional reverb | Done | Direct monitoring with headphone-only cue reverb. Latency is shown in Settings |
| Metronome, count-in, pre-roll | Done | |
| Punch in/out | Done | |
| Loop recording into takes | Done | |
| Comping with crossfades | Done (tested) | Take lanes on the Record screen |
| Multi-input recording, up to 16 | Done | Per-track input channel, mono or stereo. Limited by the interface |
| Room check | Done (tested) | 10 s recording, then noise floor, hum, hiss and fixes |
| 1. Noise removal | Partial (tested) | DSP spectral denoiser with hum notches, not RNNoise or DeepFilterNet. Strong on steady noise (fans, hiss, hum, generators); weaker on non-stationary noise than an ML model |
| 2. De-reverb | Partial | Spectral late-reverb suppression. Helps a lightly untreated room |
| 3. Breath and click control | Done | Breaths and clicks are lowered, not cut |
| 4. Pitch correction plus note editor | Done (tested) | Key and scale aware, natural to robotic. Note editor in the Vocal booth |
| 5. Timing alignment | Done | DTW alignment of backing vocals and doubles to the lead |
| 6–10. EQ, compression, de-esser, saturation, reverb/delay sends | Done (tested) | |
| Every step bypassable | Done | |
| 7 vocal presets with one Amount knob | Done (tested) | |
| Harmony generator, 2–4 parts in key | Done (tested) | |
| Doubler | Done | |
| Voice-to-MIDI | Done (tested) | |
| Stem separation | Partial | DSP separation into vocals, drums, bass and other. Usable for practice tracks; clearly below ML separators in quality |

## Module 2: Instruments, library and beat generator

| Spec item | Status | Notes |
| --- | --- | --- |
| Every family and instrument in the table | Partial | 43 starter instruments cover every row: keys, bass, guitars, drums, African percussion, strings and brass, pads and synths, choir. They are **synthesised** (modal piano, FM electric pianos, drawbar organ with Leslie, Karplus-Strong guitars, formant choir, modelled percussion), not recorded. Good for writing; recorded packs are needed for release-grade realism (the spec's top risk) |
| Multi-sampled, velocity layers, round-robins | Done | The streaming sampler supports all three from pack files. Synthesised drums vary each hit. No recorded packs are included; see [SOUND_PACKS.md](SOUND_PACKS.md) |
| Packs download on demand, move to external drive, stream from disk | Done | Resumable downloads, install from zip or folder, movable packs folder, preload plus disk streaming |
| Beat generator: style, tempo, key, mood, length, then chords, drums, bass, keys, guitar, percussion | Done (tested) | Also adds pads. Each part is editable MIDI on its own track |
| Regenerate part, simpler/busier, swap feel, more like this | Done | |
| Rule-based templates plus ML variations | Partial | Rule-based templates with seeded variation. No ML model |
| All 9 style presets with tempo ranges | Done (tested) | |
| MIDI keyboard and pad support, no setup | Done | All MIDI inputs are enabled automatically and routed to the selected track |
| On-screen keyboard and drum pads | Done | Beat lab |
| Chord pads by key and scale | Done | |
| Smart quantize | Done | Strength plus a keep-feel window |
| Arpeggiator and strum engine | Done | |
| Loop browser by style, tempo, key and instrument; auto-matched when added | Done (tested) | Time-stretched and pitch-shifted to the song. Starter loops are generated by the beat engine |

## Module 3: Arrangement and editing

| Spec item | Status | Notes |
| --- | --- | --- |
| Sections: duplicate, reorder, resize; every track follows | Done (tested) | |
| Vamp builder with rising energy | Done (tested) | Optional key lift |
| Unlimited tracks, folders, colours, markers | Done | |
| Tempo and key track | Done | Tempo changes and key changes |
| Cut, trim, fade, crossfade | Done | |
| Time-stretch and pitch-shift | Done (tested) | WSOLA stretch and a granular shifter. Good quality at moderate amounts; artifacts at extreme settings |
| Warp tool | Done | |
| Piano roll, drum grid, velocity lane, scale highlighting, chord detection, humanize | Done | |
| Automation of volume, pan and any effect parameter | Done | |
| Unlimited undo and named snapshots | Done (tested) | |
| Arrangement assistant | Done (tested) | Suggests a structure and flags sections that drag |

## Module 4: Mixing

| Spec item | Status | Notes |
| --- | --- | --- |
| Fader, pan, mute, solo, meter, effect slots; buses; sends/returns | Done | |
| Built-in effects including sidechain | Done (tested) | Reverb has room, hall, church and plate modes. 16 effect types in all |
| Auto-mix by role | Done | |
| Mix assistant with one-click fixes | Done | Buried vocal, kick/bass clash, too much low end, harshness, mud, clipping and more |
| Reference track comparison | Done | Tone curve and loudness |
| Listening checks | Done | Phone, car, earbuds and mono, on the monitor output only |
| Third-party VST3/AU | Done | |

## Module 5: Mastering and export

| Spec item | Status | Notes |
| --- | --- | --- |
| EQ, multiband compression, imaging, saturation, true-peak limiter | Done (tested) | |
| Warm, Bright, Loud, Gentle worship presets | Done (tested) | Every preset reaches −14 LUFS at or below −1 dBTP in tests |
| AI master that suggests settings | Partial | Analysis-driven suggestions with explanations. Not an ML model |
| Platform turn-up/down meter | Done | Uses common published targets; confirm current platform guidance before launch, as the spec says |
| WAV 24-bit 44.1/48 kHz, FLAC | Done | |
| MP3 320 and AAC | Done | Needs `ffmpeg` (or `lame` for MP3) installed. Not bundled, for licensing reasons |
| Instrumental and a cappella | Done | |
| Stems, one per bus | Done (tested) | Stems sum back to the mix |
| Performance track with click and cues | Done | Click and cues on the left channel, music on the right. Cues are a rising two-note tone one bar before each section, plus a text cue sheet; there are no spoken cues |
| MIDI export | Done | |
| Lyric video MP4 | Done | Synced lyrics over artwork. Needs `ffmpeg` |
| Album mastering: order, match loudness, gaps | Done | Numbered files plus one continuous WAV with a CUE sheet |

## Module 6: Release desk

| Spec item | Status | Notes |
| --- | --- | --- |
| Single, EP or album with track order | Done | |
| Full metadata | Done (tested) | Validated |
| Lyrics time-synced | Done (tested) | Tap to sync, then LRC export |
| ISRC and UPC | Done (tested) | ISRC format and UPC check-digit validation. Codes are entered by the user; there is no partner to assign them |
| 3000×3000 cover checker | Partial | Checks size, blur, borders and text density with heuristics. It cannot read text, so a URL or price in the art is not detected |
| Art maker with templates | Done | |
| Splits | Done (tested) | Must total 100 %. Exported in the metadata sheet |
| Pre-release checklist | Done (tested) | Loudness, clipping, silence at start and end, spelling and name consistency |
| Export package for manual upload | Done (tested) | Master WAV, artwork, metadata sheet and zip |
| Partner integration | Not built | Needs a commercial distribution partner (Phase 4) |
| Release dashboard with store links | Partial | Store links are kept and shown. No live status from a partner |
| Smart-link and pre-save page | Done | Static HTML to host anywhere |
| Streams and earnings | Partial | Imports a distributor's earnings CSV. No partner API |
| Reminders for collecting society and publisher | Done | |

## Supporting features

| Spec item | Status | Notes |
| --- | --- | --- |
| Lyrics pad: sections, rhyme helper, syllables, chord chart, voice memos on lines | Done (tested) | The rhyme helper is offline and suffix-based, so its suggestions are basic |
| Idea inbox | Partial | A local inbox folder: drop a memo in and it becomes a project. Phone sync needs the mobile app (Phase 4) |
| Tuner, metronome, key and tempo detector, chord-from-audio | Done (tested) | |
| Autosave, crash recovery, version history | Done (tested) | |
| Cloud backup | Not built | Phase 4. A sync manifest is ready |
| Remote collaboration | Not built | Phase 4 |
| Timeline comments | Done | |
| The four templates | Done (tested) | |
| Guided first song, tooltips, lessons | Done | Guided mode steps through Idea → Release. Tooltips on controls, and short lessons on Home |
| Marketplace, accounts and plans | Not built | Phase 4 |

## UX and screens

| Spec item | Status | Notes |
| --- | --- | --- |
| Guided and Studio modes | Done | |
| All 12 screens | Done | Plus Lyrics and Tools. See `docs/screenshots/` |
| Settings: account | Not built | There are no accounts yet |
| Dark theme, keyboard shortcuts, every destructive action undoable | Done | |
| Large touch targets on mobile | Not built | No mobile app |

## Data model and formats

| Spec item | Status | Notes |
| --- | --- | --- |
| Project folder layout | Done (tested) | Matches the spec. 32-bit float WAV for recordings |
| Core entities | Done | |
| Cloud records | Partial | Only the SyncedProject manifest exists |
| Import MIDI, WAV, AIFF, FLAC, MP3; stems from other DAWs | Done | Multi-file audio import |
| Export AAF | Not built | A stems bundle is available instead |

## Performance and quality

These targets have **not been benchmarked on the spec's reference hardware**. What is known:

| Target | Status |
| --- | --- |
| 32-bit float, 44.1–96 kHz | Done |
| Autosave every 30 s with crash recovery | Done (tested) |
| Under 10 ms monitoring at a 128-sample buffer | Achievable with ASIO or Core Audio at 128 samples. Not measured on hardware |
| 64 audio plus 32 instrument tracks; zero dropouts for 1 hour | Not measured. The real-time-safe design is in place. Offline render of a dense generated song runs about 3× faster than real time on the 4-core CI container |
| Startup under 5 s | Not measured on target hardware |
| Noise removal faster than real time | Yes: the DSP denoiser is far faster than real time |
| 30 GB pack in under 2 GB RAM | Designed for it: RAM use is preload × zones, not pack size. Not tested with a 30 GB pack |
| Screen-reader labels, scalable UI, colour-blind-safe meters | Partial: the UI scale setting (0.8–1.6×) and colour-blind-safe meters are done. Screen-reader support relies on JUCE's built-in accessibility for standard controls; custom views (timeline, piano roll) have no detailed labels |

## Build phases

| Phase | Status |
| --- | --- |
| 1: Core engine and recording | Done |
| 2: Instruments, beats and mastering | Done. All 9 styles, not just 4 |
| 3: Depth and polish | Done, except recorded sound packs (the format and streaming are ready) and ML models (DSP versions ship) |
| 4: Platform | Not built: cloud sync, collaboration, mobile, partner distribution, royalty dashboard, marketplace |
