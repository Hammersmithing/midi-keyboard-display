# MIDI Keyboard Display & Sampler

A JUCE-based VST3/AU plugin that displays MIDI input on a visual keyboard and plays back samples mapped to notes, velocities, and round-robin positions.

## Features

- Visual 3-octave keyboard display (C3-B5) with sample availability coloring
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

The 3-octave keyboard (C3-B5) shows sample availability with color coding:
- **White/Black (normal)**: Note has its own samples
- **Light grey**: Note uses fallback samples (pitch-shifted from higher note)
- **Dark grey**: Note is unavailable (no samples or fallback)
- **Blue**: Note is currently pressed

### Note Grid

The grid above the keyboard shows:
- **Columns**: One per note (36 notes, C3-B5)
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

## Author

ALDENHammersmith

## License

MIT
