# Pulsar Synthesis — disting NT Plugin

A [pulsar synthesis](https://en.wikipedia.org/wiki/Pulsar_synthesis) instrument plugin for the [Expert Sleepers disting NT](https://expert-sleepers.co.uk/distingNT.html) Eurorack module, based on Curtis Roads' technique of generating trains of short sonic particles (pulsarets) windowed within a fundamental period.

## Features

- **10 pulsaret waveforms** — sine, sine×2, sine×3, sinc, triangle, saw, square, formant, pulse, noise — with continuous morphing between adjacent shapes
- **5 window functions** — rectangular, gaussian, Hann, exponential decay, linear decay — with continuous morphing
- **1–3 parallel formants** with independent frequency control and constant-power stereo panning
- **Masking** — stochastic (probability-based) and burst (on/off pattern) modes for rhythmic textures
- **ASR envelope** with configurable attack/release and MIDI velocity sensitivity
- **4 CV inputs** — pitch (1V/oct), formant modulation, duty cycle, mask amount
- **Sample-based pulsarets** — load WAV files from SD card as custom pulsaret waveforms with adjustable playback rate
- **Waveform display** — real-time visualization of pulsaret × window shape, envelope bar, frequency readout, gate indicator
- **Preset serialization** — sample file selection saved/restored with presets

## Parameters

31 parameters across 8 pages:

| Page | Parameters |
|------|-----------|
| **Synthesis** | Pulsaret (0.0–9.0), Window (0.0–4.0), Duty Cycle (1–100%), Duty Mode (Manual/Formant) |
| **Formants** | Count (1–3), Formant 1/2/3 Hz (20–8000) |
| **Masking** | Mode (Off/Stochastic/Burst), Amount (0–100%), Burst On (1–16), Burst Off (0–16) |
| **Envelope** | Attack (0.1–2000 ms), Release (1–3200 ms), Amplitude (0–100%), Glide (0–2000 ms) |
| **Panning** | Pan 1/2/3 (-100 to +100) |
| **Sample** | Use Sample (Off/On), Folder, File, Sample Rate (25–400%) |
| **CV Inputs** | Pitch CV, Formant CV, Duty CV, Mask CV (bus selectors) |
| **Routing** | MIDI Ch (1–16), Output L, Output R |

Unused parameters are automatically grayed out based on context (e.g., formant 2/3 when count=1, burst params when mask mode is not burst).

## Signal Chain

```
MIDI Note → Frequency (with glide) → Master Phase Oscillator
  → Pulse Trigger → Mask Decision (stochastic/burst)
  → For each formant (1–3):
      Pulsaret (table morph or sample) × Window (table morph) × Mask
      → Constant-power pan → Stereo accumulate
  → Normalize → ASR Envelope × Velocity × Amplitude
  → DC-blocking highpass → Soft clip (Padé tanh)
  → Output L/R
```

## Building

### Requirements

- ARM GCC toolchain (`arm-none-eabi-c++`)
- [disting NT API](https://expert-sleepers.co.uk/distingNTSDK.html) (included as submodule)

### Build

```sh
git clone --recursive https://github.com/sroons/disting-pulsar.git
cd disting-pulsar
make
```

This produces `plugins/pulsar.o`.

### Install

Copy `plugins/pulsar.o` to the `plugins/` folder on your disting NT's SD card and reboot the module.

## Usage

1. Add the **Pulsar** algorithm to a slot on the disting NT
2. Set your MIDI channel on the **Routing** page and assign outputs
3. Play notes via MIDI — the plugin responds to note on/off with velocity
4. Shape the sound on the **Synthesis** page by sweeping Pulsaret and Window morphing controls
5. Add parallel formants on the **Formants** page and spread them with **Panning**
6. Create rhythmic textures with **Masking** (stochastic for random dropouts, burst for repeating patterns)
7. Patch CV sources to modulate pitch, formant frequency, duty cycle, or mask amount in real time
8. Optionally load a WAV file from the SD card as a custom pulsaret waveform on the **Sample** page

## License

Plugin source code is provided as-is. The disting NT API is copyright Expert Sleepers Ltd under the MIT License.
