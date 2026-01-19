# Hammer Sampler

A JUCE-based VST3 plugin that displays MIDI input on a visual 88-key keyboard and plays back samples using Direct From Disk (DFD) streaming. Designed for large sample libraries (100GB+) that exceed available RAM.

## Features

- **DFD Streaming Architecture** - Preload buffers + real-time disk streaming enables massive libraries
- **Full 88-key piano display** (A0-C8) with octave labels and sample availability coloring
- **DAW project state persistence** - samples and settings auto-reload when you reopen a project
- **Async sample loading** - projects load instantly, samples load in background thread
- Dynamic per-note grid showing velocity layers and round-robin positions
- Sample playback with velocity layers and dynamic round-robin cycling (auto-detected from samples, up to 180 voices)
- Pitch-shifting for notes using fallback samples
- Global ADSR envelope controls
- **Transpose** (-12 to +12 semitones) - shift output notes
- **Sample Offset** (-12 to +12 semitones) - borrow samples from other notes with pitch correction for subtle timbre changes
- **Velocity Layer Limit** - reduce velocity layers for lo-fi sound or lower data usage
- **Round Robin Limit** - reduce round robin positions to lower CPU/disk usage
- Sustain pedal support with visual feedback
- Same-note voice stealing with 10ms crossfade (see Voice Stealing section for details)

## Sample Folder Setup

Point the plugin to a folder containing your audio samples. Samples must follow a specific naming convention to be mapped correctly.

### File Naming Convention

```
NoteName_Velocity_RoundRobin[_OptionalSuffix...].ext
```

**Components:**
- **NoteName**: Note name with octave (e.g., `C4`, `G#6`, `Db3`, `F#5`)
  - Sharps use `#` (e.g., `C#4`, `F#3`)
  - Flats use `b` (e.g., `Db4`, `Bb3`)
  - Case insensitive
- **Velocity**: Velocity value 1-127 (e.g., `001`, `040`, `127`)
- **RoundRobin**: Round-robin position 1-3 (e.g., `01`, `02`, `03`)
- **OptionalSuffix**: Any additional underscored text is ignored (for organization)
- **ext**: Audio file extension (`.wav`, `.aif`, `.aiff`, `.flac`, `.mp3`)

**Examples:**
```
C4_127_01.wav           → C4, velocity 127, round-robin 1
G#6_040_02.wav          → G#6, velocity 40, round-robin 2
Db3_080_03.wav          → Db3, velocity 80, round-robin 3
A0_040_01_piano.wav     → A0, velocity 40, round-robin 1 (suffix ignored)
F#5_100_02_soft_v2.wav  → F#5, velocity 100, round-robin 2 (suffixes ignored)
```

### Velocity Range Mapping

Samples don't need to cover all 127 velocity values. The plugin automatically creates velocity ranges based on available samples.

**How it works:**
- Each velocity value in your samples covers a range from itself down to the next lower available velocity + 1
- The lowest velocity sample covers from velocity 1 up to its value
- The GUI dynamically adjusts to show the actual number of velocity layers in your samples

**Example with velocities 040, 080, 100, 127:**
| Sample Velocity | Triggers on MIDI Velocities |
|-----------------|----------------------------|
| 127             | 101 - 127                  |
| 100             | 81 - 100                   |
| 080             | 41 - 80                    |
| 040             | 1 - 40                     |

**Example with velocities 001, 064, 127:**
| Sample Velocity | Triggers on MIDI Velocities |
|-----------------|----------------------------|
| 127             | 65 - 127                   |
| 064             | 2 - 64                     |
| 001             | 1 only                     |

### Missing Notes (Fallback & Pitch-Shifting)

If a note has no samples available:
- The plugin uses the **first available sample higher in pitch**
- **Pitch-shifting is applied** - the sample is transposed down to sound at the correct pitch
- Uses linear interpolation for smooth pitch-shifted playback

**Example:**
If you have samples for C4, E4, and G4, but play D4:
- D4 will trigger the E4 sample (next higher available note)
- The sample is pitch-shifted down 2 semitones to sound like D4

### Round-Robin

Round-robin cycles through available samples for natural variation when repeatedly playing the same note at similar velocities.

**Dynamic Detection:** The plugin automatically detects the number of round-robin positions from your sample filenames. If your library has samples numbered `_01` through `_06`, the plugin cycles through all 6 positions (1 → 2 → 3 → 4 → 5 → 6 → 1). The UI grid dynamically adjusts to show the correct number of RR boxes.

## Visual Display

### Keyboard

The full 88-key piano keyboard (A0-C8) shows sample availability with color coding:
- **White/Black (normal)**: Note has its own samples
- **Light grey**: Note uses fallback samples (pitch-shifted from higher note)
- **Dark grey**: Note is unavailable (no samples or fallback)
- **Blue**: Note is currently pressed

Octave labels (C1-C8) are displayed below the keyboard for easy reference.

### Note Grid

The grid above the keyboard shows:
- **Columns**: One per note (88 notes, A0-C8)
- **Rows**: Dynamic based on velocity layer limit (scales to fill available space)
- **Cells**: Round-robin boxes per velocity layer (scales based on RR limit)

**Grid colors:**
- **Blue**: Active velocity layer and round-robin position
- **Dim blue**: Active velocity layer, different round-robin
- **Dark grey**: Available but not active
- **Very dark grey**: Unavailable (no samples for this note/layer)
- **Orange dash**: Exact velocity position (only on currently pressed notes)

**Dynamic Scaling:** The grid automatically rescales when you adjust the Velocity Layer Limit or RR Limit sliders. With fewer layers/positions, each cell grows larger to fill the space, making it easier to see activity.

**Orange Velocity Indicator:** An orange horizontal dash shows the precise velocity position within the grid for each currently pressed note. The dash spans across all RR boxes for visibility. Velocity 127 appears at the top, velocity 1 at the bottom.

When a note is played:
- The corresponding key lights up blue
- The velocity layer row for that note activates
- The round-robin box shows which position triggered
- An orange dash shows the exact velocity position

**Sustained vs Pressed:** With sustain pedal held:
- **Pressed notes**: Show blue boxes AND orange dash
- **Sustained notes** (key released, pedal held): Show blue boxes only (no orange dash)

This distinction helps you see which keys are actively being pressed versus which are being held by the sustain pedal.

### UI Controls

All rotary knobs use **horizontal drag** (left/right) to adjust values. The controls are arranged in a single row:

| Control | Range | Description |
|---------|-------|-------------|
| **A** (Attack) | 0.001 - 2.0s | Envelope attack time |
| **D** (Decay) | 0.001 - 2.0s | Envelope decay time |
| **S** (Sustain) | 0.0 - 1.0 | Envelope sustain level |
| **R** (Release) | 0.001 - 3.0s | Envelope release time |
| **Preload** | 32 - 1024 KB | Per-sample preload buffer size |
| **Transpose** | -12 to +12 | Semitone shift (no pitch correction) |
| **Sample Ofs** | -12 to +12 | Sample borrowing with pitch correction |
| **Vel Layers** | 1 to max | Limit velocity layers used |
| **RR Limit** | 1 to max | Limit round-robin positions |

### Status Display

The top-right area shows real-time information:
- **Size**: Total instrument file size on disk (GB/MB)
- **RAM**: Memory used by preload buffers
- **Voices**: Active voices | Streaming voices
- **Throughput**: Current disk read speed (MB/s)

### ADSR Envelope

The global amplitude envelope shapes each note:
- **A (Attack)**: 0.001 - 2.0 seconds
- **D (Decay)**: 0.001 - 2.0 seconds
- **S (Sustain)**: 0.0 - 1.0 level
- **R (Release)**: 0.001 - 3.0 seconds

### Transpose

Shifts the output note by -12 to +12 semitones. This is a simple MIDI offset with no pitch correction.

**Example with Transpose +5:**
- Press C4 → triggers and sounds F4

### Sample Offset

A subtle sound design feature that borrows samples from a different note (-12 to +12 semitones) but pitch-corrects them back to sound like the original note. This lets you play a note with a different sample's timbral characteristics.

**Example with Sample Offset +1:**
- Press C4 → uses C#4 sample → pitch-shifted down 1 semitone → sounds like C4 with C#4's timbre

**Combined example (Transpose +2, Sample Offset +1):**
- Press C4 → target sound is D4 → uses D#4 sample → pitch-shifted down → sounds like D4 with D#4's timbre

### Velocity Layer Limit

Reduces the number of velocity layers used for playback. The slider defaults to the maximum number of layers detected in your sample library. Pulling it down limits playback to fewer layers, giving a more lo-fi sound and reducing data usage.

**How it works:**
- Removes the **highest** (loudest) velocity layers
- Redistributes velocity ranges evenly across remaining layers
- Minimum is 1 layer (all velocities trigger the same sample)

**Example with 3 velocity layers (001, 080, 127) limited to 2:**
- Keeps layers 001 and 080, removes 127
- Velocity mapping: 1-64 → 001 sample, 65-127 → 080 sample

**Example with 4 velocity layers limited to 1:**
- All velocities trigger the lowest velocity sample
- Creates a consistent, lo-fi sound

### Round Robin Limit

Reduces the number of round robin positions cycled through during playback. The slider defaults to the maximum RR count detected in your sample library. Reducing it limits playback to fewer positions, lowering CPU and disk usage.

**How it works:**
- Limits cycling to positions 1 through N (where N is the limit)
- Position N+1 and above are never used
- Minimum is 1 (no round robin variation)

**Example with 6 round robins limited to 2:**
- Only positions 1 and 2 are used
- Notes alternate: 1 → 2 → 1 → 2 → ...

**Example limited to 1:**
- All notes trigger the same round robin position
- No variation, but minimal disk reads

### Data Reduction Strategy

The Velocity Layer Limit and RR Limit can be combined to drastically reduce disk I/O and CPU usage:

| Configuration | Effect |
|---------------|--------|
| Full layers + Full RR | Maximum quality, highest disk usage |
| Reduced layers + Full RR | Simpler dynamics, natural variation |
| Full layers + Reduced RR | Full dynamics, less variation |
| 1 layer + 1 RR | Minimal disk reads, consistent sound |

**Use cases:**
- **Live performance**: Reduce limits to ensure no disk dropouts
- **Lo-fi aesthetic**: Use 1-2 velocity layers for a vintage sampler sound
- **CPU-limited systems**: Reduce RR to minimize concurrent disk reads
- **Quick sketching**: Minimal settings for fast response

## Voice Stealing

### Current Behavior

When the same note is retriggered (e.g., pressing C4 while C4 is already playing), the old voice is crossfaded out over 10ms while the new voice fades in. This prevents clicks but can create artifacts in certain scenarios.

### The Loud-to-Soft Retrigger Problem

If you play a note loudly (velocity 127) and immediately retrigger it softly (velocity 1), the crossfade blends the loud sample into the soft sample, creating an unnatural "sipping" sound as volume rapidly drops. Real pianos don't behave this way.

**Real piano physics:**
1. The damper lifts before the hammer strikes
2. The already-vibrating string continues momentarily
3. The hammer strikes a string that's already in motion
4. The existing vibration doesn't instantly stop - it interacts with the new strike
5. A soft restrike on a ringing string doesn't produce a sudden volume drop

### Planned Improvement: Polyphonic Same-Note

The most realistic solution is polyphonic same-note with natural release transition:

1. When the same note is retriggered, the **old voice** transitions to its release phase (as if the key was released) and continues its natural decay
2. The **new voice** starts fresh with its attack phase
3. Both voices coexist temporarily - the old fades naturally while the new plays

This mimics real piano behavior where the damper lifts, the vibrating string continues, and the new strike adds its own energy. You hear both the tail of the old vibration and the new attack blended naturally.

**Implementation considerations:**
- Remove same-note voice stealing entirely - treat retriggered notes as new voices
- Trigger `noteOff` on existing voices for that note (sends them to release phase)
- Start a new voice for the incoming note
- Add a configurable max voices per note (e.g., 2-4) to prevent runaway voice count
- Oldest voice gets killed if limit exceeded

**Trade-off:** Uses more voices and CPU, but produces more realistic piano behavior. Most users won't rapidly retrigger the same note constantly, so the impact is minimal in typical use.

## State Persistence

The plugin saves its state when your DAW project is saved, including:
- **Sample folder path** - automatically reloads samples when project opens
- **ADSR envelope settings** - attack, decay, sustain, release values
- **Preload size** - streaming buffer configuration
- **Transpose** - semitone offset
- **Sample Offset** - sample borrowing offset
- **Velocity Layer Limit** - reduced layer setting
- **Round Robin Limit** - reduced RR cycling

This means you can close a project and reopen it later with all your samples and settings intact.

### How It Works

The plugin implements the VST3/AU state persistence API:
- `getStateInformation()` - serializes plugin state to XML when DAW saves
- `setStateInformation()` - restores state from XML when DAW loads project

State is stored as XML with the following structure:
```xml
<HammerSamplerState sampleFolder="/path/to/samples"
                   attack="0.01" decay="0.1"
                   sustain="0.7" release="0.3"
                   preloadSizeKB="64"
                   transpose="0" sampleOffset="0"
                   velocityLayerLimit="4"
                   roundRobinLimit="3"/>
```

## Async Sample Loading

To prevent DAW projects from freezing during load, samples are loaded asynchronously:

1. **Project opens instantly** - `setStateInformation()` returns immediately
2. **Background thread** - preload buffers load on a separate thread
3. **Non-blocking** - you can interact with your DAW while samples load
4. **Thread-safe** - sample mappings are swapped atomically when ready

---

# DFD (Direct From Disk) Streaming

## Overview

The sampler uses Direct From Disk streaming to enable massive sample libraries (100GB+) that would never fit in RAM. Only the beginning of each sample is preloaded into memory - the rest is streamed from disk in real-time as notes play.

### RAM Usage

| Library Size | Samples | Preload Memory (64KB) |
|--------------|---------|----------------------|
| 1 GB | ~1000 | ~64 MB |
| 10 GB | ~5000 | ~320 MB |
| 100 GB | ~50000 | ~3.2 GB |

## Architecture

```
Note On
   │
   ▼
┌─────────────┐
│  Preload    │  ← Instant playback (first 32-1024KB)
│  Buffer     │
└─────────────┘
       │
       │ (preload exhausted)
       ▼
┌─────────────┐      ┌─────────────┐
│   Ring      │ ◄────│   Disk      │
│   Buffer    │      │   Thread    │
└─────────────┘      └─────────────┘
       │                    ▲
       │                    │
       ▼                    │
   Audio Out          Reads from file
```

### The Three Components

#### 1. Preload Buffer (per sample)
- First X KB of each sample loaded into RAM at instrument load time
- Configurable via **Preload** knob (32KB - 1024KB)
- Provides instant playback on note-on with no disk latency
- UI shows total preload RAM usage

#### 2. Ring Buffer (per voice)
- Each active voice has a 32,768 frame circular buffer (~743ms at 44.1kHz, ~341ms at 96kHz)
- Lock-free SPSC (Single Producer Single Consumer) design
- Audio thread reads, disk thread writes - no locks, no glitches

#### 3. Disk Streamer (background thread)
- Continuously monitors all active voices
- Fills ring buffers from disk when they run low
- Reads in 4,096 frame chunks for efficiency

## Ring Buffer Details

### Buffer Positions

```
[0]───────────────────────────────[32767]
         ▲              ▲
         │              │
    Read Position   Write Position
    (audio thread)  (disk thread)
```

The buffer is circular - positions wrap from end back to start.

### Watermarks & Thresholds

| Threshold | Frames | Purpose |
|-----------|--------|---------|
| **Ring buffer size** | 32,768 | Total capacity (~743ms at 44.1kHz) |
| **Low watermark** | 8,192 | When to request more data (~185ms) |
| **Disk read chunk** | 4,096 | Amount read per disk operation |

When available audio drops below the low watermark (8,192 frames), the voice signals `needsData = true` and the disk thread prioritizes filling that buffer.

### Buffer Health

```
32,768 ┬─── Full (disk thread idles)
       │
       │    Safe zone
       │
 8,192 ┼─── Low watermark (needsData = true)
       │
       │    Danger zone
       │
     0 ┴─── Underrun (audio glitch)
```

## UI Controls

### Preload Knob (32KB - 1024KB)
Controls how much of each sample is preloaded into RAM.

| Preload | Time at 44.1kHz | Time at 96kHz | Notes |
|---------|-----------------|---------------|-------|
| 32 KB | ~93ms | ~43ms | Minimum, needs fast disk |
| 64 KB | ~186ms | ~85ms | Good default |
| 128 KB | ~372ms | ~170ms | Safe for most systems |
| 256 KB | ~744ms | ~341ms | Very safe |
| 1024 KB | ~2.97s | ~1.37s | Maximum buffer time |

Larger preload = more RAM used, but more time for disk to catch up.
Smaller preload = less RAM, but more reliance on disk speed.

### Info Display
- **Size**: Total instrument file size on disk
- **RAM**: Memory used by preload buffers
- **Voices**: Active / Streaming voice counts
- **Disk**: Disk throughput in MB/s
- **Underruns**: Count of buffer underruns (click to reset)

## Thread Safety

The audio thread is real-time and never waits for disk. Communication is lock-free:

- **readPosition**: Audio thread writes (release), disk thread reads (acquire)
- **writePosition**: Disk thread writes (release), audio thread reads (acquire)
- **needsData**: Atomic flag for signaling

No mutexes in the audio path = no priority inversion = no glitches.

## Performance Guidelines

### Disk Speed Requirements

| Disk Type | Read Speed | Recommended Max Voices |
|-----------|------------|------------------------|
| HDD | ~100 MB/s | ~50 voices |
| SATA SSD | ~500 MB/s | ~200 voices |
| NVMe SSD | ~3000 MB/s | ~500+ voices |

*Based on 44.1kHz stereo float (~350KB/s per voice)*

### Optimization Tips

1. **Use SSD** - HDDs may struggle with many simultaneous voices
2. **Match sample rates** - 96kHz samples use 2x the bandwidth of 44.1kHz
3. **Increase preload** if you hear clicks on note attacks
4. **Reduce polyphony** if disk can't keep up

---

## Building

Requires JUCE framework installed at `~/JUCE`.

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Output plugins are in `build/HammerSampler_artefacts/Release/` (or `Debug/`):
- `VST3/Hammer Sampler.vst3`
- `Standalone/Hammer Sampler.app`

### Installation

Copy the built plugin to your system plugin folder:

**macOS:**
```bash
cp -R build/HammerSampler_artefacts/VST3/*.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

## Example Sample Library Structure

```
Piano Samples/
├── A0_040_01_piano.wav
├── A0_040_02_piano.wav
├── A0_040_03_piano.wav
├── A0_080_01_piano.wav
├── A0_080_02_piano.wav
├── A0_080_03_piano.wav
├── A0_100_01_piano.wav
├── A0_100_02_piano.wav
├── A0_100_03_piano.wav
├── A0_127_01_piano.wav
├── A0_127_02_piano.wav
├── A0_127_03_piano.wav
├── C1_040_01_piano.wav
...
```

---

## Future Improvements

Potential features to implement:

### Audio Features
- **Loop points** - For sustaining instruments (organs, pads) that need seamless looping
- **Release samples** - Trigger separate samples on note-off (piano key release sounds)
- **Pitch bend / mod wheel** - MIDI CC handling for expression
- **Per-note panning** - Stereo spread across the keyboard range

### Engine Improvements
- **Polyphonic same-note retrigger** - Allow multiple voices of the same note to coexist for realistic piano behavior (see Voice Stealing section)
- **Sample rate conversion** - Resample on-the-fly if samples don't match host rate
- **Legato mode** - Monophonic playing with glide

### UI/UX
- **Waveform display** - Show sample waveforms in the UI
- **Velocity curve editor** - Visual curve instead of just a knob
- **Preset browser** - Save/load instrument configurations

### Stability
- **Unit tests** - Test ring buffer, voice management, file parsing
- **Stress testing** - Automated polyphony torture tests

---

## Author

ALDENHammersmith

## License

MIT
