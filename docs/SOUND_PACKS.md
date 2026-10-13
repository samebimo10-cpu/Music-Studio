# Sound packs

A sound pack is a folder of audio samples plus a `pack.json` manifest. Packs are installed
into the packs folder and played by the streaming sampler, so a large library does not
have to fit in RAM.

## Where packs live

The default location is `<user app data>/Studio/Sound Packs/`, with one sub-folder per pack.
You can move it, for example to an external drive, with **Library → Packs → Packs folder...**. Studio reads packs from
wherever that folder points.

## Layout

```
gospel-keys/
  pack.json
  piano/
    C2_p.wav  C2_mf.wav  C2_f.wav  C2_f_rr2.wav ...
  rhodes/
    ...
```

Any format JUCE reads can be used (WAV, AIFF, FLAC, and MP3 where enabled). For the best
streaming performance use WAV or AIFF, because they decode with the least CPU.

## pack.json

```json
{
  "id": "gospel-keys",
  "name": "Gospel Keys",
  "version": "1.0",
  "description": "Grand piano and Rhodes, 3 velocity layers, 2 round-robins.",
  "instruments": [
    {
      "id": "piano",
      "name": "Gospel Grand",
      "family": "Keys",
      "role": "keys",
      "attack": 0.002,
      "release": 0.4,
      "zones": [
        { "file": "piano/C2_p.wav",  "root": 36, "lo": 34, "hi": 38, "velLo": 0,  "velHi": 63 },
        { "file": "piano/C2_mf.wav", "root": 36, "lo": 34, "hi": 38, "velLo": 64, "velHi": 99 },
        { "file": "piano/C2_f.wav",  "root": 36, "lo": 34, "hi": 38, "velLo": 100, "velHi": 127, "rr": 0 },
        { "file": "piano/C2_f_rr2.wav", "root": 36, "lo": 34, "hi": 38, "velLo": 100, "velHi": 127, "rr": 1 }
      ]
    }
  ]
}
```

| Field | Default | Meaning |
| --- | --- | --- |
| `id` | folder name | Pack id, used in instrument specs |
| `name`, `version`, `description` | id, `1.0`, empty | Shown in the Library |
| `instruments[].id` | — | Instrument id within the pack |
| `instruments[].name` | id | Display name |
| `instruments[].family`, `role` | empty | Library grouping and a descriptive role tag (stored, not yet used by auto-mix) |
| `instruments[].attack`, `release` | 0.002, 0.3 | Amplitude envelope, in seconds |
| `zones[].file` | — | Path relative to the pack folder |
| `zones[].root` | 60 | MIDI note the sample was recorded at |
| `zones[].lo`, `hi` | root | Key range the zone plays (inclusive) |
| `zones[].velLo`, `velHi` | 0, 127 | Velocity range (inclusive), for velocity layers |
| `zones[].rr` | 0 | Round-robin group, used to label alternative takes |

### How a zone is chosen

On each note-on, the sampler collects every zone whose key and velocity ranges contain
the note. If several match, it cycles through them in turn on each repeat of that note.
That is how round-robins work: give alternative takes the same key and velocity ranges.
The sample is repitched from `root` to the played note and resampled to the session rate.

## Streaming

- When an instrument loads, only the first frames of each sample are read into RAM. The
  default is 16 384 frames, about 0.34 s at 48 kHz, and it can be set from 4 096 to 262 144.
- A note starts instantly from that preload. A single background disk thread then fills a
  per-voice ring buffer (about 0.7 s) from the rest of the file.
- The audio thread never reads from disk. If the disk falls behind, the voice outputs
  silence for the missing frames rather than blocking.
- Each pack instrument costs roughly `zones × channels × preload × 4 bytes` of RAM. For
  example, 3 000 stereo zones at the default preload take about 390 MB, whatever the
  pack's size on disk. The Library screen shows the current preload memory.

## Installing

- **From a file or drive:** in Library → Packs, choose **Install from file** and pick a
  `.zip` or a folder (for example on a USB drive).
  - Folders are copied into the packs folder.
  - Zips are extracted. An archive whose contents are wrapped in a single top-level folder
    is handled.
  - A pack without `pack.json` is rejected with a plain-language message.
- **Download:** a pack archive URL is downloaded in the background with progress, then
  installed.
  - Downloads resume: the partial file is kept, and the next attempt continues with an
    HTTP `Range` request, so a dropped connection on a slow or costly data plan does not
    start over.
- **Remove:** deletes the pack folder and drops its cached instruments.

## Using a pack instrument

Pack instruments appear in the Library and in every instrument picker next to the
starter set. Internally a track refers to one as `sampler:<packId>/<instrumentId>`, which
is what `project.json` stores. If a project is opened on a computer without that pack,
the track stays silent until the pack is installed. The project is not changed.

## Licensing

Only include samples you own or have licensed for redistribution inside an app. Most
consumer sample packs do not allow this. See the spec's "Licensing, legal and rights"
section.
