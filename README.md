# MIDI Keyboard Display & Sampler

A JUCE-based VST3/AU plugin that displays MIDI input on a visual 88-key keyboard and plays back samples mapped to notes, velocities, and round-robin positions. This is an MVP sampler designed for quick sample auditioning and performance.

## Features

- **Full 88-key piano display** (A0-C8) with octave labels (C1-C8) and sample availability coloring
- **DAW project state persistence** - samples and settings auto-reload when you reopen a project
- **Async sample loading** - projects load instantly, samples load in background thread
- Dynamic per-note grid showing velocity layers and round-robin positions
- Sample playback with velocity layers and round-robin cycling
- Pitch-shifting for notes using fallback samples
- Global ADSR envelope controls
- Sustain pedal support with visual feedback

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

Round-robin cycles through available samples (1 → 2 → 3 → 1) for natural variation when repeatedly playing the same note at similar velocities.

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
- **Rows**: Dynamic based on loaded samples (matches your velocity layers)
- **Cells**: 3 round-robin boxes (1, 2, 3) per velocity layer

**Grid colors:**
- **Blue**: Active velocity layer and round-robin position
- **Dim blue**: Active velocity layer, different round-robin
- **Dark grey**: Available but not active
- **Very dark grey**: Unavailable (no samples for this note/layer)

When a note is played:
- The corresponding key lights up blue
- The velocity layer row for that note activates
- The round-robin box shows which position triggered

With sustain pedal held, all triggered states accumulate and persist until pedal release.

### ADSR Controls

Four rotary knobs control the global amplitude envelope:
- **A (Attack)**: 0.001 - 2.0 seconds
- **D (Decay)**: 0.001 - 2.0 seconds
- **S (Sustain)**: 0.0 - 1.0 level
- **R (Release)**: 0.001 - 3.0 seconds

## State Persistence

The plugin saves its state when your DAW project is saved, including:
- **Sample folder path** - automatically reloads samples when project opens
- **ADSR envelope settings** - attack, decay, sustain, release values

This means you can close a project and reopen it later with all your samples and settings intact.

### How It Works

The plugin implements the VST3/AU state persistence API:
- `getStateInformation()` - serializes plugin state to XML when DAW saves
- `setStateInformation()` - restores state from XML when DAW loads project

State is stored as XML with the following structure:
```xml
<MidiKeyboardState sampleFolder="/path/to/samples"
                   attack="0.01" decay="0.1"
                   sustain="0.7" release="0.3"/>
```

## Async Sample Loading

To prevent DAW projects from freezing during load, samples are loaded asynchronously:

1. **Project opens instantly** - `setStateInformation()` returns immediately
2. **Background thread** - samples load on a separate thread
3. **Non-blocking** - you can interact with your DAW while samples load
4. **Thread-safe** - sample mappings are swapped atomically when ready

### Parallel Loading

For maximum speed, samples are loaded in parallel using `std::async`:

```cpp
// Each sample file loads on its own thread simultaneously
for (const auto& file : filesToLoad)
{
    futures.push_back(std::async(std::launch::async, [file]() {
        // Load audio file in parallel
        return loadedSample;
    }));
}
```

**Performance impact:**
- Sequential loading: ~20 seconds for large libraries
- Parallel loading: ~5 seconds (4x faster)

The number of concurrent loads scales with your CPU cores, maximizing disk I/O throughput.

## Building

Requires JUCE framework installed at `~/JUCE`.

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Output plugins are in `build/MidiKeyboardDisplay_artefacts/Release/`:
- `VST3/MIDI Keyboard Display.vst3`
- `AU/MIDI Keyboard Display.component`
- `Standalone/MIDI Keyboard Display.app`

### Installation

Copy the built plugin to your system plugin folder:

**macOS:**
```bash
# VST3
cp -R build/MidiKeyboardDisplay_artefacts/Release/VST3/*.vst3 ~/Library/Audio/Plug-Ins/VST3/

# AU
cp -R build/MidiKeyboardDisplay_artefacts/Release/AU/*.component ~/Library/Audio/Plug-Ins/Components/
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

# DFD (Direct From Disk) Streaming

## Overview

The sampler supports two modes:
- **RAM Mode**: Full samples loaded into memory (original behavior)
- **Streaming Mode**: Only preload buffers in RAM, rest streamed from disk

Toggle between modes with the **Streaming** checkbox. Streaming mode enables massive sample libraries (100GB+) that would never fit in RAM.

### RAM Usage Comparison

| Library Size | RAM Mode | Streaming (64KB preload) | Reduction |
|--------------|----------|--------------------------|-----------|
| 1 GB (1000 samples) | 1 GB | ~64 MB | 94% less |
| 10 GB (5000 samples) | 10 GB | ~320 MB | 97% less |
| 100 GB (50000 samples) | Impossible | ~3.2 GB | ✅ Viable |

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

### Streaming Toggle
- **Off**: RAM mode - full samples in memory
- **On**: Streaming mode - preload + disk streaming

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
- **RAM**: Memory used by preload buffers (streaming mode)
- **Voices**: Number of active voices playing
- **Disk**: Voices currently requesting disk reads

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

## Author

ALDENHammersmith

## License

MIT
