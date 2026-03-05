# Spaluter — Pulsar Synthesis for disting NT

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
- **1–4 voice polyphony** — play chords in MIDI mode (with voice stealing) or stack harmonic intervals in Free Run mode
- **14 chord types** — Unison, Octaves, Fifths, Sub+Oct, Major, Minor, Maj7, Min7, Sus4, Dom7, Dim, Aug, Power, Open5th — for Free Run interval stacking
- **Free Run mode** (default) — generates sound immediately without MIDI; pitch set by Base Pitch parameter + Pitch CV
- **Per-pulse AR envelope** in Free Run mode (retriggers each pulse); standard ASR in MIDI mode
- **12 bipolar CV inputs** — pitch (1V/oct), duty, mask, pulsaret morph, window morph, amplitude, formant 1/2/3 Hz, pan 1, attack, release — all 12 inputs used by default
- **Sample-based pulsarets** — load WAV files from SD card as custom pulsaret waveforms with adjustable playback rate
- **Real-time display** — waveform preview responds to CV modulation, formant Hz readouts, amplitude %, envelope bar, frequency readout, gate indicator, peak output meter

## Parameters

44 parameters across 12 pages:

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
| | Amplitude | 0–100% | 0% |
| | Glide | 0–2000 ms | 0 ms |
| **Panning** | Pan 1 | -100 to +100 | 0 |
| | Pan 2 | -100 to +100 | -50 |
| | Pan 3 | -100 to +100 | +50 |
| **Polyphony** | Voice Count | 1–4 | 1 |
| | Chord Type | Unison / Octaves / Fifths / Sub+Oct / Major / Minor / Maj7 / Min7 / Sus4 / Dom7 / Dim / Aug / Power / Open5th | Unison |
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

Unused parameters are automatically grayed out based on context (e.g., Formant 2/3 Hz when count=1, burst params when mask mode is not burst, Base Pitch in MIDI mode, MIDI Ch in Free Run mode, Chord Type in MIDI mode).

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
Pitch Source (MIDI note or Base Pitch × Chord Ratio + Pitch CV) → Frequency (with glide)
  → For each voice (1–4):
      Master Phase Oscillator → Pulse Trigger → Mask Decision (stochastic/burst)
      → For each formant (1–3):
          Pulsaret (table morph or sample) × Window (table morph) × Mask
          → Constant-power pan → Stereo accumulate
      → Normalize → Envelope (per-pulse AR in Free Run, ASR in MIDI) × Velocity × Amplitude
      → DC-blocking highpass
  → Sum voices → Normalize by voice count → Soft clip (Padé tanh)
  → Output L/R
```

## Installation

A pre-built binary is included in the repository — no toolchain required.

1. Download [`plugins/spaluter.o`](plugins/spaluter.o) from this repository (or clone the repo)
2. Remove the flash drive from your disting NT
3. Plug the flash drive into your computer
4. Copy `spaluter.o` to the `programs/plug-ins` folder on the flash drive
5. Eject the flash drive from your computer
6. Plug the flash drive back into the disting NT
7. Restart the module

Spaluter should now appear as an available plugin to load.

## Getting Sound

Spaluter starts silent by default — the base amplitude is 0%. There are two ways to get audible output:

### Option A: Use CV inputs (recommended)

Spaluter is designed to be played via CV. At minimum, patch signals into:

1. **Input 1** (Pitch CV) — a 1V/oct pitch source (sequencer, keyboard, etc.) controls the fundamental frequency
2. **Input 2** (Amplitude CV) — a bipolar signal (envelope, LFO, etc.) controls the output level. Positive voltage opens up the amplitude; at +5V you get 50% amplitude

With just these two inputs patched, you'll hear sound. The other 10 CV inputs (duty, mask, pulsaret/window morph, formant frequencies, pan, attack, release) add further modulation when patched.

### Option B: Set parameters manually

If you want sound without any CV patched:

1. Navigate to the **Envelope** page
2. Raise **Amplitude** above 0% (try 50–80%)
3. Sound will begin immediately in Free Run mode at the default pitch (C1, ~32.7 Hz)
4. Adjust **Base Pitch** on the **Routing** page to change the fundamental frequency

You can combine both approaches — set a base amplitude manually and let CV modulate it further.

## Building from Source

If you want to modify the plugin and rebuild:

### Requirements

- ARM GCC toolchain (`arm-none-eabi-c++`)
- [disting NT API](https://expert-sleepers.co.uk/distingNTSDK.html) (included as submodule)

### Build

```sh
git clone --recursive https://github.com/sroons/disting-pulsar.git
cd disting-pulsar
make
```

This produces `plugins/spaluter.o`.

## Hardware Controls

Encoders and buttons not listed below retain their standard disting NT navigation behavior.

### Pots

| Pot | Parameter | Range |
|-----|-----------|-------|
| Left | Pulsaret morph | 0.0–9.0 (sweeps all 10 waveforms) |
| Centre | Duty Cycle | 1–100% |
| Right | Window morph | 0.0–4.0 (sweeps all 5 windows) |

### Buttons

| Button | Action |
|--------|--------|
| Left encoder button | Cycle mask mode: Off → Stochastic → Burst → Off |
| Right encoder button | Cycle formant count: 1 → 2 → 3 → 1 |
| Button 3 | Cycle voice count: 1 → 2 → 3 → 4 → 1 |
| Button 4 | Cycle chord type (14 options) |

### Outputs

Output L and Output R are routable to any output bus via the **Routing** page. Default: L = bus 13, R = bus 14.

## Display

The custom display shows (256×64 px, standard parameter line at top):

- **Waveform preview** — real-time pulsaret × window shape, responds to CV modulation of pulsaret, window, duty, and formant parameters
- **Fundamental Hz** — current oscillator frequency
- **Formant count + voice count** — "2F 4V"
- **Chord type label** — "MAJ", "OCT", etc. (Free Run mode; dimmed when voice count is 1)
- **Envelope bar** — max envelope across all voices
- **Gate indicator** — lit when any voice gate is open
- **"FR" label** — shown when in Free Run mode
- **Peak output meter** — below waveform, shows output level
- **F1/F2/F3 Hz readouts** — effective formant frequencies after CV modulation (inactive formants shown dimmed)
- **Amplitude %** — effective amplitude after CV modulation

## Usage

1. Add the **Spaluter** algorithm to a slot on the disting NT
2. Patch a signal into **Input 2** (Amplitude CV) or raise **Amplitude** manually — the plugin starts silent by default
3. Shape the sound on the **Synthesis** page by sweeping Pulsaret and Window morphing controls (or use the pots)
4. Add parallel formants on the **Formants** page and spread them with **Panning**
5. Create rhythmic textures with **Masking** (stochastic for random dropouts, burst for repeating patterns)
6. Patch CV sources to modulate formant frequencies, duty cycle, mask amount, pulsaret/window morph, amplitude, pan, attack, or release — all 12 inputs are assigned by default
7. Add voices on the **Polyphony** page — in Free Run mode, choose a **Chord Type** to stack intervals; in MIDI mode, play chords
8. For MIDI control, switch **Gate Mode** to MIDI on the **Routing** page and set your MIDI channel
9. Optionally load a WAV file from the SD card as a custom pulsaret waveform on the **Sample** page

## Sound Design Tips

### Formant Frequencies — the biggest tonal lever

The formant frequencies have the most dramatic effect on timbre. They determine the spectral peaks of the sound, much like vowel formants in speech.

- **Low formants (20–100 Hz)** produce deep, subharmonic rumbles. Set Formant 1 to 20 Hz for a thick bass drone.
- **Vowel-like tones**: Try F1=270, F2=2300 for an "ah" sound, or F1=300, F2=870 for an "oo." Experiment with two or three formants tuned to vocal formant charts.
- **Harmonic stacking**: Set formants to integer multiples of the fundamental (e.g., if Base Pitch gives you ~130 Hz, try formants at 130, 260, 390) for bright, reinforced harmonics.
- **Inharmonic/metallic tones**: Set formants to non-integer ratios of each other (e.g., F1=200, F2=347, F3=511) for bell-like or metallic timbres.
- **CV modulation of formants** creates the most expressive results — patch an LFO into Formant 1 CV to sweep a resonant peak through the spectrum in real time.

### Pulsaret Waveform — harmonic character

The pulsaret waveform sets the raw harmonic content inside each pulse.

- **Sine (0.0)** is pure and clean — good starting point for isolating formant effects.
- **Sine harmonics (1.0–2.0)** add upper partials without harshness.
- **Sinc (3.0)** produces a band-limited impulse with natural sidelobes — characteristic pulsar synthesis sound. This is the closest to Roads' original work.
- **Triangle/Saw (4.0–5.0)** bring familiar subtractive-style brightness.
- **Square (6.0)** creates hollow, clarinet-like tones.
- **Formant (7.0)** has a built-in resonant peak — stacking this with the formant frequency parameters creates double-resonance effects.
- **Pulse (8.0)** is extremely bright and nasal.
- **Noise (9.0)** replaces pitched content with noise bursts — useful for percussion or breathy textures.
- **Morph between adjacent shapes** using fractional values (e.g., 3.5 blends sinc and triangle) for timbres that don't exist in any single waveform.

### Duty Cycle — density and hollowness

Duty cycle controls what fraction of each period contains sound.

- **High duty (80–100%)** fills the period with sound, approaching classic wavetable synthesis. Rich and full.
- **Medium duty (30–60%)** introduces silence gaps that create the characteristic pulsar "buzz." This is the sweet spot for most pulsar patches.
- **Low duty (5–20%)** produces sparse, clicking, particle-like textures. Individual pulsarets become audible events.
- **Formant Duty Mode** ties duty to the formant frequency automatically — as formant frequency rises, duty shrinks to keep the pulsaret waveform cycles consistent.

### Window Function — attack shape of each pulse

The window shapes how each pulsaret fades in and out.

- **Rectangular (0.0)** gives hard edges — maximum brightness and click.
- **Gaussian (1.0)** is smooth and gentle — the most natural-sounding window.
- **Hann (2.0)** is similar to Gaussian but with a slightly flatter top — good general-purpose choice.
- **Exponential decay (3.0)** creates plucked or percussive attacks that ring out.
- **Linear decay (4.0)** is a simpler percussive shape.
- Pair **exponential decay window + sinc pulsaret** for a classic plucked pulsar tone.

### Masking — rhythm and texture

Masking selectively mutes pulses to break up the continuous tone.

- **Stochastic masking** at low amounts (10–30%) adds subtle grit and instability, like a rough surface on the sound. At high amounts (70–90%) the tone dissolves into a sparse cloud of particles.
- **Burst masking** creates repeating rhythmic patterns at the micro-timescale. Try Burst On=1, Off=3 for a stuttering rhythm, or On=7, Off=1 for a subtle hiccup.
- **CV-controlled mask amount** lets you crossfade between a solid tone and granular disintegration in real time.

### Panning — stereo width

With 2 or 3 formants active, spreading their pan positions creates wide stereo images.

- **Pan 1=−100, Pan 2=0, Pan 3=+100** spreads formants hard left, center, and hard right for maximum width.
- **Subtle spread (−30/0/+30)** keeps the sound centered but adds dimension.
- **CV on Pan 1** with an LFO creates spatial movement.

### Envelope — shaping the overall amplitude

- **Short attack + short release (1–5 ms each)** in Free Run mode creates sharp, percussive clicks that retrigger on every pulse — excellent for rhythmic textures.
- **Longer attack (50–200 ms)** softens each pulse onset for pad-like sounds.
- **Long release (500–2000 ms)** in MIDI mode creates sustained, reverb-like tails after note-off.

### Polyphony — chords and interval stacking

Voice Count adds up to 4 simultaneous voices. In MIDI mode, this enables playing chords with automatic voice allocation and voice stealing. In Free Run mode, additional voices are tuned relative to the base pitch according to the selected Chord Type.

**Harmonic intervals** (frequency ratios):
- **Unison** — all voices at the same pitch; use for thickening via detuned formants or masking variation
- **Octaves** (1×, 2×, 4×, 8×) — wide, organ-like stacking spanning 3 octaves
- **Fifths** (1×, 1.5×, 2×, 3×) — open, consonant, medieval quality
- **Sub+Oct** (0.5×, 1×, 2×, 4×) — adds a sub-octave below the base pitch

**Tonal chords** (equal temperament):
- **Major / Minor** — standard triads with octave doubling in voice 4
- **Maj7 / Min7 / Dom7** — jazz voicings; pair with sine pulsaret + Gaussian window for warm pad chords
- **Sus4** — ambiguous, unresolved tension
- **Dim / Aug** — dissonant, unstable intervals; effective with stochastic masking for unsettling textures
- **Power** (root, 5th, oct, oct+5th) — heavy, distorted-guitar-style voicing
- **Open5th** (root, 5th, oct, oct+maj3) — spread voicing with major color

Each voice has independent phase, envelope, masking (PRNG/burst), and DC filter state. Volume is normalized by voice count (not active voices) so adding/removing voices doesn't cause level jumps.

### Combination Recipes

| Recipe | Pulsaret | Window | Duty | Formants | Masking | Character |
|--------|----------|--------|------|----------|---------|-----------|
| Classic pulsar | Sinc (3.0) | Gaussian (1.0) | 40% | F1=200 | Off | Clean buzzing tone |
| Vocal drone | Sine (0.0) | Hann (2.0) | 60% | F1=270, F2=2300 | Off | "Ah" vowel sound |
| Metallic bell | Formant (7.0) | Exp decay (3.0) | 30% | F1=200, F2=347, F3=511 | Off | Inharmonic ring |
| Particle cloud | Noise (9.0) | Gaussian (1.0) | 10% | F1=800 | Stochastic 70% | Sparse noise bursts |
| Rhythmic bass | Saw (5.0) | Rectangular (0.0) | 50% | F1=80 | Burst 3on/2off | Stuttering low tone |
| Plucked string | Sinc (3.0) | Exp decay (3.0) | 45% | F1=400, F2=800 | Off | Percussive pluck |
| Harsh industrial | Square (6.0) | Rectangular (0.0) | 25% | F1=150, F2=1500 | Stochastic 40% | Aggressive buzz |
| Organ stack | Sine (0.0) | Hann (2.0) | 80% | F1=200, F2=800 | Off | 4V Octaves — rich organ |
| Power drone | Saw (5.0) | Gaussian (1.0) | 50% | F1=100 | Off | 4V Power — heavy fifth-stack |
| Maj7 pad | Sinc (3.0) | Gaussian (1.0) | 60% | F1=300, F2=900 | Off | 4V Maj7 — warm jazz chord |
| Dissonant cloud | Noise (9.0) | Exp decay (3.0) | 15% | F1=500 | Stochastic 60% | 4V Dim — eerie cluster |

## License

Plugin source code is provided as-is. The disting NT API is copyright Expert Sleepers Ltd under the MIT License.
