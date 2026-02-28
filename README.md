# Pulsar Synthesis — disting NT Plugin

A [pulsar synthesis](https://en.wikipedia.org/wiki/Pulsar_synthesis) instrument plugin for the [Expert Sleepers disting NT](https://expert-sleepers.co.uk/distingNT.html) Eurorack module, based on Curtis Roads' technique of generating trains of short sonic particles (pulsarets) windowed within a fundamental period.

## Features

- **10 pulsaret waveforms** — sine, sine×2, sine×3, sinc, triangle, saw, square, formant, pulse, noise — with continuous morphing between adjacent shapes
- **5 window functions** — rectangular, gaussian, Hann, exponential decay, linear decay — with continuous morphing
- **1–3 parallel formants** with independent frequency control and constant-power stereo panning
- **Masking** — stochastic (probability-based) and burst (on/off pattern) modes for rhythmic textures
- **Free Run mode** — generate sound without MIDI; pitch controlled by Base Pitch parameter and Pitch CV
- **ASR envelope** with configurable attack/release and MIDI velocity sensitivity
- **9 CV inputs** — pitch (1V/oct), formant, duty, mask, pulsaret morph, window morph, glide, sample rate, amplitude
- **Sample-based pulsarets** — load WAV files from SD card as custom pulsaret waveforms with adjustable playback rate
- **Waveform display** — real-time visualization of pulsaret × window shape, envelope bar, frequency readout, gate indicator
- **Preset serialization** — sample file selection saved/restored with presets

## Parameters

38 parameters across 10 pages:

| Page | Parameters |
|------|-----------|
| **Synthesis** | Pulsaret (0.0–9.0), Window (0.0–4.0), Duty Cycle (1–100%), Duty Mode (Manual/Formant) |
| **Formants** | Count (1–3), Formant 1/2/3 Hz (20–8000) |
| **Masking** | Mode (Off/Stochastic/Burst), Amount (0–100%), Burst On (1–16), Burst Off (0–16) |
| **Envelope** | Attack (0.1–2000 ms), Release (1–3200 ms), Amplitude (0–100%), Glide (0–2000 ms) |
| **Panning** | Pan 1/2/3 (-100 to +100) |
| **Sample** | Use Sample (Off/On), Folder, File, Sample Rate (25–400%) |
| **CV Inputs** (3 pages) | Pitch CV, Formant CV, Duty CV, Mask CV, Pulsaret CV, Window CV, Glide CV, Sample Rate CV, Amplitude CV |
| **Routing** | Gate Mode (MIDI/Free Run), Base Pitch (0–127, default A4), MIDI Ch (1–16), Output L, Output R |

Unused parameters are automatically grayed out based on context (e.g., formant 2/3 when count=1, burst params when mask mode is not burst, Base Pitch in MIDI mode, MIDI Ch in Free Run mode).

## Signal Chain

```
Pitch Source (MIDI note or Base Pitch) → Frequency (with glide)
  → Master Phase Oscillator → Pulse Trigger → Mask Decision (stochastic/burst)
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

## Hardware Controls

Encoders and buttons not listed below retain their standard disting NT navigation behavior.

### Pots

| Pot | Parameter | Range |
|-----|-----------|-------|
| Left | Pulsaret morph | 0.0–9.0 (sweeps all 10 waveforms) |
| Centre | Duty Cycle | 1–100% |
| Right | Window morph | 0.0–4.0 (sweeps all 5 windows) |

### Encoder Buttons

| Button | Action |
|--------|--------|
| Left encoder button | Cycle mask mode: Off → Stochastic → Burst → Off |
| Right encoder button | Cycle formant count: 1 → 2 → 3 → 1 |

### CV Inputs

CV inputs are routable via bus selectors on the **CV Inputs** pages — set each to any of the 64 busses (12 hardware inputs + 8 outputs + 44 aux), or 0 for none.

| Parameter | Default | Function |
|-----------|---------|----------|
| Pitch CV | Input 1 | 1V/oct frequency modulation (per-sample) |
| Formant CV | Input 2 | Bipolar formant Hz modulation (±50% at ±5V) |
| Duty CV | Input 3 | Bipolar duty cycle offset (±20% at ±5V) |
| Mask CV | Input 4 | Unipolar mask amount (0–10V maps to 0–1) |
| Pulsaret CV | Input 5 | Bipolar pulsaret morph (±5V sweeps full range) |
| Window CV | Input 6 | Bipolar window morph (±5V sweeps full range) |
| Glide CV | Input 7 | Unipolar glide time (0–10V maps to 0–2000ms) |
| Sample Rate CV | Input 8 | Bipolar sample rate offset (±5V maps to ±2x) |
| Amplitude CV | Input 12 | Unipolar amplitude (0–10V maps to 0–1) |

### Outputs

Output L and Output R are routable to any output bus via the **Routing** page.

## Usage

1. Add the **Crab Nebula** algorithm to a slot on the disting NT
2. On the **Routing** page, choose **Gate Mode**:
   - **MIDI** (default) — set your MIDI channel, assign outputs, and play notes via MIDI
   - **Free Run** — sound starts immediately at the Base Pitch; no MIDI required
3. Shape the sound on the **Synthesis** page by sweeping Pulsaret and Window morphing controls
4. Add parallel formants on the **Formants** page and spread them with **Panning**
5. Create rhythmic textures with **Masking** (stochastic for random dropouts, burst for repeating patterns)
6. Patch CV sources to modulate pitch, formant frequency, duty cycle, mask amount, pulsaret/window morph, glide, sample rate, or amplitude in real time
7. Optionally load a WAV file from the SD card as a custom pulsaret waveform on the **Sample** page

## License

Plugin source code is provided as-is. The disting NT API is copyright Expert Sleepers Ltd under the MIT License.
