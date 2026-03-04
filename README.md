# Crab Nebula — Pulsar Synthesis for disting NT

A [pulsar synthesis](https://en.wikipedia.org/wiki/Pulsar_synthesis) instrument plugin for the [Expert Sleepers disting NT](https://expert-sleepers.co.uk/distingNT.html) Eurorack module.

## What Is Pulsar Synthesis?

Pulsar synthesis is a sound synthesis technique developed by [Curtis Roads](https://www.curtisroads.net/) and Alberto de Campo in the early 2000s, described in Roads' book [*Microsound*](https://mitpress.mit.edu/9780262681544/microsound/) (MIT Press, 2001). It belongs to the family of granular and particle-based synthesis methods that operate on the *micro-timescale* of sound — durations below about 100 milliseconds where individual sonic events blur into continuous tones and textures.

### Core Concept

A **pulsar** is a brief burst of sound (a **pulsaret**) shaped by an amplitude envelope (a **window function**), repeated at a controllable fundamental frequency. The train of pulsars creates a pitched tone whose timbre is determined by three factors:

1. **The pulsaret waveform** — the waveshape inside each pulse (sine, saw, formant-like shapes, noise, etc.)
2. **The window function** — the amplitude envelope applied to each pulsaret (Hann, Gaussian, exponential decay, etc.)
3. **The duty cycle** — what fraction of each fundamental period contains active sound vs. silence

When the duty cycle is 100%, every period is filled with sound and the result resembles classic wavetable synthesis. As the duty cycle decreases, each pulsaret occupies a shorter fraction of the period, introducing silence gaps that create a characteristic hollow, buzzing, or clicking quality. At very low duty cycles the individual pulsarets become audible as distinct sonic particles.

### Formants and Spectral Shaping

A key feature of pulsar synthesis is **formant control**. The pulsaret waveform can cycle at a frequency independent of the fundamental, creating fixed spectral peaks (formants) similar to vowel sounds in speech. By setting formant frequencies to values like 200 Hz, 800 Hz, or 2000 Hz, you can sculpt vowel-like resonances that remain constant as the fundamental pitch changes — just as human vocal formants stay roughly fixed regardless of the pitch being spoken or sung.

This plugin extends the original technique with up to **3 independent formant oscillators**, each with its own frequency, stereo pan position, and CV input. The formants sum together before the envelope and output stages, enabling rich spectral structures that evolve under voltage control.

### Masking

Roads also introduced **masking** — selectively muting pulses within the train to create rhythmic or stochastic textures. In *stochastic masking*, each pulse has a random probability of being silenced, producing granular, cloud-like dissolution of the tone. In *burst masking*, a repeating on/off pattern (e.g., 4 pulses on, 2 off) creates metrically structured rhythms at the micro-timescale.

### Further Reading

- Curtis Roads, [*Microsound*](https://mitpress.mit.edu/9780262681544/microsound/) (MIT Press, 2001) — the definitive reference
- Curtis Roads, ["Sound Composition with Pulsars"](https://doi.org/10.2307/3681778), *Journal of the Audio Engineering Society*, 2001
- [Wikipedia: Pulsar Synthesis](https://en.wikipedia.org/wiki/Pulsar_synthesis)
- Curtis Roads, [*The Computer Music Tutorial*](https://mitpress.mit.edu/9780262680820/the-computer-music-tutorial/) (MIT Press, 1996) — broader context on granular and particle synthesis
- Alberto de Campo, [*Microsound*](https://www.albertodecampo.at/) — de Campo's related work on implementations

## Features

- **10 pulsaret waveforms** — sine, sine×2, sine×3, sinc, triangle, saw, square, formant, pulse, noise — with continuous morphing between adjacent shapes
- **5 window functions** — rectangular, Gaussian, Hann, exponential decay, linear decay — with continuous morphing
- **1–3 parallel formants** with independent frequency control, per-formant CV modulation, and constant-power stereo panning
- **Masking** — stochastic (probability-based) and burst (on/off pattern) modes for rhythmic textures
- **Free Run mode** (default) — generates sound immediately without MIDI; pitch set by Base Pitch parameter + Pitch CV
- **Per-pulse AR envelope** in Free Run mode (retriggers each pulse); standard ASR in MIDI mode
- **12 bipolar CV inputs** — pitch (1V/oct), duty, mask, pulsaret morph, window morph, amplitude, formant 1/2/3 Hz, pan 1, attack, release — all 12 inputs used by default
- **Sample-based pulsarets** — load WAV files from SD card as custom pulsaret waveforms with adjustable playback rate
- **Real-time display** — waveform preview responds to CV modulation, formant Hz readouts, amplitude %, envelope bar, frequency readout, gate indicator, peak output meter

## Parameters

42 parameters across 11 pages:

| Page | Parameter | Range | Default |
|------|-----------|-------|---------|
| **Synthesis** | Pulsaret | 0.0–9.0 | 2.5 |
| | Window | 0.0–4.0 | 0.5 |
| | Duty Cycle | 1–100% | 50% |
| | Duty Mode | Manual / Formant | Manual |
| **Formants** | Formant Count | 1–3 | 2 |
| | Formant 1 Hz | 20–2000 Hz | 20 Hz |
| | Formant 2 Hz | 20–2000 Hz | 200 Hz |
| | Formant 3 Hz | 20–2000 Hz | 400 Hz |
| **Masking** | Mask Mode | Off / Stochastic / Burst | Off |
| | Mask Amount | 0–100% | 50% |
| | Burst On | 1–16 | 4 |
| | Burst Off | 0–16 | 4 |
| **Envelope** | Attack | 0.1–2000 ms | 10 ms |
| | Release | 1.0–3200 ms | 200 ms |
| | Amplitude | 0–100% | 25% |
| | Glide | 0–2000 ms | 0 ms |
| **Panning** | Pan 1 | -100 to +100 | 0 |
| | Pan 2 | -100 to +100 | -50 |
| | Pan 3 | -100 to +100 | +50 |
| **Sample** | Use Sample | Off / On | Off |
| | Folder | (SD card) | — |
| | File | (SD card) | — |
| | Sample Rate | 25–400% | 100% |
| **CV Inputs** | *(see CV table below)* | | |
| **Routing** | Gate Mode | MIDI / Free Run | Free Run |
| | Base Pitch | MIDI note 0–127 | C1 (24) |
| | MIDI Ch | 1–16 | 1 |
| | Output L | Bus 1–28 | Bus 13 |
| | Output R | Bus 1–28 | Bus 14 |

Unused parameters are automatically grayed out based on context (e.g., Formant 2/3 Hz when count=1, burst params when mask mode is not burst, Base Pitch in MIDI mode, MIDI Ch in Free Run mode).

## CV Inputs

All CV inputs are **bipolar** (±5V). Each is routable to any of the 28 buses (1–12 inputs, 13–20 outputs, 21–28 aux), or 0 for none.

| CV Input | Default Bus | Scaling | Effect |
|----------|-------------|---------|--------|
| Pitch CV | Input 1 | 1V/oct exponential | Per-sample pitch modulation |
| Duty CV | Input 3 | ±5V → ±20% offset | Duty cycle offset added to base |
| Mask CV | Input 4 | ±5V → ±50% offset | Mask amount offset (bipolar) |
| Pulsaret CV | Input 5 | ±5V → full range | Sweeps pulsaret morph ±4.5 |
| Window CV | Input 6 | ±5V → full range | Sweeps window morph ±2.0 |
| Amplitude CV | Input 2 | ±5V → ±50% offset | Amplitude offset added to base |
| Formant 1 CV | Input 7 | ±5V → ±1000 Hz | Formant 1 frequency offset |
| Formant 2 CV | Input 8 | ±5V → ±1000 Hz | Formant 2 frequency offset |
| Formant 3 CV | Input 9 | ±5V → ±1000 Hz | Formant 3 frequency offset |
| Pan 1 CV | Input 10 | ±5V → ±100% offset | Formant 1 stereo pan position |
| Attack CV | Input 11 | ±5V → ±1000 ms | Envelope attack time offset |
| Release CV | Input 12 | ±5V → ±1600 ms | Envelope release time offset |

CV modulation is applied as an offset on top of the parameter's base value (set by knob or parameter page). All CVs except Pitch are block-rate averaged. Pitch CV is processed per-sample for accurate 1V/oct tracking.

## Signal Chain

```
Pitch Source (MIDI note or Base Pitch + Pitch CV) → Frequency (with glide)
  → Master Phase Oscillator → Pulse Trigger → Mask Decision (stochastic/burst)
  → For each formant (1–3):
      Pulsaret (table morph or sample) × Window (table morph) × Mask
      → Constant-power pan → Stereo accumulate
  → Normalize → Envelope (per-pulse AR in Free Run, ASR in MIDI) × Velocity × Amplitude
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

This produces `plugins/crab_nebula.o`.

### Install

Copy `plugins/crab_nebula.o` to the `plugins/` folder on your disting NT's SD card and reboot the module.

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

### Outputs

Output L and Output R are routable to any output bus via the **Routing** page. Default: L = bus 13, R = bus 14.

## Display

The custom display shows (256×64 px, standard parameter line at top):

- **Waveform preview** — real-time pulsaret × window shape, responds to CV modulation of pulsaret, window, duty, and formant parameters
- **Fundamental Hz** — current oscillator frequency
- **Formant count** — "1F", "2F", or "3F"
- **Envelope bar** — animated ASR envelope level
- **Gate indicator** — lit when gate is open
- **"FR" label** — shown when in Free Run mode
- **Peak output meter** — below waveform, shows output level
- **F1/F2/F3 Hz readouts** — effective formant frequencies after CV modulation (inactive formants shown dimmed)
- **Amplitude %** — effective amplitude after CV modulation

## Usage

1. Add the **Crab Nebula** algorithm to a slot on the disting NT
2. In **Free Run** mode (default), sound starts immediately at C1 (~32.7 Hz)
3. Shape the sound on the **Synthesis** page by sweeping Pulsaret and Window morphing controls (or use the pots)
4. Add parallel formants on the **Formants** page and spread them with **Panning**
5. Create rhythmic textures with **Masking** (stochastic for random dropouts, burst for repeating patterns)
6. Patch CV sources to modulate formant frequencies, duty cycle, mask amount, pulsaret/window morph, amplitude, pan, attack, or release — all 12 inputs are assigned by default
7. For MIDI control, switch **Gate Mode** to MIDI on the **Routing** page and set your MIDI channel
8. Optionally load a WAV file from the SD card as a custom pulsaret waveform on the **Sample** page

## License

Plugin source code is provided as-is. The disting NT API is copyright Expert Sleepers Ltd under the MIT License.
