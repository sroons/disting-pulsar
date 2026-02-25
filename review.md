# Pulsar Plugin Code Review

Performance and sound quality findings for `src/pulsar.cpp`. All suggestions are intended to improve performance and/or sound quality without negatively impacting sound quality.

---

## Performance

### 1. Precompute reciprocal of sample rate per block

**Location:** `step()`, line 850

The division `freqHz / sr` occurs every sample inside the main loop. Division is expensive on Cortex-M7. Precomputing `float invSr = 1.0f / sr;` once at the top of `step()` and replacing with `freqHz * invSr` saves one hardware division per sample.

**Benefit:** Eliminates one floating-point division per sample. On Cortex-M7 FPv5, `fdiv` takes ~14 cycles vs ~1 cycle for `fmul`.

---

### 2. Precompute reciprocal of duty per formant

**Location:** `step()`, line 915

Inside the per-sample, per-formant inner loop: `float pulsaretPhase = phase / duty;`. The `duty` value is constant for the entire block (computed at lines 817-833). Precomputing `float invDuty[3]` before the sample loop and using `phase * invDuty[f]` would eliminate a division from the hottest loop in the plugin.

**Benefit:** Eliminates one division per sample per active formant from the innermost loop. With 3 formants at 48kHz, that is up to 144,000 divisions per second replaced with multiplications.

---

### 3. Remove no-op mask smooth hold assignment

**Location:** `step()`, line 901

```cpp
dtc->maskSmooth[f] = dtc->maskSmooth[f]; // hold
```

This is a self-assignment that compiles to a pointless memory read and write every sample for every formant on non-pulse samples (the vast majority of samples). Removing this line entirely preserves identical behavior since the value already persists in memory.

**Benefit:** Eliminates unnecessary memory load/store traffic on non-pulse samples. With formantCount=3, this removes up to 3 wasted memory writes per sample.

---

### 4. Combine CV averaging into a single loop

**Location:** `step()`, lines 780-794

Three separate loops iterate over the entire buffer to compute CV averages:
```cpp
for (int i = 0; i < numFrames; ++i) cvFormantAvg += cvFormant[i];
for (int i = 0; i < numFrames; ++i) cvDutyAvg += cvDuty[i];
for (int i = 0; i < numFrames; ++i) cvMaskAvg += cvMask[i];
```

These can be merged into a single loop, improving cache locality since the CV buffers are in the same region of bus memory.

**Benefit:** Reduces loop overhead by 2x and improves data cache utilization by processing adjacent memory in a single pass.

---

### 5. Replace per-sample `exp2f` with fast approximation for pitch CV

**Location:** `step()`, line 847

```cpp
freqHz *= exp2f(cvPitch[i]);
```

`exp2f()` is called every sample when pitch CV is connected. On Cortex-M7 without hardware exp2, this is a costly libm call (~50-100 cycles). A fast polynomial approximation (e.g., the classic integer bit-manipulation trick or a cubic polynomial over [-1,1] with octave range reduction) can achieve sufficient accuracy for V/Oct pitch tracking at a fraction of the cost.

**Benefit:** Potential ~5-10x speedup for the exp2 computation per sample. Pitch CV accuracy within ~1 cent is achievable with a 3rd-order polynomial approximation, which is well within musical tolerance.

---

### 6. Hoist `maskSmoothCoeff` out of the sample loop

**Location:** `step()`, line 895

```cpp
float maskSmoothCoeff = 0.995f;
```

This constant is declared inside the per-sample loop. While the compiler may optimize this, explicitly declaring it before the loop makes the intent clear and guarantees no per-sample overhead.

**Benefit:** Minor; ensures no redundant stack allocation per iteration if the compiler doesn't hoist it.

---

### 7. Precompute formant ratio outside the sample loop (table-based mode)

**Location:** `step()`, line 931

```cpp
float formantRatio = pThis->formantHz[f] * formantCvMul / (dtc->fundamentalHz > 0.1f ? dtc->fundamentalHz : 0.1f);
```

When pitch CV is not connected, `fundamentalHz` changes slowly (only via glide). The formant ratio could be computed once per block and updated incrementally, avoiding a per-sample division per formant. When pitch CV is connected, the frequency changes per sample, so this optimization applies only to the non-CV case.

**Benefit:** Eliminates one division per sample per formant in the common case (no pitch CV). Falls back to per-sample when pitch CV is active.

---

## Sound Quality

### 8. Fix mask smoothing to run continuously

**Location:** `step()`, lines 895-903

The mask smooth filter currently only updates on pulse boundaries:
```cpp
if (newPulse)
    dtc->maskSmooth[f] = maskGain + maskSmoothCoeff * (dtc->maskSmooth[f] - maskGain);
else
    dtc->maskSmooth[f] = dtc->maskSmooth[f]; // hold
```

This means the mask transitions are effectively instantaneous -- the one-pole filter runs for exactly one sample at each pulse, providing negligible smoothing. Between pulses, the value is held (no-op), so the filter never converges toward the target. The result is that mask on/off transitions produce discontinuities (clicks), especially at lower fundamental frequencies where pulses are further apart.

**Fix:** Store the mask target per-formant and run the one-pole filter every sample:
```cpp
// On new pulse, update target:
if (newPulse) dtc->maskTarget[f] = maskGain;
// Every sample, smooth toward target:
dtc->maskSmooth[f] = dtc->maskTarget[f] + maskSmoothCoeff * (dtc->maskSmooth[f] - dtc->maskTarget[f]);
```

**Benefit:** Eliminates clicks on mask transitions. The one-pole filter will properly smooth the mask gain over multiple samples, producing clean crossfade-like transitions between masked and unmasked pulses.

---

### 9. Make LeakDC coefficient sample-rate dependent

**Location:** `step()`, lines 962, 967

```cpp
float yL = xL - dtc->leakDC_xL + 0.995f * dtc->leakDC_yL;
```

The DC-blocking filter coefficient is hardcoded to `0.995`. This gives a cutoff frequency that depends on sample rate:
- At 48kHz: ~38 Hz cutoff
- At 96kHz: ~19 Hz cutoff

The cutoff should be consistent regardless of sample rate. Precompute the coefficient as `1.0 - (2*pi*fc / sr)` where `fc` is the desired cutoff frequency (e.g., 20-30 Hz), and store it in the DTC struct (updated on construction or sample rate change).

**Benefit:** Consistent low-frequency response across different sample rates. At higher sample rates, the current hardcoded value removes less DC offset than intended; at lower rates it cuts into audible bass content.

---

### 10. Make mask smooth coefficient sample-rate dependent

**Location:** `step()`, line 895

```cpp
float maskSmoothCoeff = 0.995f;
```

Same issue as the LeakDC coefficient -- the smoothing time constant depends on sample rate. At 48kHz this gives a ~4.6ms time constant; at 96kHz it doubles to ~9.2ms. Should derive from a desired time constant in ms using `coeffFromMs()`, which is already available.

**Benefit:** Consistent mask smoothing behavior across sample rates.

---

### 11. Guard against negative sample buffer index

**Location:** `step()`, lines 924-925

```cpp
if (sIdx >= pThis->sampleLoadedFrames - 1) sIdx = pThis->sampleLoadedFrames - 2;
```

If `sampleLoadedFrames` is 0 or 1, `sampleLoadedFrames - 2` is negative, causing an out-of-bounds read into `dram->sampleBuffer`. The outer check `pThis->sampleLoadedFrames > 0` at line 918 allows `sampleLoadedFrames == 1`, which would result in `sIdx = -1`.

**Fix:** Change the guard to require `sampleLoadedFrames >= 2`, or clamp `sIdx` to `max(0, sampleLoadedFrames - 2)`.

**Benefit:** Prevents potential out-of-bounds memory access that could produce audio glitches or crashes with very short samples.

---

### 12. Velocity is stored but unused

**Location:** `midiMessage()`, line 661; `step()`

The MIDI velocity is captured into `dtc->velocity` but is never applied during audio rendering. In standard synthesizer behavior, velocity scales the amplitude of the note. Applying velocity would improve expressiveness:

```cpp
// In step(), where envelope is applied:
float vel = dtc->velocity / 127.0f;
sumL *= dtc->envValue * amplitude * vel;
sumR *= dtc->envValue * amplitude * vel;
```

**Benefit:** Adds velocity sensitivity, which is expected behavior for a MIDI instrument and improves playability. Without it, all notes sound at the same loudness regardless of how they are played.

---

### 13. Use `floorf()` instead of `(int)` cast for phase wrapping

**Location:** `step()`, line 933

```cpp
tablePhase -= (int)tablePhase; // wrap to 0-1
```

Cast to `int` truncates toward zero in C/C++. For negative values of `tablePhase` (which shouldn't normally occur but could due to floating-point edge cases), this truncation goes the wrong direction, leaving a negative result. The follow-up `if (tablePhase < 0.0f) tablePhase += 1.0f;` partially handles this but only corrects by one period. Using `tablePhase = fmodf(tablePhase, 1.0f)` or `tablePhase -= floorf(tablePhase)` is more robust.

**Benefit:** More robust phase wrapping prevents potential discontinuities at extreme formant ratios or edge cases.

---

## Build / Compiler

### 14. Consider `-O2` instead of `-Os` for the step function

**Location:** `Makefile`, line 14

The build uses `-Os` (optimize for size). For a DSP-intensive audio plugin running on Cortex-M7, `-O2` (optimize for speed) would allow the compiler to unroll inner loops, inline more aggressively, and better schedule FPU instructions. The code size increase is typically small for a single-file plugin. Alternatively, use `__attribute__((optimize("O2")))` on just the `step()` function to keep the rest size-optimized.

**Benefit:** Potentially significant performance improvement in the audio processing loop. `-Os` can inhibit loop unrolling and instruction scheduling optimizations that matter for the per-sample inner loop.

---

### 15. Add `-ffast-math` for the step function

**Location:** `Makefile`, line 14

The plugin does not use `-ffast-math`. Enabling it (or the more targeted `-fno-math-errno -fno-trapping-math -ffinite-math-only -funsafe-math-optimizations`) would allow the compiler to:
- Reorder floating-point operations
- Use reciprocal approximations for division
- Skip NaN/Inf checks
- Use fused multiply-add (FMA) instructions on Cortex-M7 FPv5

The plugin already uses fast approximations (fastTanh) and doesn't rely on strict IEEE 754 semantics, so this is safe.

**Benefit:** Enables FMA instructions and other FPU optimizations. Cortex-M7 FPv5 has hardware FMA which can combine multiply+add into a single cycle, but the compiler won't emit these without `-ffast-math` or `-ffp-contract=fast`.
